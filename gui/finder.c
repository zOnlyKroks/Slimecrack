#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "both.h"
#include "chunks.h"
#include "crack.h"
#include "render.h"
#include "slime.h"

#define FIELD_LEN 64
#define TAB_H 44
#define MAP_TOP (TAB_H + 48)
#define FOOT_H 26
#define MIN_SCALE 1.5
#define MAX_SCALE 96.0
#define SHOWN_RESULTS 10
#define BASE_W 1000.0f
#define BASE_H 700.0f

#define BG      ((Color){ 24, 24, 26, 255 })
#define LINE    ((Color){ 62, 62, 67, 255 })
#define FG      ((Color){ 226, 226, 226, 255 })
#define DIM     ((Color){ 132, 132, 138, 255 })
#define ACCENT  ((Color){ 126, 191, 110, 255 })
#define INK     ((Color){ 18, 18, 20, 255 })

typedef struct { char text[FIELD_LEN]; } Field;
typedef struct { double cx, cz, scale; } View;
typedef enum { IDLE, RUNNING, DONE } RunState;

typedef struct {
    Chunks *ch, *work;
    int nslime;
    int64_t bx0, bz0, bx1, bz1;
    Mode mode;   /* what to crack: MODE_SEED, MODE_SALT (scrambler) or MODE_BOTH */
    Field scr, world;
    char file[128];
    Ctx ctx;
    int show_list, show_sug;
    float list_scroll;
    Suggestion sug[SUGGEST_MAX];
    int nsug, sug_left;
    char note[160];
    Results res;
    int matches;
    _Atomic int running;
    RunState state;
    pthread_t th;
    double t0, t1;
} Crack;

static Font g_font;
static Field *g_focus;
static _Atomic int g_pct;
static float g_scale = 1;   /* UI scale: everything is laid out in logical units and scaled by this */

/* Mouse position in logical (unscaled) units. */
static Vector2 mouse_pos(void) {
    const Vector2 m = GetMousePosition();
    return (Vector2){ m.x / g_scale, m.y / g_scale };
}

/* Mouse movement since the last frame in logical units. */
static Vector2 mouse_delta(void) {
    const Vector2 d = mouse_delta();
    return (Vector2){ d.x / g_scale, d.y / g_scale };
}

/* Window size in logical units. */
static int logical_w(void) { return (int)(GetScreenWidth() / g_scale); }
static int logical_h(void) { return (int)(GetScreenHeight() / g_scale); }

/* Scissor rectangle given in logical units (scissoring works on real pixels). */
static void scissor_begin(const int x, const int y, const int w, const int h) {
    BeginScissorMode((int)(x * g_scale), (int)(y * g_scale), (int)ceilf(w * g_scale), (int)ceilf(h * g_scale));
}

static void set_progress(const int pct) { g_pct = pct; }

static void text(const char *s, const float x, const float y, const float size, const Color c) {
    DrawTextEx(g_font, s, (Vector2){ x, y }, size, 0, c);
}

static float text_w(const char *s, const float size) {
    return MeasureTextEx(g_font, s, size, 0).x;
}

/* Loads the UI font with a glyph atlas of the given pixel size. */
static Font load_font(const int px) {
    const char *candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/SFNS.ttf",
        "C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
        if (!FileExists(candidates[i])) continue;
        const Font f = LoadFontEx(candidates[i], px, NULL, 0);
        if (f.glyphCount > 0) { SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR); return f; }
    }
    return GetFontDefault();
}

/* Java's String.hashCode, which Minecraft uses for non-numeric seeds. */
static int64_t java_hash(const char *s) {
    int32_t h = 0;
    for (; *s; s++) h = (int32_t)((uint32_t)h * 31u + (uint8_t)*s);
    return h;
}

static int64_t parse_seed(const char *s) {
    if (!*s) return 0;
    char *end;
    const long long v = strtoll(s, &end, 10);
    return *end ? java_hash(s) : v;
}

static int64_t parse_num(const char *s) {
    return strtoll(s, NULL, 0);
}

static int hovering(const Rectangle r) {
    return CheckCollisionPointRec(mouse_pos(), r);
}

