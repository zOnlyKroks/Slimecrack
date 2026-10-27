#ifndef CHUNKS_H
#define CHUNKS_H

#include <stdint.h>

#define MAX_OBS (1 << 20)

/* One observed chunk. c = offs(x,z); d = offset of its first state from the anchor's (set by crack). */
typedef struct { int64_t x, z; int slime; uint64_t c; uint64_t d; } Obs;

typedef struct {
    Obs *v;
    int n;
} Chunks;

/* Allocates an empty chunk list. */
Chunks *chunks_new(void);

/* Frees a chunk list. */
void chunks_free(Chunks *ch);

/* Appends one observation (ignored once MAX_OBS is reached). */
void chunks_add(Chunks *ch, int64_t x, int64_t z, int slime);

/* Reads "x,z[,slime]" lines (CSV or whitespace separated, '#' comments, a missing flag means slime). */
int chunks_load(Chunks *ch, const char *path);

/* Returns a w*h grid marking the listed chunks of their bounding box (caller frees),
   or NULL if the box has more than limit cells. */
char *chunks_grid(const Chunks *ch, int64_t limit, int64_t *x0, int64_t *z0, int64_t *w, int64_t *h);

/* The list holds every slime chunk of its bounding box, so adds the unlisted ones there as non-slime.
   Scattered chunks spanning a huge box are left as they are. */
void chunks_fill_non_slime(Chunks *ch);

#endif
