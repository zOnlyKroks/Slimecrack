#include "slime.h"

/* Vanilla Minecraft: (int)(x*x*0x4c1906) + (int)(x*0x5ac0db) + (int)(z*z)*0x4307a7L + (int)(z*0x5f24f). */
uint64_t offs(const int64_t x, const int64_t z) {
    const uint32_t xu = (uint32_t)x, zu = (uint32_t)z;
    const int32_t t1 = (int32_t)(xu * xu * 4987142u);
    const int32_t t2 = (int32_t)(xu * 5947611u);
    const int32_t zz = (int32_t)(zu * zu);
    const int32_t t4 = (int32_t)(zu * 389711u);
    return (uint64_t)((int64_t)t1 + t2 + (int64_t)zz * 4392871LL + t4);
}

/* Runs the exact nextInt(10) == 0 test (including the rejection loop) from a first LCG state. */
int slime_from_state(uint64_t st) {
    int32_t i, j;
    do {
        st = (st * MUL + ADDC) & MASK48;
        i = (int32_t)(st >> 17);
        j = i % 10;
    } while ((int32_t)((uint32_t)i - (uint32_t)j + 9u) < 0);
    return j == 0;
}

/* Whether the chunk with offset c is a slime chunk for world seed W under the context's scrambler. */
int is_slime(const Ctx *ctx, const uint64_t W, const uint64_t c) {
    return slime_from_state(((W + c) ^ ctx->K) & MASK48);
}

/* Inverse of a modulo 2^48 via Newton iteration (a must be odd). */
uint64_t inv48(const uint64_t a) {
    uint64_t x = a;
    for (int i = 0; i < 6; i++) x = (x * (2 - a * x)) & MASK48;
    return x;
}

/* Division-free test of "i % 10 == 0" (or i in the rejected range), as the loop would accept it. */
int pass_div10(const uint32_t i) {
    uint32_t r = i * 0xCCCCCCCDu;
    r = (r >> 1) | (r << 31);
    return (r <= 0x19999999u) | (i >= 2147483640u);
}
