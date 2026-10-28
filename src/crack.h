#ifndef CRACK_H
#define CRACK_H

#include <stdint.h>
#include "chunks.h"
#include "slime.h"

typedef struct {
    uint64_t *v;    /* seeds (seed/both mode) or scramblers (salt mode), low 48 bits */
    uint64_t *w2;   /* scramblers belonging to v (both mode only, else NULL) */
    int n;
} Results;

/* Searches all first states of the anchor_nth slime chunk and collects every seed/scrambler consistent with
   all chunks. Returns the number of matches (results are unsorted). */
int crack(const Ctx *ctx, const Chunks *ch, int anchor_nth, Results *out);

#endif
