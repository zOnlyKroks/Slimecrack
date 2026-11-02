# Approach: Seed oder Scrambler durch Hochziehen

Gilt für `seed` (Scrambler bekannt, `worldSeed & m` gesucht) und `salt` (Seed bekannt, `K = scrambler ^ 0x5DEECE66D` gesucht). Mit `m = (1L << 48) - 1` und dem Chunk-Offset `c_k` (`offs(x, z)` in `src/slime.c`) gilt in beiden Fällen

```
state_k  = ((W + c_k) & m) ^ K
state'_k = (state_k * 0x5DEECE66DL + 0xB) & m
i_k      = state'_k >>> 17
slime_k  <=>  i_k % 10 == 0
```

wobei genau eine der Größen `W`, `K` unbekannt ist. Im Folgenden heißt sie `x`. Die Aufzählung aus `approach.md` kostet `2^28` Schritte pro überlebendem `lo`. Hier wird stattdessen `x` wie in `approach-both.md` Bit für Bit von unten aufgebaut, nur mit einer statt zwei Unbekannten.

## Stufe 1: untere 18 Bit

Wie in `approach-both.md`: Bit 17 von `state'_k` hängt nur von den unteren 18 Bit von `x` ab und muss für jeden Slime-Chunk 0 sein. Alle `2^18` Werte von `x & (2^18 - 1)` werden gegen alle Slime-Chunks geprüft.

## Stufe 2: Hochziehen mit dem Paar-Test

Jeder Kandidat mit `k` bekannten Bit bekommt zwei Kinder (neues Bit 0 oder 1). Der Paar-Test aus `approach-both.md` (`src/pairs.h`) streicht falsche Kandidaten mit Wahrscheinlichkeit `4/5` pro Paar, sobald `|ds| < 2^k` für ein Paar von Slime-Chunks gilt. Das Wachstum um Faktor 2 pro Bit endet damit bei `k ≈ kmin`, dem Bit-Bedarf des engsten Paars. Der Aufwand ist etwa `2^(kmin - 18)` Knoten statt `2^28` pro `lo`.

Der Paar-Test hängt nur von den bekannten unteren Bit ab, daher ist `K` im Seed-Modus komplett (auf `k` Bit maskiert) und `W` im Salt-Modus komplett bekannt.

## Stufe 3: Suche

Breitensuche, bis mindestens 4096 Kandidaten leben, danach Tiefensuche parallel (OpenMP) bis `k = 48`. Jeder Kandidat mit 48 Bit wird exakt gegen alle Chunks geprüft (Slime und Nicht-Slime, inklusive Ablehnungsschleife). Im Gegensatz zum Modus `both` gibt es hier keine Symmetrie, Bit 47 wird also in beiden Varianten probiert.

## Rückfall

Ohne ein Slime-Paar mit `kmin <= 40` (zum Beispiel bei verstreuten Chunks, oder unter zwei Slime-Chunks) wäre das Hochziehen nicht schneller als die Aufzählung, dann läuft `crack()` wie in `approach.md` beschrieben. `--anchor` wirkt nur dort.

Wie beim Modus `both` ignorieren die Paar-Tests den seltenen Ablehnungsfall (Wahrscheinlichkeit `8 / 2^31` pro Chunk). Die Endprüfung ist exakt.
