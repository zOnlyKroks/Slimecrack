#include <stdlib.h>
#include "pairs.h"

#define WINDOW 8   /* neighbours in sorted offset order paired with each slime chunk */

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

/* The closest pair of a set is always adjacent in sorted order, so only neighbours (cyclic mod 2^48) are paired. */
uint32_t *pairs_build(const Chunks *ch, PairSet *ps, int *ns_out) {
    int ns = 0;
    uint32_t *c18 = malloc(sizeof(uint32_t) * (size_t)ch->n + 1);
    uint64_t *sorted = malloc(sizeof(uint64_t) * (size_t)ch->n + 1);
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
    ps->n = 0;
    for (size_t k = 0; k < na && ps->n < MAX_PAIRS && all[k].kmin <= 48; k++) ps->p[ps->n++] = all[k];
    for (int L = 0; L <= 48; L++) {
        ps->upto[L] = 0;
        while (ps->upto[L] < ps->n && ps->p[ps->upto[L]].kmin <= L) ps->upto[L]++;
    }
    free(all); free(sorted);
    return c18;
}
