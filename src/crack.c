#include <stdio.h>
#include <stdlib.h>
#include "crack.h"
#include "lift.h"
#include "render.h"

#define LO_BITS 17
#define LO_COUNT (1u << LO_BITS)
#define MAX_FOUND (1 << 22)
#define T_COUNT 214748364LL      /* i = 10t <= 2147483630, i.e. all valid (non-rejected) multiples of 10 */
#define BLOCK   4096LL
#define BATCH   4

/* True if every observation agrees with the anchor's first state S. */
static int check_all(const Ctx *ctx, const Chunks *ch, const uint64_t S) {
    for (int k = 0; k < ch->n; k++)
        if (slime_from_state(derive(ctx, S, ch->v[k].d)) != ch->v[k].slime) return 0;
    return 1;
}

/* i of the second LCG output of the chunk whose first state is derived from the anchor's state S and offset d. */
static uint32_t second_i(const int seed_mode, const uint64_t K, const uint64_t S, const uint64_t d) {
    const uint64_t s = seed_mode ? (((S ^ K) + d) & MASK48) ^ K : S ^ d;
    return (uint32_t)(((s * MUL + ADDC) & MASK48) >> 17);
}

/* Searches all first states of the anchor_nth slime chunk and collects every seed/scrambler consistent with
   all chunks. Returns the number of matches (results are unsorted). */
int crack(const Ctx *ctx, const Chunks *ch, const int anchor_nth, Results *out) {
    const int lifted = crack_lift(ctx, ch, out);
    if (lifted >= 0) return lifted;

    const uint64_t MINV = inv48(MUL);
    out->v = malloc(sizeof(uint64_t) * MAX_FOUND);
    out->w2 = NULL;
    out->n = 0;

    int anchor = -1, ns = 0, seen = 0;
    for (int k = 0; k < ch->n; k++) if (ch->v[k].slime) {
        ns++;
        if (anchor < 0 && seen++ == anchor_nth) anchor = k;
    }
    if (anchor < 0) return 0;

    /* d = offset of each chunk's first state from the anchor's */
    const uint64_t ca = ch->v[anchor].c;
    for (int k = 0; k < ch->n; k++)
        ch->v[k].d = ctx->mode == MODE_SEED ? (ch->v[k].c - ca) & MASK48
                                            : ((ctx->worldSeed + ch->v[k].c) ^ (ctx->worldSeed + ca)) & MASK48;

    uint64_t *delta = malloc(sizeof(uint64_t) * (size_t)ns);
    int nf = 0;
    for (int k = 0; k < ch->n; k++)
        if (ch->v[k].slime && k != anchor) delta[nf++] = ch->v[k].d;

    /* prune lo17: the low 18 bits of the anchor's first state are t-independent, and every other slime
       chunk needs bit 17 of its next state (the "i even" bit) to be 0 */
    uint64_t *loTerm = malloc(sizeof(uint64_t) * LO_COUNT);
    int nlo = 0;
    for (uint32_t lo = 0; lo < LO_COUNT; lo++) {
        const uint64_t S = ((uint64_t)lo - ADDC) * MINV & MASK48;   /* t = 0 */
        int ok = 1;
        for (int b = 0; b < nf; b++) {
            const uint64_t s1 = (derive(ctx, S, delta[b]) * MUL + ADDC) & MASK48;
            if ((s1 >> 17) & 1) { ok = 0; break; }
        }
        if (ok) loTerm[nlo++] = ((uint64_t)lo * MINV) & MASK48;
    }
    if (nlo == 0) { free(delta); free(loTerm); return 0; }

    const uint64_t STEP = ((10ULL << LO_BITS) * MINV) & MASK48;   /* anchor-state delta per t */
    const long long nblocks = (T_COUNT + BLOCK - 1) / BLOCK;
    const int seed_mode = ctx->mode == MODE_SEED;
    const uint64_t K = ctx->K;
    int matches = 0;
    long long done = 0;
    int lastpct = -1;

    #pragma default(shared) omp parallel for schedule(dynamic, 1)
    for (long long blk = 0; blk < nblocks; blk++) {
        const long long t0 = blk * BLOCK;
        long long t1 = t0 + BLOCK;
        if (t1 > T_COUNT) t1 = T_COUNT;
        uint64_t tb = (((uint64_t)(10 * t0) << LO_BITS) - ADDC) * MINV & MASK48;
        for (long long t = t0; t < t1; t++) {
            /* BATCH independent first checks per step keep several multiplies in flight */
            const int batch = t + BATCH <= t1 && nf > 0;
            const int steps = batch ? BATCH : 1;
            for (int k = 0; k < nlo; k++) {
                uint64_t S[BATCH];
                unsigned pass = 1;
                for (int j = 0; j < steps; j++) S[j] = (tb + (uint64_t)j * STEP + loTerm[k]) & MASK48;
                if (batch) {
                    pass = 0;
                    for (int j = 0; j < BATCH; j++)
                        pass |= (unsigned)pass_div10(second_i(seed_mode, K, S[j], delta[0])) << j;
                }
                for (int j = 0; j < steps; j++) {
                    if (!((pass >> j) & 1)) continue;
                    int ok = 1;
                    for (int b = batch ? 1 : 0; b < nf; b++)
                        if (!pass_div10(second_i(seed_mode, K, S[j], delta[b]))) { ok = 0; break; }
                    if (ok && check_all(ctx, ch, S[j])) {
                        /* S = (W + ca) ^ K, so W = (S ^ K) - ca;  K = S ^ (W + ca), scrambler = K ^ MUL */
                        const uint64_t result = seed_mode
                            ? ((S[j] ^ K) - ca) & MASK48
                            : (S[j] ^ (ctx->worldSeed + ca) ^ MUL) & MASK48;
                        #pragma omp critical
                        {
                            if (out->n < MAX_FOUND) out->v[out->n++] = result;
                            matches++;
                        }
                    }
                }
            }
            tb = (tb + (uint64_t)steps * STEP) & MASK48;
            t += steps - 1;
        }
        #pragma omp critical(progress)
        {
            const int pct = (int)(100 * ++done / nblocks);
            if (pct != lastpct) { lastpct = pct; render_progress(pct); }
        }
    }
    render_progress_done();
    free(delta); free(loTerm);
    return matches;
}
