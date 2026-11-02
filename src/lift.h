#ifndef LIFT_H
#define LIFT_H

#include "chunks.h"
#include "crack.h"
#include "slime.h"

/* seed / salt mode by lifting the unknown 48-bit value bit by bit from bit 18 upwards, pruning with pairs of slime
   chunks whose offsets are close (see docs/approach-lift.md). Fills out and returns the number of matches,
   or -1 (out untouched) if the chunks have no close enough pair, in which case the caller falls back to crack(). */
int crack_lift(const Ctx *ctx, const Chunks *ch, Results *out);

#endif
