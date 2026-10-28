# Approach: Seed und Scrambler gleichzeitig

Gesucht sind `worldSeed & m` und der Scrambler, beide unbekannt. Es gilt wie in den anderen Dokumenten `m = (1L << 48) - 1`, `c_k` ist der Chunk-Offset (`offs(x, z)` in `src/slime.c`) und `K = scrambler ^ 0x5DEECE66D`:

```
state_k  = ((worldSeed + c_k) & m) ^ K
state'_k = (state_k * 0x5DEECE66DL + 0xB) & m
i_k      = state'_k >>> 17
slime_k  <=>  i_k % 10 == 0
```

Naiv sind das `2^48 * 2^48` Kombinationen. Auch der Trick aus `approach.md` (Slime-Chunk `a` aufzählen, `t` und `lo`) hilft allein nicht: er kostet `2^28` für `t`, dazu kommen noch 30 unbekannte hohe Bits von `K`, zusammen etwa `2^58`.

## Symmetrie

`(W + c) ^ K` ändert sich nicht, wenn Bit 47 in `W` **und** `K` gleichzeitig umgedreht wird (die Addition modulo `2^48` erzeugt an Bit 47 keinen Übertrag nach oben). Die Lösung ist also nur bis auf dieses Paar eindeutig. Beide Varianten liefern dieselbe Slime-Karte; das Programm gibt die mit `W`-Bit-47 = 0 aus.

## Stufe 1: untere 18 Bit

Die unteren `k` Bit von `state_b` hängen nur von den unteren `k` Bit von `W` und `K` ab (Addition und Multiplikation übertragen nur nach oben, XOR ist bitweise). Aus den unteren 18 Bit folgt Bit 17 von `state'_b`, also das niedrigste Bit von `i_b`. Bei einem Slime-Chunk muss `i_b` gerade sein, also Bit 17 = 0.

Alle `2^18 * 2^18` Paare `(W18, K18)` werden gegen alle Slime-Chunks geprüft. Jeder Chunk halbiert die Überlebenden. Mit 30 oder mehr Slime-Chunks bleibt nur noch eine Handvoll übrig. Die Prüfung ist ein Tabellenzugriff `bit17[((W18 + c_b) & m18) ^ K18]`, die Tabelle hat `2^18` Bit.

## Stufe 2: Bit für Bit hochziehen

Jeder Kandidat mit `k` bekannten Bit bekommt vier Kinder (neues Bit von `W` und `K` je 0 oder 1). Bis hierhin gibt es keine weitere Einschränkung, denn `i_b % 10 == 0` hängt von allen Bits ab und nicht bitweise von unten. Der Hebel ist ein Paar-Test.

**Paar-Test.** Seien `a` und `b` zwei Slime-Chunks mit kleinem Offset-Unterschied `ds = c_b - c_a` (vorzeichenbehaftet, `|ds| < 2^(k-1)`). Mit `va = (W + c_a) mod 2^k` ist `vb = va + ds`. Liegt `vb` wieder in `[0, 2^k)`, gab es keinen Übertrag in die oberen Bit, und es gilt:

```
state_b = state_a ^ (va ^ vb)         -> state_b und state_a unterscheiden sich nur in den unteren k Bit
delta   = state_b - state_a           (exakt bekannt, |delta| < 2^k)
```

Weil der LCG linear ist, ist `state'_b = state'_a + 0x5DEECE66D * delta (mod 2^48)`. Schreibt man `state'_a = (i_a << 17) | r_a` (`r_a` sind die unteren 17 Bit, bekannt) und `y = ((r_a + 0x5DEECE66D * delta) mod 2^48) >> 17`, dann ist

```
i_b = (i_a + y) mod 2^31
```

Da `i_a` durch 10 teilbar ist und `2^31 mod 10 = 8`, gilt `i_b % 10 == 0` genau dann, wenn `y % 10 == 0` (kein Überlauf) oder `y % 10 == 8` (Überlauf). Das hängt **nicht** vom unbekannten `t_a` ab, nur von den unteren Bit. Falsche Kandidaten bestehen also nur mit Wahrscheinlichkeit `1/5`.

Ist `vb` außerhalb von `[0, 2^k)` (Übertrag), lässt sich der Test mit `k` Bit noch nicht entscheiden und gilt als bestanden. Bei `k = 48` gibt es keinen Übertrag mehr, der Test ist dort exakt.

Das Programm wählt die bis zu 256 Paare mit dem kleinsten `|ds|` unter den ersten 1500 Slime-Chunks. Ein Paar wird ab `k = bitlen(|ds|) + 1` (mindestens 18) eingesetzt.

## Stufe 3: Suche

Breitensuche, solange höchstens `2^20` Kandidaten leben, danach Tiefensuche parallel (OpenMP) über diese Frontier bis `k = 48`. Jeder Kandidat mit 48 Bit wird exakt gegen alle Chunks geprüft (Slime und Nicht-Slime, inklusive Ablehnungsschleife). Beim Hochziehen auf Bit 47 wird nur `W`-Bit = 0 probiert (Symmetrie).

## Aufwand und Voraussetzungen

Bis die Paar-Tests greifen, wächst die Zahl der Kandidaten um den Faktor 4 pro Bit. Die Ausgabe nennt `~2^(2 * (kmin - 18))` Knoten, mit `kmin` aus dem engsten Paar.

- **Dichter Block:** Chunks in derselben Zeile (gleiches `z`) unterscheiden sich im Offset um weniger als etwa `2^33`, in der Nähe des Ursprungs um deutlich weniger. Dann greifen die Paare ab etwa Bit 25 bis 34, und die Suche braucht Minuten.
- **Verstreute Chunks** ohne enge Paare: `kmin` liegt bei 40 oder höher, die Suche ist praktisch nicht machbar (der Aufwand nähert sich `2^58`).
- **Zu wenige Slime-Chunks** (unter etwa 30): nach Stufe 1 bleiben zu viele Kandidaten. Bei über `2^24` bricht das Programm ab.

Wie bei den anderen Modi wird der seltene Ablehnungsfall (`i >= 2147483640`, Wahrscheinlichkeit `8 / 2^31` pro Chunk) in den Vorfiltern ignoriert. Das kann mit einer Wahrscheinlichkeit von etwa `4e-9` pro Chunk den echten Kandidaten fälschlich verwerfen. Die Endprüfung ist exakt.
