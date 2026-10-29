#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "render.h"

static void (*progress_sink)(int);

void render_set_progress_sink(void (*sink)(int)) { progress_sink = sink; }

/* Draws the stderr progress bar for pct percent. */
void render_progress(const int pct) {
    if (progress_sink) { progress_sink(pct); return; }
    const int filled = pct * 30 / 100;
    fprintf(stderr, "\r[%.*s%.*s] %3d%%", filled, "##############################",
            30 - filled, "------------------------------", pct);
    fflush(stderr);
}

/* Ends the progress bar line. */
void render_progress_done(void) {
    if (progress_sink) return;
    fprintf(stderr, "\n");
}

/* Comparison function for sorting uint64_t ascending. */
static int cmp_u64(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}


/* Whether candidate k of the result list makes the chunk with offset c a slime chunk. */
static int cand_slime(const Ctx *ctx, const Results *res, const int k, const uint64_t c) {
    uint64_t W, K;
    switch (ctx->mode) {
        case MODE_SEED: W = res->v[k]; K = ctx->K; break;
        case MODE_SALT: W = ctx->worldSeed; K = (res->v[k] ^ MUL) & MASK48; break;
        default: W = res->v[k]; K = (res->w2[k] ^ MUL) & MASK48; break;
    }
    return slime_from_state(((W + c) ^ K) & MASK48);
}

int suggest_chunks(const Ctx *ctx, const Chunks *ch, const Results *res, Suggestion *out, const int max, int *left) {
    int64_t cx = 0, cz = 0;
    for (int k = 0; k < ch->n; k++) { cx += ch->v[k].x; cz += ch->v[k].z; }
    cx /= ch->n; cz /= ch->n;
    const int n = res->n < SUGGEST_CAND ? res->n : SUGGEST_CAND;
    const int side = 2 * SUGGEST_RADIUS + 1;
    uint64_t *mask = malloc(sizeof(uint64_t) * (size_t)side * side);
    for (int i = 0; i < side; i++)
        for (int j = 0; j < side; j++) {
            const uint64_t c = offs(cx - SUGGEST_RADIUS + i, cz - SUGGEST_RADIUS + j);
            uint64_t m = 0;
            for (int k = 0; k < n; k++) m |= (uint64_t)cand_slime(ctx, res, k, c) << k;
            mask[i * side + j] = m;
        }

    int group[SUGGEST_CAND] = {0}, ngroups = 1, picks = 0;
    while (picks < max && ngroups < n) {
        int tot[SUGGEST_CAND] = {0};
        for (int k = 0; k < n; k++) tot[group[k]]++;
        long best = 0;
        int bi = -1, bd = 0;
        for (int i = 0; i < side; i++)
            for (int j = 0; j < side; j++) {
                int hit[SUGGEST_CAND] = {0};
                for (int k = 0; k < n; k++) hit[group[k]] += (int)((mask[i * side + j] >> k) & 1);
                long score = 0;
                for (int g = 0; g < ngroups; g++) score += (long)hit[g] * (tot[g] - hit[g]);
                const int dx = abs(i - SUGGEST_RADIUS), dz = abs(j - SUGGEST_RADIUS), d = dx > dz ? dx : dz;
                if (score > best || (score == best && score > 0 && d < bd)) { best = score; bi = i * side + j; bd = d; }
            }
        if (bi < 0) break;
        const uint64_t m = mask[bi];
        out[picks].x = cx - SUGGEST_RADIUS + bi / side;
        out[picks].z = cz - SUGGEST_RADIUS + bi % side;
        out[picks].slime = m;
        picks++;

        int next[SUGGEST_CAND], ids = 0;
        for (int k = 0; k < n; k++) {
            next[k] = -1;
            for (int j = 0; j < k && next[k] < 0; j++)
                if (group[j] == group[k] && ((m >> j) & 1) == ((m >> k) & 1)) next[k] = next[j];
            if (next[k] < 0) next[k] = ids++;
        }
        for (int k = 0; k < n; k++) group[k] = next[k];
        ngroups = ids;
    }
    free(mask);
    *left = n - ngroups;
    return picks;
}

/* Prints the suggested chunks with the candidates that are slime on each. */
static void print_distinguishing_chunks(const Ctx *ctx, const Chunks *ch, const Results *res) {
    Suggestion sug[SUGGEST_MAX];
    int left;
    const int picks = suggest_chunks(ctx, ch, res, sug, SUGGEST_MAX, &left);
    printf("distinguishing chunks (append to CSV as x,z,1 or x,z,0):\n");
    for (int p = 0; p < picks; p++) {
        printf("  chunk %lld,%lld is slime for #", (long long)sug[p].x, (long long)sug[p].z);
        int first = 1;
        for (int k = 0; k < SUGGEST_CAND && k < res->n; k++)
            if ((sug[p].slime >> k) & 1) { printf(first ? "%d" : ",%d", k + 1); first = 0; }
        printf(", not slime for the rest\n");
    }
    if (left) printf("  %d candidates stay indistinguishable within %d chunks of the data\n", left, SUGGEST_RADIUS);
}

/* Marks a scrambler result that equals the vanilla default. */
static const char *default_note(const Ctx *ctx, const uint64_t v) {
    return ctx->mode == MODE_SALT && v == DEFAULT_SCRAMBLER ? "  = default 987234911" : "";
}

/* Prints seed/scrambler pairs found by both mode (each is also valid with bit 47 of both flipped). */
static void report_both(const Ctx *ctx, const Chunks *ch, const Results *res) {
    if (res->n == 0) { printf("no solutions\n"); return; }
    printf(res->n == 1 ? "definitive (low 48 bits):\n" : "%d candidates (low 48 bits):\n", res->n);
    for (int k = 0; k < res->n && k < 20; k++)
        printf("  #%d  seed %llu (0x%012llx)  scrambler %llu (0x%012llx)%s\n", k + 1, (unsigned long long)res->v[k],
               (unsigned long long)res->v[k], (unsigned long long)res->w2[k], (unsigned long long)res->w2[k],
               res->w2[k] == DEFAULT_SCRAMBLER ? "  = default 987234911" : "");
    if (res->n > 20) printf("  ... and %d more\n", res->n - 20);
    printf("flipping bit 47 of both seed and scrambler gives the identical slime map\n");
    if (res->n > 1) print_distinguishing_chunks(ctx, ch, res);
}

/* Prints a status line to stderr. */
void render_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* Sorts and prints the results; in seed mode with several candidates also suggests chunks to observe next. */
void render_report(const Ctx *ctx, const Chunks *ch, Results *res) {
    if (ctx->mode == MODE_BOTH) { report_both(ctx, ch, res); return; }
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
    if (ctx->mode == MODE_SEED) printf("smallest: %llu\n", (unsigned long long)res->v[0]);
    print_distinguishing_chunks(ctx, ch, res);
}

/* Prints the command-line help. */
int render_usage(const char *prog) {
    fprintf(stderr,
            "usage (--scrambler defaults to 987234911):\n"
            "  %s seed  chunks.csv [--scrambler N] [--anchor N]   recover the world seed\n"
            "  %s salt  WORLDSEED chunks.csv [--anchor N]         recover the scrambler (spigot.yml seed-slime)\n"
            "  %s both  chunks.csv                                recover seed and scrambler (needs a dense block)\n"
            "  %s check SEED chunks.csv [--scrambler N]           does SEED reproduce the file?\n"
            "  %s gui                                             open the slime chunk map\n",
            prog, prog, prog, prog, prog);
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