static int clicked(const Rectangle r) {
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hovering(r);
}

static int button(const Rectangle r, const char *label, const int enabled, const int active) {
    const int hot = enabled && hovering(r);
    DrawRectangleRec(r, active ? ACCENT : hot ? (Color){ 54, 54, 59, 255 } : (Color){ 40, 40, 44, 255 });
    const Color fc = !enabled ? (Color){ 92, 92, 98, 255 } : active ? INK : FG;
    text(label, r.x + (r.width - text_w(label, 15)) / 2, r.y + (r.height - 15) / 2 - 1, 15, fc);
    return enabled && clicked(r);
}

/* Returns 1 on the frame Enter is pressed inside the field. */
static int field(Field *f, const Rectangle box) {
    if (clicked(box)) g_focus = f;
    const int focused = g_focus == f;
    int entered = 0;
    DrawRectangleRec(box, INK);
    DrawRectangle((int)box.x, (int)(box.y + box.height - 2), (int)box.width, 2, focused ? ACCENT : LINE);
    if (focused) {
        size_t n = strlen(f->text);
        for (int c = GetCharPressed(); c; c = GetCharPressed())
            if (c >= 32 && c < 127 && n < FIELD_LEN - 1) { f->text[n++] = (char)c; f->text[n] = 0; }
        const int mod = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_CONTROL);
        if (mod && IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            for (; clip && *clip && n < FIELD_LEN - 1; clip++)
                if (*clip >= 32 && *clip < 127) f->text[n++] = *clip;
            f->text[n] = 0;
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && n > 0) f->text[n - 1] = 0;
        entered = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    }
    text(f->text, box.x + 7, box.y + (box.height - 16) / 2 - 1, 16, FG);
    if (focused && ((int)(GetTime() * 2) & 1))
        DrawRectangle((int)(box.x + 8 + text_w(f->text, 16)), (int)box.y + 6, 1, (int)box.height - 12, FG);
    return entered;
}

static void chunks_bounds(Crack *c) {
    c->nslime = 0;
    if (!c->ch->n) return;
    c->bx0 = c->bx1 = c->ch->v[0].x;
    c->bz0 = c->bz1 = c->ch->v[0].z;
    for (int k = 0; k < c->ch->n; k++) {
        const Obs *o = &c->ch->v[k];
        c->nslime += o->slime;
        if (o->x < c->bx0) c->bx0 = o->x;
        if (o->x > c->bx1) c->bx1 = o->x;
        if (o->z < c->bz0) c->bz0 = o->z;
        if (o->z > c->bz1) c->bz1 = o->z;
    }
}

/* Native file dialog; blocks until closed. Returns 1 and fills path if a file was chosen. */
static int choose_file(char *path, const size_t n) {
#if defined(__APPLE__)
    const char *cmd = "osascript -e 'POSIX path of (choose file with prompt \"Chunk list (.csv)\")' 2>/dev/null";
#elif defined(_WIN32)
    const char *cmd = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
                      "$d = New-Object System.Windows.Forms.OpenFileDialog; $d.Filter = 'CSV|*.csv|All|*.*'; "
                      "if ($d.ShowDialog() -eq 'OK') { $d.FileName }\"";
#else
    const char *cmd = "zenity --file-selection --title='Chunk list (.csv)' 2>/dev/null";
#endif
    FILE *p = 
#ifdef _WIN32
        _popen(cmd, "r");
#else
        popen(cmd, "r");
#endif
    if (!p) return 0;
    const int ok = fgets(path, (int)n, p) != NULL;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    path[strcspn(path, "\r\n")] = 0;
    return ok && path[0];
}

static void chunks_open(Crack *c, const char *path) {
    c->ch->n = 0;
    if (chunks_load(c->ch, path)) snprintf(c->file, sizeof c->file, "%s", GetFileName(path));
    else c->file[0] = 0;
    chunks_bounds(c);
}

