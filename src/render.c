#include <stdio.h>
#include <stdlib.h>
#include "render.h"

/* Draws the stderr progress bar for pct percent. */
void render_progress(const int pct) {
    const int filled = pct * 30 / 100;
    fprintf(stderr, "\r[%.*s%.*s] %3d%%", filled, "##############################",
            30 - filled, "------------------------------", pct);
    fflush(stderr);
}

/* Ends the progress bar line. */
void render_progress_done(void) {
    fprintf(stderr, "\n");
}

/* Comparison function for sorting uint64_t ascending. */
static int cmp_u64(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* Prints the chunk if the candidate seeds disagree about it; returns 1 if printed. */
static int split_cell(const Ctx *ctx, const Results *res, const int64_t x, const int64_t z, const int ncand) {
    const uint64_t c = offs(x, z);
    int votes = 0;
    for (int k = 0; k < ncand; k++) votes += is_slime(ctx, res->v[k], c);
    if (votes == 0 || votes == ncand) return 0;
    printf("  chunk %lld,%lld is slime for seed(s) #", (long long)x, (long long)z);
    int first = 1;
    for (int k = 0; k < ncand; k++)
        if (is_slime(ctx, res->v[k], c)) { printf(first ? "%d" : ",%d", k + 1); first = 0; }
    printf(", not slime for the rest\n");
    return 1;
}

/* Spirals outward from the data's centre and prints up to 6 chunks on which the candidate seeds disagree. */
static void print_distinguishing_chunks(const Ctx *ctx, const Chunks *ch, const Results *res) {
    int64_t cx = 0, cz = 0;
    for (int k = 0; k < ch->n; k++) { cx += ch->v[k].x; cz += ch->v[k].z; }
    cx /= ch->n; cz /= ch->n;
    const int ncand = res->n < 64 ? res->n : 64;
    int shown = 0;
    printf("distinguishing chunks (append to CSV as x,z,1 or x,z,0):\n");
    for (int64_t r = 0; r <= 2000 && shown < 6; r++) {
        for (int64_t i = -r; i <= r && shown < 6; i++) {
            shown += split_cell(ctx, res, cx + i, cz - r, ncand);
            if (r == 0) break;
            if (shown < 6) shown += split_cell(ctx, res, cx + i, cz + r, ncand);
            if (shown < 6 && i > -r && i < r) {
                shown += split_cell(ctx, res, cx - r, cz + i, ncand);
                if (shown < 6) shown += split_cell(ctx, res, cx + r, cz + i, ncand);
            }
        }
    }
    if (shown == 0) printf("  none within 2000 chunks of the data\n");
}

/* Marks a scrambler result that equals the vanilla default. */
static const char *default_note(const Ctx *ctx, const uint64_t v) {
    return ctx->mode == MODE_SALT && v == DEFAULT_SCRAMBLER ? "  = default 987234911" : "";
}

/* Sorts and prints the results; in seed mode with several candidates also suggests chunks to observe next. */
void render_report(const Ctx *ctx, const Chunks *ch, Results *res) {
    const char *what = ctx->mode == MODE_SEED ? "seed (low 48 bits)" : "scrambler (low 48 bits)";
    if (res->n == 0) { printf("no solutions\n"); return; }
    qsort(res->v, (size_t)res->n, sizeof res->v[0], cmp_u64);
    if (res->n == 1) {
        printf("definitive %s: %llu (0x%012llx)%s\n", what, (unsigned long long)res->v[0],
               (unsigned long long)res->v[0], default_note(ctx, res->v[0]));
        return;
    }
    printf("%d candidates, %s:\n", res->n, what);
    for (int k = 0; k < res->n && k < 20; k++)
        printf("  #%d  %llu (0x%012llx)%s\n", k + 1, (unsigned long long)res->v[k], (unsigned long long)res->v[k],
               default_note(ctx, res->v[k]));
    if (res->n > 20) printf("  ... and %d more\n", res->n - 20);
    if (ctx->mode == MODE_SEED) {
        printf("smallest: %llu\n", (unsigned long long)res->v[0]);
        print_distinguishing_chunks(ctx, ch, res);
    }
}

/* Prints the command-line help. */
int render_usage(const char *prog) {
    fprintf(stderr,
            "usage (--scrambler defaults to 987234911):\n"
            "  %s seed  chunks.csv [--scrambler N] [--anchor N]   recover the world seed\n"
            "  %s salt  WORLDSEED chunks.csv [--anchor N]         recover the scrambler (spigot.yml seed-slime)\n"
            "  %s check SEED chunks.csv [--scrambler N]           does SEED reproduce the file?\n",
            prog, prog, prog);
    return 1;
}

/* Prints one chunk where the file and the seed disagree (seed_slime = what the seed says). */
void render_check_mismatch(const int64_t x, const int64_t z, const int seed_slime) {
    printf("  chunk %lld,%lld: file says %s, seed says %s\n", (long long)x, (long long)z,
           seed_slime ? "not slime" : "slime", seed_slime ? "slime" : "not slime");
}

/* Prints the MATCH / MISMATCH verdict of a seed check. */
void render_check_summary(const uint64_t seed, const long long bad, const long long total) {
    if (bad) printf("MISMATCH: %lld of %lld chunks differ\n", bad, total);
    else printf("MATCH: seed %llu reproduces all %lld chunks\n", (unsigned long long)seed, total);
}
