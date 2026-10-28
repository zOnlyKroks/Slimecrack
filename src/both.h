#ifndef BOTH_H
#define BOTH_H

#include "chunks.h"
#include "crack.h"
#include "slime.h"

/* Recovers world seed and scrambler together by lifting both bit by bit from the lowest bit, pruning with
   pairs of slime chunks whose offsets are close (see docs/approach-both.md).
   out->v = seeds, out->w2 = scramblers (low 48 bits, seed bit 47 normalised to 0). Returns the number found. */
int crack_both(const Chunks *ch, Results *out);

#endif
