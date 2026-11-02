#include <stdlib.h>
#include "lift.h"
#include "pairs.h"
#include "render.h"

#ifndef LIFT_MAX_KMIN
#define LIFT_MAX_KMIN 40         /* beyond this the plain enumeration of crack() is cheaper */
#endif
#define BFS_TARGET 4096          /* expand breadth-first until this many candidates, then go depth-first in parallel */
#define MAX_FOUND (1 << 22)

/* The unknown x is the world seed (seed mode, scrambler fixed in ctx->K) or K = scrambler ^ MUL (salt mode,
   world seed fixed in ctx->worldSeed). Only its low bits are meaningful while lifting. */
typedef struct {
    const Ctx *ctx;
    const Chunks *ch;
    PairSet ps;
    Results *out;
    int matches;
} Lift;

static int accept(const Lift *l, const uint64_t x, const int L) {
    return l->ctx->mode == MODE_SEED ? pairs_ok(&l->ps, x, l->ctx->K, L)
                                     : pairs_ok(&l->ps, l->ctx->worldSeed, x, L);
}

/* Exact slime test of every chunk for the full 48-bit candidate x. */
static int exact(const Lift *l, const uint64_t x) {
    const int seed = l->ctx->mode == MODE_SEED;
    const uint64_t W = seed ? x : l->ctx->worldSeed, K = seed ? l->ctx->K : x;
    for (int k = 0; k < l->ch->n; k++)
        if (slime_from_state(((W + l->ch->v[k].c) ^ K) & MASK48) != l->ch->v[k].slime) return 0;
    return 1;
}

static void record(Lift *l, const uint64_t x) {
    #pragma omp critical(lift_record)
    {
        if (l->out->n < MAX_FOUND) l->out->v[l->out->n++] = l->ctx->mode == MODE_SEED ? x : (x ^ MUL) & MASK48;
        l->matches++;
    }
}

/* Depth-first lifting from k known bits to 48. */
static void dfs(Lift *l, const uint64_t x, const int k) {
    if (k == 48) {
        if (exact(l, x)) record(l, x);
        return;
    }
    for (int b = 0; b <= 1; b++) {
        const uint64_t x2 = x | ((uint64_t)b << k);
        if (accept(l, x2, k + 1)) dfs(l, x2, k + 1);
    }
}

int crack_lift(const Ctx *ctx, const Chunks *ch, Results *out) {
    Lift l = { .ctx = ctx, .ch = ch, .out = out, .matches = 0 };
    int ns;
    uint32_t *c18 = pairs_build(ch, &l.ps, &ns);
    if (ns < 2 || l.ps.n == 0 || l.ps.p[0].kmin > LIFT_MAX_KMIN) { free(c18); return -1; }

    out->v = malloc(sizeof(uint64_t) * MAX_FOUND);
    out->w2 = NULL;
    out->n = 0;

    /* every slime chunk needs bit 17 of its next state (the "i even" bit) to be 0, which depends on 18 bits only */
    const uint32_t N = 1u << LEVEL0, M = N - 1;
    const int seed = ctx->mode == MODE_SEED;
    const uint32_t fixed18 = (uint32_t)((seed ? ctx->K : ctx->worldSeed) & M);
    size_t n = 0, cap = 1024;
    uint64_t *cur = malloc(sizeof(uint64_t) * cap);
    for (uint32_t x = 0; x < N; x++) {
        int ok = 1;
        for (int b = 0; b < ns; b++) {
            const uint32_t u = seed ? ((x + c18[b]) & M) ^ fixed18 : ((fixed18 + c18[b]) & M) ^ x;
            if ((((uint64_t)u * MUL + ADDC) >> 17) & 1) { ok = 0; break; }
        }
        if (!ok) continue;
        if (n == cap) { cap *= 2; cur = realloc(cur, sizeof(uint64_t) * cap); }
        cur[n++] = x;
    }
    free(c18);

    int level = LEVEL0;
    while (level < 48 && n > 0 && n < BFS_TARGET) {
        uint64_t *next = malloc(sizeof(uint64_t) * n * 2);
        size_t m = 0;
        for (size_t i = 0; i < n; i++)
            for (int b = 0; b <= 1; b++) {
                const uint64_t x2 = cur[i] | ((uint64_t)b << level);
                if (accept(&l, x2, level + 1)) next[m++] = x2;
            }
        free(cur);
        cur = next; n = m; level++;
    }

    if (level == 48) {
        for (size_t i = 0; i < n; i++) if (exact(&l, cur[i])) record(&l, cur[i]);
    } else if (n > 0) {
        long long fin = 0;
        int lastpct = -1;
        #pragma omp parallel for schedule(dynamic, 1)
        for (long long i = 0; i < (long long)n; i++) {
            dfs(&l, cur[i], level);
            #pragma omp critical(lift_progress)
            {
                const int pct = (int)(100LL * ++fin / (long long)n);
                if (pct != lastpct) { lastpct = pct; render_progress(pct); }
            }
        }
        render_progress_done();
    }
    free(cur);
    return l.matches;
}
