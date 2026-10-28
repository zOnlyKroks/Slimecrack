#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chunks.h"
#include "crack.h"
#include "render.h"
#include "slime.h"
#ifdef SLIMECRACK_HAS_GUI
#include "gui.h"
#endif

/* Reports whether seed reproduces the file (listed = slime, unlisted in the bounding box = not slime). */
static int check_seed(const Ctx *ctx, const Chunks *ch, const uint64_t seed) {
    int64_t x0, z0, w, h;
    char *listed = chunks_grid(ch, MAX_OBS * 4LL, &x0, &z0, &w, &h);
    if (!listed) { fprintf(stderr, "area too large\n"); return 1; }
    long long bad = 0, shown = 0;
    for (int64_t x = x0; x < x0 + w; x++)
        for (int64_t z = z0; z < z0 + h; z++) {
            const int s = is_slime(ctx, seed, offs(x, z));
            if (s == listed[(x - x0) * h + (z - z0)]) continue;
            if (shown++ < 5) render_check_mismatch(x, z, s);
            bad++;
        }
    free(listed);
    render_check_summary(seed, bad, w * h);
    return bad ? 2 : 0;
}

/* Parses the command line, loads the chunk file and runs seed / salt / check. */
int main(const int argc, char **argv) {
    if (argc < 2) return render_usage(argv[0]);

    const char *cmd = argv[1];
    if (strcmp(cmd, "gui") == 0) {
#ifdef SLIMECRACK_HAS_GUI
        return gui_main();
#else
        fprintf(stderr, "this build has no GUI (configure with -DSLIMECRACK_GUI=ON)\n");
        return 1;
#endif
    }
    const int is_check = strcmp(cmd, "check") == 0;
    Ctx ctx = { .mode = MODE_SEED, .K = 0, .worldSeed = 0 };
    if (strcmp(cmd, "salt") == 0) ctx.mode = MODE_SALT;
    else if (strcmp(cmd, "seed") != 0 && !is_check) return render_usage(argv[0]);

    const int need = ctx.mode == MODE_SALT || is_check ? 2 : 1;   /* positionals: [WORLDSEED|SEED] chunks.csv */
    uint64_t scrambler = DEFAULT_SCRAMBLER;
    int anchor = 0, np = 0;
    const char *positional[2];
    for (int a = 2; a < argc; a++) {
        if (strcmp(argv[a], "--scrambler") == 0 && a + 1 < argc) scrambler = (uint64_t)strtoll(argv[++a], NULL, 0);
        else if (strcmp(argv[a], "--anchor") == 0 && a + 1 < argc) anchor = atoi(argv[++a]);
        else if (argv[a][0] == '-' && argv[a][1] == '-') { fprintf(stderr, "unknown option %s\n", argv[a]); return 1; }
        else if (np < 2) positional[np++] = argv[a];
    }
    if (np != need) return render_usage(argv[0]);
    ctx.K = (scrambler ^ MUL) & MASK48;
    if (ctx.mode == MODE_SALT) ctx.worldSeed = (uint64_t)strtoll(positional[0], NULL, 0);

    Chunks *ch = chunks_new();
    int rc = 1;
    if (chunks_load(ch, positional[need - 1])) {
        if (ch->n == 0) {
            printf("no solutions\n");
            rc = 2;
        } else if (is_check) {
            rc = check_seed(&ctx, ch, strtoull(positional[0], NULL, 0) & MASK48);
        } else {
            Results res;
            chunks_fill_non_slime(ch);
            rc = crack(&ctx, ch, anchor, &res) > 0 ? 0 : 2;
            render_report(&ctx, ch, &res);
            free(res.v);
        }
    }
    chunks_free(ch);
    return rc;
}
