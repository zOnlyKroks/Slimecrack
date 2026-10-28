#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
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
    int salt;
    Field scr, world;
    char file[128];
    Ctx ctx;
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

/* crack.c reports through these; the GUI reads the percentage instead of drawing a bar on stderr. */
void render_progress(const int pct) { g_pct = pct; }
void render_progress_done(void) {}

static void text(const char *s, const float x, const float y, const float size, const Color c) {
    DrawTextEx(g_font, s, (Vector2){ x, y }, size, 0, c);
}

static float text_w(const char *s, const float size) {
    return MeasureTextEx(g_font, s, size, 0).x;
}

static Font load_font(void) {
    const char *candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/SFNS.ttf",
        "C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
        if (!FileExists(candidates[i])) continue;
        const Font f = LoadFontEx(candidates[i], 32, NULL, 0);
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
    return CheckCollisionPointRec(GetMousePosition(), r);
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

static void *worker(void *arg) {
    Crack *c = arg;
    c->matches = crack(&c->ctx, c->work, 0, &c->res);
    if (c->res.n > 0) qsort(c->res.v, (size_t)c->res.n, sizeof c->res.v[0], cmp_u64);
    atomic_store(&c->running, 0);
    return NULL;
}

static void crack_start(Crack *c) {
    free(c->res.v);
    c->res = (Results){ 0 };
    c->work = chunks_new();
    memcpy(c->work->v, c->ch->v, sizeof(Obs) * (size_t)c->ch->n);
    c->work->n = c->ch->n;
    chunks_fill_non_slime(c->work);
    c->ctx = (Ctx){ .mode = c->salt ? MODE_SALT : MODE_SEED };
    if (c->salt) c->ctx.worldSeed = (uint64_t)parse_seed(c->world.text);
    else c->ctx.K = ((uint64_t)parse_num(c->scr.text) ^ MUL) & MASK48;
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

/* Returns the index of the result whose "map" button was clicked, or -1. */
static int crack_tab(Crack *c, const int W) {
    const float x = 24;
    int open = -1;
    const int running = c->state == RUNNING;

    if (button((Rectangle){ x, 64, 92, 28 }, "seed", !running, !c->salt)) c->salt = 0;
    if (button((Rectangle){ x + 96, 64, 92, 28 }, "scrambler", !running, c->salt)) c->salt = 1;

    text(c->salt ? "world seed" : "scrambler", x, 108, 13, DIM);
    field(c->salt ? &c->world : &c->scr, (Rectangle){ x, 126, 280, 30 });

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
        else if (c->res.n == 1) snprintf(info, sizeof info, "%s   %s", c->salt ? "scrambler" : "seed", t);
        else snprintf(info, sizeof info, "%d candidates   %s", c->res.n, t);
        text(info, x, 266, 14, DIM);
        for (int k = 0; k < c->res.n && k < SHOWN_RESULTS; k++) {
            const float y = 296 + (float)k * 30;
            const unsigned long long v = c->res.v[k];
            snprintf(info, sizeof info, "%llu", v);
            text(info, x, y + 5, 16, FG);
            snprintf(info, sizeof info, "%012llx", v);
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

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1000, 700, "SlimeFinder");
    SetWindowMinSize(760, 520);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    g_font = load_font();

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
        const int W = GetScreenWidth(), H = GetScreenHeight();
        const Vector2 m = GetMousePosition();
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

        if (tab == 0) {
            const int open = crack_tab(&crack_s, W);
            if (open >= 0) {
                const unsigned long long r = crack_s.res.v[open];
                if (crack_s.salt) {
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

            BeginScissorMode(0, MAP_TOP, W, (int)mapH);
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
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