static int cmp_u64(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* Sorts both-mode results by seed, keeping each scrambler with its seed. */
static void sort_pairs(Results *r) {
    for (int i = 1; i < r->n; i++)
        for (int j = i; j > 0 && r->v[j - 1] > r->v[j]; j--) {
            const uint64_t a = r->v[j], b = r->w2[j];
            r->v[j] = r->v[j - 1]; r->w2[j] = r->w2[j - 1];
            r->v[j - 1] = a; r->w2[j - 1] = b;
        }
}

static void *worker(void *arg) {
    Crack *c = arg;
    if (c->mode == MODE_BOTH) {
        c->matches = crack_both(c->work, &c->res);
        sort_pairs(&c->res);
    } else {
        c->matches = crack(&c->ctx, c->work, 0, &c->res);
        if (c->res.n > 0) qsort(c->res.v, (size_t)c->res.n, sizeof c->res.v[0], cmp_u64);
    }
    if (c->res.n > 1) c->nsug = suggest_chunks(&c->ctx, c->ch, &c->res, c->sug, SUGGEST_MAX, &c->sug_left);
    atomic_store(&c->running, 0);
    return NULL;
}

static void crack_start(Crack *c) {
    c->note[0] = 0;
    c->show_list = c->show_sug = 0;
    c->nsug = c->sug_left = 0;
    free(c->res.v);
    free(c->res.w2);
    c->res = (Results){ 0 };
    c->work = chunks_new();
    memcpy(c->work->v, c->ch->v, sizeof(Obs) * (size_t)c->ch->n);
    c->work->n = c->ch->n;
    chunks_fill_non_slime(c->work);
    c->ctx = (Ctx){ .mode = c->mode };
    if (c->mode == MODE_SALT) c->ctx.worldSeed = (uint64_t)parse_seed(c->world.text);
    else if (c->mode == MODE_SEED) c->ctx.K = ((uint64_t)parse_num(c->scr.text) ^ MUL) & MASK48;
    c->matches = 0;
    g_pct = 0;
    c->t0 = GetTime();
    atomic_store(&c->running, 1);
    c->state = RUNNING;
    pthread_create(&c->th, NULL, worker, c);
}

static void crack_poll(Crack *c) {
    if (c->state != RUNNING || atomic_load(&c->running)) return;
    pthread_join(c->th, NULL);
    chunks_free(c->work);
    c->t1 = GetTime();
    c->state = DONE;
}

static void fmt_time(char *out, const size_t n, const double s) {
    snprintf(out, n, "%d:%02d", (int)s / 60, (int)s % 60);
}

/* Asks where to save the exported list; returns 1 and fills path when a file was chosen. */
static int save_file(char *path, const size_t n) {
#if defined(__APPLE__)
    const char *cmd = "osascript -e 'POSIX path of (choose file name default name \"results.csv\")' 2>/dev/null";
#elif defined(_WIN32)
    const char *cmd = "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
                      "$d = New-Object System.Windows.Forms.SaveFileDialog; $d.Filter = 'CSV|*.csv|All|*.*'; "
                      "$d.FileName = 'results.csv'; if ($d.ShowDialog() -eq 'OK') { $d.FileName }\"";
#else
    const char *cmd = "zenity --file-selection --save --confirm-overwrite --filename=results.csv 2>/dev/null";
#endif
    FILE *p =
#ifdef _WIN32
        _popen(cmd, "r");
#else
        popen(cmd, "r");
#endif
    if (!p) return 0;
    const int ok = fgets(path, (int)n, p) != NULL;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    path[strcspn(path, "\r\n")] = 0;
    return ok && path[0];
}

/* Writes every result as CSV: seed / scrambler / seed,scrambler depending on the mode. Returns 1 on success. */
static int export_results(const Crack *c, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    if (c->mode == MODE_BOTH) fprintf(f, "seed,scrambler\n");
    else fprintf(f, "%s\n", c->mode == MODE_SALT ? "scrambler" : "seed");
    for (int k = 0; k < c->res.n; k++) {
        if (c->mode == MODE_BOTH) fprintf(f, "%llu,%llu\n", (unsigned long long)c->res.v[k], (unsigned long long)c->res.w2[k]);
        else fprintf(f, "%llu\n", (unsigned long long)c->res.v[k]);
    }
    fclose(f);
    return 1;
}

/* Full-window scrollable list of all results; returns the index whose "map" button was clicked, or -1. */
static int list_view(Crack *c, const int W) {
    const int H = logical_h(), rowh = 30, top = 120;
    const float x = 24;
    int open = -1;
    char s[96];

    text(c->mode == MODE_SALT ? "scrambler" : "seed", x + 50, 74, 13, DIM);
    text(c->mode == MODE_BOTH ? "scrambler" : "hex", x + 290, 74, 13, DIM);
    snprintf(s, sizeof s, "%d results", c->res.n);
    text(s, x, 94, 14, FG);
    if (button((Rectangle){ (float)W - 220, 62, 92, 28 }, "export", 1, 0)) {
        char path[1024];
        if (save_file(path, sizeof path))
            snprintf(c->note, sizeof c->note, export_results(c, path) ? "saved %s" : "could not write %s", GetFileName(path));
    }
    if (button((Rectangle){ (float)W - 120, 62, 96, 28 }, "close", 1, 0)) c->show_list = 0;
    if (c->note[0]) text(c->note, x + 200, 94, 13, DIM);

    const float visible = (float)(H - top - 10);
    const float total = (float)(c->res.n * rowh);
    c->list_scroll -= GetMouseWheelMove() * rowh * 3;
    if (c->list_scroll > total - visible) c->list_scroll = total - visible;
    if (c->list_scroll < 0) c->list_scroll = 0;

    scissor_begin(0, top, W, (int)visible);
    for (int k = 0; k < c->res.n; k++) {
        const float y = (float)top + (float)(k * rowh) - c->list_scroll;
        if (y < (float)top - rowh || y > (float)H) continue;
        snprintf(s, sizeof s, "%d", k + 1);
        text(s, x, y + 6, 13, DIM);
        snprintf(s, sizeof s, "%llu", (unsigned long long)c->res.v[k]);
        text(s, x + 50, y + 5, 16, FG);
        if (c->mode == MODE_BOTH) snprintf(s, sizeof s, "%llu", (unsigned long long)c->res.w2[k]);
        else snprintf(s, sizeof s, "%012llx", (unsigned long long)c->res.v[k]);
        text(s, x + 290, y + 6, 14, DIM);
        if (y >= (float)top - 4 && button((Rectangle){ x + 520, y, 48, 26 }, "map", 1, 0)) open = k;
    }
    EndScissorMode();
    return open;
}

/* Full-window list of the chunks to observe next, each with the candidates it separates. */
static void sug_view(Crack *c, const int W) {
    const int H = logical_h(), rowh = 30, top = 120;
    const float x = 24;
    const int ncand = c->res.n < SUGGEST_CAND ? c->res.n : SUGGEST_CAND;
    char s[256];

    snprintf(s, sizeof s, "%d chunks tell the %d candidates apart", c->nsug, ncand - c->sug_left);
    text(s, x, 74, 14, FG);
    text("check these chunks (slime or not) and add them to the csv as x,z,1 or x,z,0", x, 94, 13, DIM);
    if (button((Rectangle){ (float)W - 120, 62, 96, 28 }, "close", 1, 0)) c->show_sug = 0;

    const float visible = (float)(H - top - 10);
    const float total = (float)((c->nsug + (c->sug_left > 0)) * rowh);
    c->list_scroll -= GetMouseWheelMove() * rowh * 3;
    if (c->list_scroll > total - visible) c->list_scroll = total - visible;
    if (c->list_scroll < 0) c->list_scroll = 0;

    scissor_begin(0, top, W, (int)visible);
    for (int p = 0; p < c->nsug; p++) {
        const float y = (float)top + (float)(p * rowh) - c->list_scroll;
        if (y < (float)top - rowh || y > (float)H) continue;
        snprintf(s, sizeof s, "%d", p + 1);
        text(s, x, y + 6, 13, DIM);
        snprintf(s, sizeof s, "%lld, %lld", (long long)c->sug[p].x, (long long)c->sug[p].z);
        text(s, x + 40, y + 5, 16, FG);

        int slime = 0;
        for (int k = 0; k < ncand; k++) slime += (int)((c->sug[p].slime >> k) & 1);
        const int want = slime * 2 <= ncand;   /* list the smaller side of the split */
        int len = snprintf(s, sizeof s, want ? "slime for #" : "not slime for #");
        for (int k = 0, first = 1; k < ncand && len < (int)sizeof s - 8; k++)
            if ((int)((c->sug[p].slime >> k) & 1) == want) {
                len += snprintf(s + len, sizeof s - (size_t)len, first ? "%d" : ",%d", k + 1);
                first = 0;
            }
        text(s, x + 220, y + 6, 14, DIM);
    }
    if (c->sug_left > 0) {
        snprintf(s, sizeof s, "%d candidates stay indistinguishable within %d chunks of the data", c->sug_left, SUGGEST_RADIUS);
        text(s, x, (float)top + (float)(c->nsug * rowh) - c->list_scroll + 6, 13, DIM);
    }
    EndScissorMode();
}

/* Returns the index of the result whose "map" button was clicked, or -1. */
static int crack_tab(Crack *c, const int W) {
    const float x = 24;
    int open = -1;
    const int running = c->state == RUNNING;
    if (c->show_list) return list_view(c, W);
    if (c->show_sug) { sug_view(c, W); return -1; }

    if (button((Rectangle){ x, 64, 92, 28 }, "seed", !running, c->mode == MODE_SEED)) c->mode = MODE_SEED;
    if (button((Rectangle){ x + 96, 64, 92, 28 }, "scrambler", !running, c->mode == MODE_SALT)) c->mode = MODE_SALT;
    if (button((Rectangle){ x + 192, 64, 92, 28 }, "both", !running, c->mode == MODE_BOTH)) c->mode = MODE_BOTH;

    if (c->mode == MODE_BOTH) {
        text("needs a dense block of chunks, both unknown", x, 108, 13, DIM);
    } else {
        text(c->mode == MODE_SALT ? "world seed" : "scrambler", x, 108, 13, DIM);
        field(c->mode == MODE_SALT ? &c->world : &c->scr, (Rectangle){ x, 126, 280, 30 });
    }

    char info[128];
    if (button((Rectangle){ x, 170, 92, 28 }, "open csv", !running, 0)) {
        char path[1024];
        if (choose_file(path, sizeof path)) chunks_open(c, path);
    }
    if (c->ch->n) {
        snprintf(info, sizeof info, "%s   %d chunks, %d slime", c->file, c->ch->n, c->nslime);
        text(info, x + 104, 175, 14, FG);
    } else {
        text(c->file[0] ? "no chunks found in file" : "or drop a file here", x + 104, 175, 14, DIM);
    }

    if (button((Rectangle){ x, 216, 92, 32 }, running ? "running" : "start", !running && c->nslime > 0, 0)) crack_start(c);
    const int has = c->state == DONE && c->res.n > 0;
    if (button((Rectangle){ x + 96, 216, 92, 32 }, "list", has, 0)) { c->show_list = 1; c->list_scroll = 0; }
    if (button((Rectangle){ x + 288, 216, 92, 32 }, "next chunks", c->state == DONE && c->nsug > 0, 0)) { c->show_sug = 1; c->list_scroll = 0; }
    if (button((Rectangle){ x + 192, 216, 92, 32 }, "export", has, 0)) {
        char path[1024];
        if (save_file(path, sizeof path))
            snprintf(c->note, sizeof c->note, export_results(c, path) ? "saved %s" : "could not write %s", GetFileName(path));
    }

    char t[32];
    if (running) {
        const int pct = g_pct;
        DrawRectangle((int)x, 266, 280, 3, LINE);
        DrawRectangle((int)x, 266, 280 * pct / 100, 3, ACCENT);
        fmt_time(t, sizeof t, GetTime() - c->t0);
        snprintf(info, sizeof info, "%d%%   %s", pct, t);
        text(info, x, 276, 14, DIM);
    } else if (c->state == DONE) {
        fmt_time(t, sizeof t, c->t1 - c->t0);
        if (c->res.n == 0) snprintf(info, sizeof info, "no solutions   %s", t);
        else if (c->res.n == 1) snprintf(info, sizeof info, "%s   %s", c->mode == MODE_BOTH ? "seed + scrambler" : c->mode == MODE_SALT ? "scrambler" : "seed", t);
        else snprintf(info, sizeof info, "%d candidates   %s", c->res.n, t);
        text(info, x, 266, 14, DIM);
        for (int k = 0; k < c->res.n && k < SHOWN_RESULTS; k++) {
            const float y = 296 + (float)k * 30;
            const unsigned long long v = c->res.v[k];
            snprintf(info, sizeof info, "%llu", v);
            text(info, x, y + 5, 16, FG);
            if (c->mode == MODE_BOTH) snprintf(info, sizeof info, "%llu", (unsigned long long)c->res.w2[k]);
            else snprintf(info, sizeof info, "%012llx", v);
            text(info, x + 190, y + 6, 13, DIM);
            if (button((Rectangle){ x + 280, y, 48, 26 }, "map", 1, 0)) open = k;
        }
        if (c->res.n > SHOWN_RESULTS) {
            snprintf(info, sizeof info, "+%d more", c->res.n - SHOWN_RESULTS);
            text(info, x, 296 + SHOWN_RESULTS * 30 + 4, 13, DIM);
        }
    }

    const float px = 410, py = 64, pw = (float)W - px - 24;
    if (c->ch->n && c->ch->n <= 20000 && pw > 40) {
        const double gw = (double)(c->bx1 - c->bx0 + 1), gh = (double)(c->bz1 - c->bz0 + 1);
        double cell = pw / gw < 420 / gh ? pw / gw : 420 / gh;
        if (cell > 16) cell = 16;
        if (cell < 1) cell = 1;
        DrawRectangle((int)px, (int)py, (int)(gw * cell), (int)(gh * cell), INK);
        for (int k = 0; k < c->ch->n; k++) {
            const Obs *o = &c->ch->v[k];
            if (o->slime)
                DrawRectangle((int)(px + (double)(o->x - c->bx0) * cell), (int)(py + (double)(o->z - c->bz0) * cell),
                              (int)ceil(cell), (int)ceil(cell), ACCENT);
        }
    }
    return open;
}

/* One pixel per visible chunk; scaled up with nearest filtering when drawn. */
static void rebuild(Texture2D *tex, Image *img, const Ctx *ctx, const uint64_t seed,
                    const int64_t x0, const int64_t z0, const int w, const int h) {
    if (img->width != w || img->height != h) {
        if (img->data) UnloadImage(*img);
        *img = GenImageColor(w, h, BLANK);
        if (tex->id) UnloadTexture(*tex);
        *tex = LoadTextureFromImage(*img);
        SetTextureFilter(*tex, TEXTURE_FILTER_POINT);
    }
    Color *px = img->data;
    const Color rock = { 40, 40, 45, 255 };
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            px[j * w + i] = is_slime(ctx, seed, offs(x0 + i, z0 + j)) ? ACCENT : rock;
    UpdateTexture(*tex, img->data);
}

int gui_main(void) {
    render_set_progress_sink(set_progress);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1000, 700, "SlimeFinder");
    SetWindowMinSize(760, 520);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    g_font = load_font(20);
    int font_px = 20;

    Crack crack_s = { .ch = chunks_new(), .scr = { "987234911" }, .world = { "0" } };
    Field map_seed = { "0" }, map_scr = { "987234911" }, go_x = { "" }, go_z = { "" };
    View v = { 0.5, 0.5, 12 };
    Texture2D tex = { 0 };
    Image img = { 0 };
    int tab = 0, dirty = 1, dragging = 0, tw = 0, th = 0;
    int64_t tx0 = 0, tz0 = 0;
    char last_seed[FIELD_LEN] = "", last_scr[FIELD_LEN] = "";
    Ctx ctx = { .mode = MODE_SEED };
    uint64_t seed = 0;

    while (!WindowShouldClose()) {
        /* scale to the window (1.0 at the initial 1000x700), in steps of 0.25 so the font is not rebuilt constantly */
        float s = fminf((float)GetScreenWidth() / BASE_W, (float)GetScreenHeight() / BASE_H);
        s = floorf(s * 4) / 4;
        if (s < 0.75f) s = 0.75f;
        if (s > 3.0f) s = 3.0f;
        if (s != g_scale) {
            g_scale = s;
            const int px = (int)(20 * s + 0.5f);
            if (px != font_px) { UnloadFont(g_font); g_font = load_font(px); font_px = px; }
        }
        const int W = logical_w(), H = logical_h();
        const Vector2 m = mouse_pos();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_focus = NULL;
        crack_poll(&crack_s);

        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            if (files.count > 0 && crack_s.state != RUNNING) {
                chunks_open(&crack_s, files.paths[0]);
                tab = 0;
            }
            UnloadDroppedFiles(files);
        }

        BeginDrawing();
        ClearBackground(BG);
        BeginMode2D((Camera2D){ .zoom = g_scale });

        if (tab == 0) {
            const int open = crack_tab(&crack_s, W);
            if (open >= 0) {
                const unsigned long long r = crack_s.res.v[open];
                if (crack_s.mode == MODE_BOTH) {
                    snprintf(map_seed.text, FIELD_LEN, "%llu", r);
                    snprintf(map_scr.text, FIELD_LEN, "%llu", (unsigned long long)crack_s.res.w2[open]);
                } else if (crack_s.mode == MODE_SALT) {
                    snprintf(map_seed.text, FIELD_LEN, "%s", crack_s.world.text);
                    snprintf(map_scr.text, FIELD_LEN, "%llu", r);
                } else {
                    snprintf(map_seed.text, FIELD_LEN, "%llu", r);
                    snprintf(map_scr.text, FIELD_LEN, "%s", crack_s.scr.text);
                }
                if (crack_s.ch->n) { v.cx = (double)(crack_s.bx0 + crack_s.bx1) / 2 + 0.5; v.cz = (double)(crack_s.bz0 + crack_s.bz1) / 2 + 0.5; }
                tab = 1;
            }
        } else {
            const double mapH = H - MAP_TOP - FOOT_H;
            const Rectangle maprect = { 0, MAP_TOP, (float)W, (float)mapH };

            text("seed", 24, TAB_H + 14, 13, DIM);
            field(&map_seed, (Rectangle){ 62, TAB_H + 8, 220, 30 });
            text("scrambler", 304, TAB_H + 14, 13, DIM);
            field(&map_scr, (Rectangle){ 374, TAB_H + 8, 130, 30 });
            text("chunk", (float)W - 330, TAB_H + 14, 13, DIM);
            const int e1 = field(&go_x, (Rectangle){ (float)W - 280, TAB_H + 8, 70, 30 });
            const int e2 = field(&go_z, (Rectangle){ (float)W - 204, TAB_H + 8, 70, 30 });
            if (button((Rectangle){ (float)W - 128, TAB_H + 8, 48, 30 }, "go", 1, 0) || e1 || e2) {
                v.cx = (double)parse_num(go_x.text) + 0.5;
                v.cz = (double)parse_num(go_z.text) + 0.5;
                dirty = 1;
            }

            if (strcmp(last_seed, map_seed.text) || strcmp(last_scr, map_scr.text)) {
                snprintf(last_seed, FIELD_LEN, "%s", map_seed.text);
                snprintf(last_scr, FIELD_LEN, "%s", map_scr.text);
                seed = (uint64_t)parse_seed(map_seed.text);
                ctx.K = ((uint64_t)parse_num(map_scr.text) ^ MUL) & MASK48;
                dirty = 1;
            }

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, maprect)) dragging = 1;
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragging = 0;
            if (dragging) {
                const Vector2 d = GetMouseDelta();
                if (d.x != 0 || d.y != 0) { v.cx -= d.x / v.scale; v.cz -= d.y / v.scale; dirty = 1; }
            }
            const float wheel = GetMouseWheelMove();
            if (wheel != 0 && CheckCollisionPointRec(m, maprect)) {
                const double bx = v.cx + (m.x - W / 2.0) / v.scale, bz = v.cz + (m.y - MAP_TOP - mapH / 2.0) / v.scale;
                v.scale *= pow(1.15, wheel);
                if (v.scale < MIN_SCALE) v.scale = MIN_SCALE;
                if (v.scale > MAX_SCALE) v.scale = MAX_SCALE;
                v.cx = bx - (m.x - W / 2.0) / v.scale;
                v.cz = bz - (m.y - MAP_TOP - mapH / 2.0) / v.scale;
                dirty = 1;
            }
            if (!g_focus && IsKeyPressed(KEY_HOME)) { v.cx = 0.5; v.cz = 0.5; dirty = 1; }

            const double left = v.cx - W / 2.0 / v.scale, top = v.cz - mapH / 2.0 / v.scale;
            const int64_t x0 = (int64_t)floor(left), z0 = (int64_t)floor(top);
            const int w = (int)ceil(W / v.scale) + 2, h = (int)ceil(mapH / v.scale) + 2;
            if (x0 != tx0 || z0 != tz0 || w != tw || h != th) { tx0 = x0; tz0 = z0; tw = w; th = h; dirty = 1; }
            if (dirty) { rebuild(&tex, &img, &ctx, seed, x0, z0, w, h); dirty = 0; }

            scissor_begin(0, MAP_TOP, W, (int)mapH);
            DrawTexturePro(tex, (Rectangle){ 0, 0, (float)w, (float)h },
                           (Rectangle){ (float)((x0 - left) * v.scale), (float)(MAP_TOP + (z0 - top) * v.scale),
                                        (float)(w * v.scale), (float)(h * v.scale) }, (Vector2){ 0, 0 }, 0, WHITE);
            if (v.scale >= 3) {
                const int step = v.scale >= 12 ? 1 : 16;
                const Color line = step == 1 ? (Color){ 0, 0, 0, 60 } : (Color){ 255, 255, 255, 36 };
                for (int64_t x = (x0 / step) * step; x <= x0 + w; x += step) {
                    const float sx = (float)((x - left) * v.scale);
                    DrawLineV((Vector2){ sx, MAP_TOP }, (Vector2){ sx, (float)H }, line);
                }
                for (int64_t z = (z0 / step) * step; z <= z0 + h; z += step) {
                    const float sy = (float)(MAP_TOP + (z - top) * v.scale);
                    DrawLineV((Vector2){ 0, sy }, (Vector2){ (float)W, sy }, line);
                }
            }
            const float ax = (float)(-left * v.scale), ay = (float)(MAP_TOP - top * v.scale);
            DrawLineV((Vector2){ ax, MAP_TOP }, (Vector2){ ax, (float)H }, (Color){ 210, 90, 90, 150 });
            DrawLineV((Vector2){ 0, ay }, (Vector2){ (float)W, ay }, (Color){ 210, 90, 90, 150 });
            EndScissorMode();

            DrawRectangle(0, H - FOOT_H, W, FOOT_H, BG);
            if (CheckCollisionPointRec(m, maprect)) {
                const int64_t hx = (int64_t)floor(left + m.x / v.scale), hz = (int64_t)floor(top + (m.y - MAP_TOP) / v.scale);
                char info[120];
                snprintf(info, sizeof info, "%lld  %lld     block %lld  %lld     %s", (long long)hx, (long long)hz,
                         (long long)hx * 16, (long long)hz * 16, is_slime(&ctx, seed, offs(hx, hz)) ? "slime" : "-");
                text(info, 24, H - FOOT_H + 5, 14, DIM);
            }
        }

        DrawRectangle(0, 0, W, TAB_H, BG);
        const char *names[2] = { "crack", "map" };
        float tx = 24;
        for (int k = 0; k < 2; k++) {
            const float tw_ = text_w(names[k], 17);
            const Rectangle r = { tx - 8, 0, tw_ + 16, TAB_H };
            if (clicked(r)) tab = k;
            text(names[k], tx, 12, 17, tab == k ? FG : DIM);
            if (tab == k) DrawRectangle((int)tx, TAB_H - 3, (int)tw_, 2, ACCENT);
            tx += tw_ + 32;
        }
        DrawRectangle(0, TAB_H - 1, W, 1, LINE);

        while (GetCharPressed()) {}
        EndMode2D();
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
