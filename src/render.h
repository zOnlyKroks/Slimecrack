#ifndef RENDER_H
#define RENDER_H

#include "chunks.h"
#include "crack.h"
#include "slime.h"

/* Draws the stderr progress bar for pct percent. */
void render_progress(int pct);

/* Redirects progress reports to sink instead of the stderr bar (the GUI reads the percentage itself). */
void render_set_progress_sink(void (*sink)(int pct));

/* Prints a printf-style status line to stderr. */
void render_message(const char *fmt, ...);

/* Ends the progress bar line. */
void render_progress_done(void);

/* Sorts and prints the results; in seed mode with several candidates also suggests chunks to observe next. */
void render_report(const Ctx *ctx, const Chunks *ch, Results *res);

#define SUGGEST_MAX 16     /* most chunks suggested */
#define SUGGEST_RADIUS 64  /* chunks searched around the centre of the data */
#define SUGGEST_CAND 64    /* candidates considered (the first ones of the result list) */

/* A chunk to observe next; bit k of slime is set if candidate k (result list order) makes it a slime chunk. */
typedef struct { int64_t x, z; uint64_t slime; } Suggestion;

/* Greedily picks up to max chunks around the data that together tell the candidates apart, each pick separating
   as many still-confusable candidate pairs as possible. Returns the number of picks; *left is the number of
   candidates that stay indistinguishable. */
int suggest_chunks(const Ctx *ctx, const Chunks *ch, const Results *res, Suggestion *out, int max, int *left);

/* Prints the command-line help. */
int render_usage(const char *prog);

/* Prints one chunk where the file and the seed disagree (seed_slime = what the seed says). */
void render_check_mismatch(int64_t x, int64_t z, int seed_slime);

/* Prints the MATCH / MISMATCH verdict of a seed check. */
void render_check_summary(uint64_t seed, long long bad, long long total);

#endif
