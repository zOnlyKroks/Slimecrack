#ifndef SLIME_H
#define SLIME_H

#include <stdint.h>

#define MASK48 ((1ULL << 48) - 1)
#define MUL    0x5DEECE66DULL
#define ADDC   0xBULL
#define DEFAULT_SCRAMBLER 987234911ULL

/* The first LCG state of chunk (x,z) is (worldSeed + offs(x,z)) ^ K, with K = scrambler ^ MUL.
   MODE_SEED: scrambler known, world seed unknown.  MODE_SALT: world seed known, scrambler unknown. */
typedef enum { MODE_SEED, MODE_SALT } Mode;

typedef struct {
    Mode mode;
    uint64_t K;            /* (scrambler ^ MUL) & MASK48; used in seed mode */
    uint64_t worldSeed;    /* used in salt mode */
} Ctx;

/* Seed-independent chunk offset of vanilla Minecraft's slime-chunk formula. */
uint64_t offs(int64_t x, int64_t z);

/* Runs the exact nextInt(10) == 0 test (including the rejection loop) from a first LCG state. */
int slime_from_state(uint64_t st);

/* Whether the chunk with offset c is a slime chunk for world seed W under the context's scrambler. */
int is_slime(const Ctx *ctx, uint64_t W, uint64_t c);

/* Inverse of a modulo 2^48 (a must be odd). */
uint64_t inv48(uint64_t a);

/* Division-free test of "i % 10 == 0" (or i in the rejected range), as the loop would accept it. */
int pass_div10(uint32_t i);

/* First state of a chunk, given the anchor chunk's first state S and the chunk's offset d from the anchor. */
static uint64_t derive(const Ctx *ctx, const uint64_t S, const uint64_t d) {
    return ctx->mode == MODE_SEED ? ((S ^ ctx->K) + d & MASK48) ^ ctx->K : S ^ d;
}

#endif
