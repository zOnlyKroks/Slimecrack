#ifndef PAIRS_H
#define PAIRS_H

#include <stdint.h>
#include "chunks.h"
#include "slime.h"

#define LEVEL0 18         /* bits known after the bit-17 stage */
#define MAX_PAIRS 256

typedef struct { uint64_t ca; int64_t ds; uint64_t absds; int kmin; } Pair;   /* c_b = c_a + ds (mod 2^48) */

typedef struct {
    Pair p[MAX_PAIRS];   /* ascending |ds| */
    int n;
    int upto[49];        /* pairs usable once L bits are known (kmin <= L) */
} PairSet;

/* Picks the slime-chunk pairs with the smallest offset difference into ps and returns the slime offsets mod 2^18
   (caller frees); *ns_out is the number of slime chunks. */
uint32_t *pairs_build(const Chunks *ch, PairSet *ps, int *ns_out);

/* Pair test with L bits of W and K known: is slime chunk b = a + ds consistent with a being slime?
   Passes when it cannot be decided yet (a carry leaves the known bits). Bits of W / K above L are ignored for K
   and masked for W + c, so W may be fully known. */
static int pair_ok(const uint64_t W, const uint64_t K, const int L, const Pair *p) {
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
    const uint64_t Kl = K & mask;
    const uint64_t sa = va ^ Kl, sb = vb ^ Kl;
    const int64_t delta = (int64_t)sb - (int64_t)sa;
    const uint64_t ra = (sa * MUL + ADDC) & 0x1FFFFULL;
    const uint64_t D = ((uint64_t)delta * MUL) & MASK48;
    const uint32_t y = (uint32_t)(((ra + D) >> 17) & 0x7FFFFFFFULL);
    const uint32_t m = y % 10;
    return m == 0 || m == 8;
}

/* All pair tests that apply once L bits are known (newest pairs first, they reject most). */
static int pairs_ok(const PairSet *ps, const uint64_t W, const uint64_t K, const int L) {
    for (int j = ps->upto[L] - 1; j >= 0; j--)
        if (!pair_ok(W, K, L, &ps->p[j])) return 0;
    return 1;
}

#endif
