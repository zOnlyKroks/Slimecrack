#include <stdio.h>
#include <stdlib.h>
#include "both.h"
#include "render.h"

#define LEVEL0 18                    /* bits known after the bit-17 stage */
#define WINDOW 8                     /* neighbours in sorted offset order paired with each slime chunk */
#define MAX_PAIRS 256
#define FRONTIER_LIMIT (1u << 20)    /* stop the breadth-first lifting once this many candidates are alive */
#define SURVIVOR_LIMIT (1u << 24)
#define MAX_BOTH 4096

typedef struct { uint64_t ca; int64_t ds; uint64_t absds; int kmin; } Pair;   /* c_b = c_a + ds (mod 2^48) */
typedef struct { uint64_t W, K; } Node;                                       /* low bits of seed and K known */

static Pair pairs[MAX_PAIRS];
static int npairs;
static int npairs_upto[49];          /* pairs usable once L bits are known (kmin <= L) */
static uint64_t bit17[(1u << LEVEL0) / 64];
static const Chunks *chunks;
static Results *results;

/* Number of bits needed to represent v. */
static int bitlen(uint64_t v) {
    int n = 0;
    while (v) { n++; v >>= 1; }
    return n;
}

/* Orders pairs by ascending |ds|. */
static int cmp_pair(const void *a, const void *b) {
    const uint64_t x = ((const Pair *)a)->absds, y = ((const Pair *)b)->absds;
    return x < y ? -1 : x > y;
}

