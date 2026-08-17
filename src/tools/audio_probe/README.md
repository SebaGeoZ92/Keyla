# audio_probe — spike de latencia (fase 0-B)

Responde a una sola pregunta: **¿es viable Keyla con este PC?** Antes de invertir
en arquitectura. Si no cumple los números del doc 04 §8, hay que investigar antes
de escribir nada más (doc 05).

Sin UI y sin samples, a propósito.

## Qué mide

| Métrica | Cómo |
|---|---|
| Latencia del sistema | `(inicio_bloque − t_midi_exacto)/sr + latencia_salida` por cada Note On; se reporta p50 / p95 / máx, nunca la media (doc 04 §4) |
| Dropouts | huecos en el reloj del stream entre callbacks + el contador de xruns del driver |
| Carga de CPU | duración del callback / periodo de buffer, con `--stress 32` para medirla a polifonía plena |
| Jitter del callback | σ del intervalo real entre callbacks |
| Jitter de entrada MIDI | σ(llegada − envío real) contra un generador de tempo conocido |
| Latencia de salida real | `--calibrate`, chirp + correlación cruzada por loopback (doc 04 §3) |

## Uso

```
audio_probe --list
```

Enumera dispositivos de audio y puertos MIDI.

**Elígelos por nombre, no por índice.** Todas las opciones de dispositivo aceptan
un trozo del nombre (`--midi-in SE49`, `--audio-out H510`) además del índice. Los
índices se desplazan en cuanto enchufas un cacharro o creas un puerto virtual, y
entonces acabas midiendo el dispositivo equivocado sin enterarte. El nombre exacto
gana sobre las coincidencias parciales, así que `SE49` no choca con
`MIDIIN2 (SE49)`.

**No te fíes del predeterminado.** Un dispositivo virtual (SteelSeries Sonar,
Voicemeeter y similares) puede aceptar abrirse y no entregar ni un callback; el
probe lo detecta y aborta con exit 2 en vez de reportar ceros. Pasa `--audio-out`
con la salida física.

### Modo en vivo (con el teclado)

```
audio_probe --audio-out H510 --midi-in SE49 --stress 32 --duration 600 --out informe.txt
```

Toca. Ctrl-C corta antes de tiempo. `--duration 600` es lo que exige el criterio
de dropouts: cero en 10 minutos.

Con un teclado físico **no se puede medir el jitter MIDI de verdad**: no sabemos
cuándo tocaba llegar cada nota, así que la σ de intervalos entre ataques incluye
tu propio timing. Se reporta como dato, no como criterio.

### Modo automatizable (sin teclado)

```
audio_probe --selftest
```

Necesita un puerto virtual de [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html):
crea uno y el probe lo encuentra solo. Se manda las notas a sí mismo a intervalos
exactos, apunta el instante real de cada envío y mide σ(llegada − envío). Eso
aísla el jitter de la ruta MIDI del jitter del hilo que genera.

La búsqueda del puerto **no se fía del nombre**: el SE49 aparece a la vez como
entrada y como salida y no forma bucle. Los candidatos con el mismo nombre a los
dos lados sólo ordenan la búsqueda; para cada uno se manda una nota inaudible en
el canal 16 y se espera 300 ms a que vuelva. Quien decide es el bucle.

Termina solo y devuelve **0 si se cumplen los criterios**, 1 si no. Con `--json`
saca el informe entero en JSON.

Ojo con la interpretación: por un puerto virtual esto mide la ruta software. El
jitter del bus USB del teclado sólo aparece con el teclado enchufado.

### Calibración por loopback

```
audio_probe --calibrate --audio-in 0
```

Cable de 3,5 mm de la salida de auriculares a la entrada de línea/micro. Emite
20 chirps, los busca por correlación cruzada y reporta la mediana del round-trip
y su dispersión. Escupe la línea de comandos que hay que usar después:

```
audio_probe --output-latency-ms 4.60
```

Sin esto, la latencia de salida sale de lo que declara el driver, y **los
drivers mienten**. La cifra del informe lo indica siempre.

## Estructura

| Fichero | Qué es |
|---|---|
| `main.cpp` | CLI, apertura de dispositivos, bucle de consola, orquestación |
| `ProbeEngine.*` | el dominio de tiempo real: callback MIDI, callback de audio, síntesis, métricas |
| `Lockfree.h` | FIFO SPSC, seqlock de snapshots, Welford, percentiles |
| `MidiPulseGenerator.*` | generador de notas a tempo exacto del `--selftest` |
| `LoopbackCalibrator.*` | método B del doc 04 §3 |
| `ProbeOptions.*` · `Report.*` | línea de comandos y veredicto contra el doc 04 §8 |

Los invariantes de `CLAUDE.md` se respetan ya aquí, aunque sea un spike: el hilo
de audio no asigna, no bloquea y no loguea; el MIDI entra por su propio hilo y
cruza una FIFO lock-free; la consola hace polling de snapshots inmutables; y la
posición fraccionaria exacta de cada evento se conserva para medir aunque se
cuantice al bloque para sonar.
