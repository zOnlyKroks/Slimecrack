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

/* Prints the command-line help. */
int render_usage(const char *prog);

/* Prints one chunk where the file and the seed disagree (seed_slime = what the seed says). */
void render_check_mismatch(int64_t x, int64_t z, int seed_slime);

/* Prints the MATCH / MISMATCH verdict of a seed check. */
void render_check_summary(uint64_t seed, long long bad, long long total);

#endif
