# SlimeCrack

Recovers the world seed or the slime scrambler of a Minecraft world from a list of slime chunks
(Java Edition's slime-chunk formula; the scrambler is `seed-slime-*` in spigot.yml).

## Build
    cmake -B build && cmake --build build
(or: gcc -O3 -march=native -fopenmp src/*.c -o slimecrack)

## Usage
    slimecrack seed  chunks.csv [--scrambler N] [--anchor N]   # world seed (scrambler defaults to 987234911)
    slimecrack salt  WORLDSEED chunks.csv [--anchor N]         # scrambler for a known world seed
    slimecrack check SEED chunks.csv [--scrambler N]           # does SEED reproduce the file?

## chunks.csv
    x,z[,slime]
    0,1
    1,0
Chunk coordinates, one per line. `slime` is optional: 1 (default) = slime chunk, 0 = not a slime chunk.
The file must list EVERY slime chunk inside its bounding box: unlisted chunks there are treated as
not slime. More chunks means fewer candidate results.

## Output
Only the low 48 bits of the seed matter for slime chunks. If several candidates remain, the tool
lists them and suggests chunks to look at to tell them apart.
