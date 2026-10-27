#include <stdio.h>
#include <stdlib.h>
#include "chunks.h"
#include "slime.h"

/* Allocates an empty chunk list. */
Chunks *chunks_new(void) {
    Chunks *ch = malloc(sizeof *ch);
    ch->v = malloc(sizeof(Obs) * MAX_OBS);
    ch->n = 0;
    return ch;
}

/* Frees a chunk list. */
void chunks_free(Chunks *ch) {
    free(ch->v);
    free(ch);
}

/* Appends one observation (ignored once MAX_OBS is reached). */
void chunks_add(Chunks *ch, const int64_t x, const int64_t z, const int slime) {
    if (ch->n >= MAX_OBS) return;
    Obs *o = &ch->v[ch->n++];
    o->x = x; o->z = z; o->slime = slime; o->c = offs(x, z); o->d = 0;
}

/* Reads "x,z[,slime]" lines (CSV or whitespace separated, '#' comments, a missing flag means slime). */
int chunks_load(Chunks *ch, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("open"); return 0; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        long long x, z; int s = 1;
        if (line[0] == '#') continue;
        for (char *p = line; *p; p++) if (*p == ',' || *p == ';') *p = ' ';   /* header lines fail sscanf and skip */
        if (sscanf(line, "%lld %lld %d", &x, &z, &s) >= 2) chunks_add(ch, x, z, s != 0);
    }
    fclose(f);
    return 1;
}

/* Returns a w*h grid marking the listed chunks of their bounding box (caller frees),
   or NULL if the box has more than limit cells. */
char *chunks_grid(const Chunks *ch, const int64_t limit, int64_t *x0, int64_t *z0, int64_t *w, int64_t *h) {
    int64_t x1, z1;
    *x0 = x1 = ch->v[0].x; *z0 = z1 = ch->v[0].z;
    for (int k = 1; k < ch->n; k++) {
        if (ch->v[k].x < *x0) *x0 = ch->v[k].x;
        if (ch->v[k].x > x1) x1 = ch->v[k].x;
        if (ch->v[k].z < *z0) *z0 = ch->v[k].z;
        if (ch->v[k].z > z1) z1 = ch->v[k].z;
    }
    *w = x1 - *x0 + 1; *h = z1 - *z0 + 1;
    if (*w * *h > limit) return NULL;
    char *listed = calloc((size_t)(*w * *h), 1);
    for (int k = 0; k < ch->n; k++) listed[(ch->v[k].x - *x0) * *h + (ch->v[k].z - *z0)] = 1;
    return listed;
}

/* The list holds every slime chunk of its bounding box, so adds the unlisted ones there as non-slime.
   Scattered chunks spanning a huge box are left as they are. */
void chunks_fill_non_slime(Chunks *ch) {
    int64_t x0, z0, w, h;
    char *listed = chunks_grid(ch, MAX_OBS, &x0, &z0, &w, &h);
    if (!listed) return;
    for (int64_t x = x0; x < x0 + w; x++)
        for (int64_t z = z0; z < z0 + h; z++)
            if (!listed[(x - x0) * h + (z - z0)]) chunks_add(ch, x, z, 0);
    free(listed);
}
