# Approach

```
bool isSlimeChunk(SlimeChunk chunk, long worldSeed) {
    long seed = worldSeed
        + chunk.x * chunk.x * 4_987_142L
        + chunk.x * 5_947_611L
        + chunk.z * chunk.z * 4_392_871L
        + chunk.z * 389_711L
        ^ 987_234_911L;

    long state = (seed ^ 0x5DEECE66DL) & ((1L << 48) - 1); // CheckedRandom ctor

    int i, j;
    do {
        state = (state * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1); // next(31)
        i = cast(int)(state >>> 17); // top 31 bits
        j = i % 10;
    }
    while (i - j + 9 < 0); // rejection (rare for 10)
    return j == 0;
}
```

Sei `m = (1L << 48) - 1`. Für den Chunk k sei `c_k = chunk.x * chunk.x * 4_987_142L + chunk.x * 5_947_611L + chunk.z * chunk.z * 4_392_871L + chunk.z * 389_711L`.

Da `987_234_911L ^ 0x5DEECE66DL = 0x5E434E432L` und die Addition nur nach oben überträgt:

```
state_k = ((worldSeed + c_k) & m) ^ 0x5E434E432
state'_k = (state_k * 0x5DEECE66DL + 0xB) & m
i_k = state'_k >>> 17
slime_k  <=>  i_k % 10 == 0          (ohne Ablehnungsfall i - j + 9 < 0, Wahrscheinlichkeit 9 / 2^31)
```

Nur `worldSeed & m` geht ein. Gesucht ist `worldSeed & m`.

**Aufzählung.** Für einen Slime-Chunk a gilt `i_a = 10t`, also `state'_a = (10t << 17) | lo` mit `0 <= t < 214748364` und `0 <= lo < 2^17`. Das sind `214748364 * 2^17 ≈ 2^48 / 10` Paare `(t, lo)`. Jedes Paar liefert genau einen Kandidaten:

```
state_a = ((state'_a - 0xB) * 0xDFE05BCB1365) & m
worldSeed & m = ((state_a ^ 0x5E434E432) - c_a) & m
```

(`0xDFE05BCB1365` ist die Inverse von `0x5DEECE66D` mod `2^48`.)

**Notwendige Bedingung aus der Parität.** `i_k % 10 == 0` impliziert `i_k % 2 == 0`, also Bit 17 von `state'_k` gleich 0. Bei `+`, `^` und `*` hängen die unteren `n` Bit des Ergebnisses nur von den unteren `n` Bit der Eingaben ab. Daher hängt Bit 17 von `state'_k` nur von `(worldSeed + c_k) & ((1L << 18) - 1)` ab.

Für den Anker gilt `(10t << 17) = 5t * 2^18 ≡ 0 (mod 2^18)`, also:

```
state_a & ((1L << 18) - 1) = ((lo - 0xB) * 0xDFE05BCB1365) & ((1L << 18) - 1)
(worldSeed + c_a) & ((1L << 18) - 1) = (state_a & ((1L << 18) - 1)) ^ (0x5E434E432 & ((1L << 18) - 1))
```

Diese unteren 18 Bit hängen nur von `lo`, nicht von `t` ab. Für jeden weiteren Slime-Chunk b mit `d_b = c_b - c_a` muss daher gelten:

```
Bit 17 von ((((worldSeed + c_a + d_b) & ((1L << 18) - 1)) ^ 0x5E434E432) * 0x5DEECE66DL + 0xB) = 0
```

Das ist ein Prädikat auf `lo`. Für falsche `lo` ist das Bit annähernd gleichverteilt, bei `N` Slime-Chunks bleiben erwartet `2^17 / 2^(N-1)` Werte von `lo` übrig. Nur für diese läuft die Aufzählung über alle `t`.

**Prüfung.** Ein Kandidat gilt genau dann als Lösung, wenn der exakte Algorithmus (mit Ablehnungsschleife) für jeden Chunk des Datensatzes das Ergebnis liefert, das die Datei vorgibt: gelistete Chunks Slime, ungelistete innerhalb des Bereichs nicht Slime. Die Filter sind nur notwendige Bedingungen und streichen nie eine Lösung.

**Ergebnis.** Die Aufzählung erfasst alle `worldSeed & m` mit `slime_a`, die Menge der Treffer ist damit vollständig. Bleibt genau ein Treffer, ist `worldSeed & m` eindeutig. Mehrere Treffer unterscheiden sich nur in Chunks außerhalb des Datensatzes.