static int cmp_u64(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* Picks the slime-chunk pairs with the smallest offset difference; returns the list of slime offsets mod 2^18.
   The closest pair of a set is always adjacent in sorted order, so only neighbours (cyclic mod 2^48) are paired. */
static uint32_t *build_pairs(const Chunks *ch, int *ns_out) {
    int ns = 0;
    uint32_t *c18 = malloc(sizeof(uint32_t) * (size_t)ch->n);
    uint64_t *sorted = malloc(sizeof(uint64_t) * (size_t)ch->n);
    for (int k = 0; k < ch->n; k++) if (ch->v[k].slime) {
        c18[ns] = (uint32_t)(ch->v[k].c & ((1u << LEVEL0) - 1));
        sorted[ns++] = ch->v[k].c & MASK48;
    }
    *ns_out = ns;
    qsort(sorted, (size_t)ns, sizeof(uint64_t), cmp_u64);

    Pair *all = malloc(sizeof(Pair) * ((size_t)ns * WINDOW + 1));
    size_t na = 0;
    for (int i = 0; i < ns; i++)
        for (int w = 1; w <= WINDOW && w < ns; w++) {
            const uint64_t diff = (sorted[(i + w) % ns] - sorted[i]) & MASK48;
            if (diff == 0) continue;
            const int64_t ds = diff >= (1ULL << 47) ? (int64_t)diff - (int64_t)(1ULL << 48) : (int64_t)diff;
            Pair *p = &all[na++];
            p->ca = sorted[i]; p->ds = ds; p->absds = ds < 0 ? (uint64_t)-ds : (uint64_t)ds;
            p->kmin = bitlen(p->absds);
            if (p->kmin < LEVEL0) p->kmin = LEVEL0;
        }
    qsort(all, na, sizeof(Pair), cmp_pair);
    for (size_t k = 0; k < na && npairs < MAX_PAIRS && all[k].kmin <= 48; k++) pairs[npairs++] = all[k];
    for (int L = 0; L <= 48; L++) {
        npairs_upto[L] = 0;
        while (npairs_upto[L] < npairs && pairs[npairs_upto[L]].kmin <= L) npairs_upto[L]++;
    }
    free(all); free(sorted);
    return c18;
}

/* Fills the table of bit 17 of (u * MUL + ADDC) mod 2^18 for every 18-bit u. */
static void build_bit17_table(void) {
    for (uint32_t u = 0; u < (1u << LEVEL0); u++)
        if ((((uint64_t)u * MUL + ADDC) >> 17) & 1) bit17[u >> 6] |= 1ULL << (u & 63);
}

/* Pair test with L bits of W and K known: is slime chunk b = a + ds consistent with a being slime?
   Passes when it cannot be decided yet (a carry leaves the known bits). */
static inline int pair_ok(const uint64_t W, const uint64_t K, const int L, const Pair *p) {
    const uint64_t mask = (1ULL << L) - 1;
    const uint64_t va = (W + p->ca) & mask;
    const int64_t t = (int64_t)va + p->ds;
    uint64_t vb;
    if (L < 48) {
        if (t < 0 || t > (int64_t)mask) return 1;
        vb = (uint64_t)t;
    } else {
        vb = (uint64_t)t & MASK48;
    }
    const uint64_t sa = va ^ K, sb = vb ^ K;
    const int64_t delta = (int64_t)sb - (int64_t)sa;
    const uint64_t ra = (sa * MUL + ADDC) & 0x1FFFFULL;
    const uint64_t D = ((uint64_t)delta * MUL) & MASK48;
    const uint32_t y = (uint32_t)(((ra + D) >> 17) & 0x7FFFFFFFULL);
    const uint32_t m = y % 10;
    return m == 0 || m == 8;
}

/* All pair tests that apply once L bits are known (newest pairs first, they reject most). */
static inline int pairs_ok(const uint64_t W, const uint64_t K, const int L) {
    for (int j = npairs_upto[L] - 1; j >= 0; j--)
        if (!pair_ok(W, K, L, &pairs[j])) return 0;
    return 1;
}

/* Exact slime test of every chunk for the full 48-bit candidate. */
static int exact(const uint64_t W, const uint64_t K) {
    for (int k = 0; k < chunks->n; k++)
        if (slime_from_state(((W + chunks->v[k].c) ^ K) & MASK48) != chunks->v[k].slime) return 0;
    return 1;
}

/* Stores a verified candidate (scrambler = K ^ MUL). */
static void record(const uint64_t W, const uint64_t K) {
    #pragma omp critical(record)
    {
        if (results->n < MAX_BOTH) {
            results->v[results->n] = W;
            results->w2[results->n] = (K ^ MUL) & MASK48;
            results->n++;
        }
    }
}

/* Depth-first lifting from k known bits to 48; bit 47 of the seed is fixed to 0 (the flip symmetry). */
static void dfs(const uint64_t W, const uint64_t K, const int k) {
    if (k == 48) {
        if (exact(W, K)) record(W, K);
        return;
    }
    const int L = k + 1;
    for (int w = 0; w <= (k == 47 ? 0 : 1); w++)
        for (int kb = 0; kb <= 1; kb++) {
            const uint64_t W2 = W | ((uint64_t)w << k), K2 = K | ((uint64_t)kb << k);
            if (pairs_ok(W2, K2, L)) dfs(W2, K2, L);
        }
}

/* Recovers world seed and scrambler together by lifting both bit by bit from the lowest bit, pruning with
   pairs of slime chunks whose offsets are close (see docs/approach-both.md).
   out->v = seeds, out->w2 = scramblers (low 48 bits, seed bit 47 normalised to 0). Returns the number found. */
int crack_both(const Chunks *ch, Results *out) {
    chunks = ch;
    results = out;
    out->v = malloc(sizeof(uint64_t) * MAX_BOTH);
    out->w2 = malloc(sizeof(uint64_t) * MAX_BOTH);
    out->n = 0;

    int ns;
    uint32_t *c18 = build_pairs(ch, &ns);
    if (ns < 2 || npairs == 0) {
        render_message("need at least two slime chunks with usable offsets\n");
        free(c18);
        return 0;
    }
    render_message("%d slime chunks, %d pairs; closest pair needs %d bits (search ~2^%d nodes per candidate)\n",
                   ns, npairs, pairs[0].kmin, 2 * (pairs[0].kmin - LEVEL0));
    if (ns < 30) render_message("warning: under 30 slime chunks leaves many candidates after the bit-17 stage\n");

    /* stage 1: every (W, K) of 18 bits where each slime chunk has bit 17 of its next state equal to 0 */
    build_bit17_table();
    const uint32_t N = 1u << LEVEL0, M = N - 1;
    Node *cur = malloc(sizeof(Node) * 1024);
    size_t n = 0, cap = 1024;
    int overflow = 0, done = 0, lastpct = -1;
    #pragma omp parallel for schedule(dynamic, 16)
    for (long long W = 0; W < N; W++) {
        for (uint32_t K = 0; K < N; K++) {
            int ok = 1;
            for (int b = 0; b < ns; b++) {
                const uint32_t u = (((uint32_t)W + c18[b]) & M) ^ K;
                if ((bit17[u >> 6] >> (u & 63)) & 1) { ok = 0; break; }
            }
            if (!ok) continue;
            #pragma omp critical(survivors)
            {
                if (n >= SURVIVOR_LIMIT) overflow = 1;
                else {
                    if (n == cap) { cap *= 2; cur = realloc(cur, sizeof(Node) * cap); }
                    cur[n].W = (uint64_t)W; cur[n].K = K; n++;
                }
            }
        }
        #pragma omp critical(progress)
        {
            const int pct = (int)(100LL * ++done / N);
            if (pct != lastpct) { lastpct = pct; render_progress(pct); }
        }
    }
    render_progress_done();
    free(c18);
    if (overflow) {
        render_message("too many candidates after the bit-17 stage: provide more slime chunks\n");
        free(cur);
        return 0;
    }
    render_message("%zu candidates after the bit-17 stage\n", n);

    /* stage 2: breadth-first lifting while the candidate set stays small */
    int level = LEVEL0;
    while (level < 48 && n > 0 && n <= FRONTIER_LIMIT) {
        const int L = level + 1;
        Node *next = malloc(sizeof(Node) * n * 4);
        size_t m = 0;
        for (size_t i = 0; i < n; i++)
            for (int w = 0; w <= (level == 47 ? 0 : 1); w++)
                for (int kb = 0; kb <= 1; kb++) {
                    const uint64_t W2 = cur[i].W | ((uint64_t)w << level), K2 = cur[i].K | ((uint64_t)kb << level);
                    if (pairs_ok(W2, K2, L)) { next[m].W = W2; next[m].K = K2; m++; }
                }
        free(cur);
        cur = next; n = m; level = L;
    }

    /* stage 3: depth-first from the frontier, in parallel */
    if (level == 48) {
        for (size_t i = 0; i < n; i++) if (exact(cur[i].W, cur[i].K)) record(cur[i].W, cur[i].K);
    } else if (n > 0) {
        render_message("lifting %zu candidates from bit %d\n", n, level);
        long long fin = 0;
        lastpct = -1;
        #pragma omp parallel for schedule(dynamic, 1)
        for (long long i = 0; i < (long long)n; i++) {
            dfs(cur[i].W, cur[i].K, level);
            #pragma omp critical(progress)
            {
                const int pct = (int)(100LL * ++fin / (long long)n);
                if (pct != lastpct) { lastpct = pct; render_progress(pct); }
            }
        }
        render_progress_done();
    }
    free(cur);
    return out->n;
}
