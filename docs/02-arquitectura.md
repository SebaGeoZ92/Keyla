# 02 — Arquitectura propuesta

Responde a los puntos 5, 6, 9, 10 y 14.5 de la visión.

---

## 1. El eje de corte correcto

Tu punto 10 propone dividir por *funcionalidad* (MIDI Engine, Audio Engine,
Exercise Engine...). Es una división razonable de **módulos**, pero no es la
división que determina si el software funciona.

La división que importa es por **dominio de ejecución**, porque define qué código
puede asignar memoria, tomar locks, tocar disco o fallar tarde:

```
┌─────────────────────────────────────────────────────────────┐
│  DOMINIO TIEMPO REAL   (callback de audio + callback MIDI)   │
│  Prohibido: malloc, locks, I/O, logging, excepciones         │
│  · ingesta y sellado temporal de MIDI                        │
│  · sintetizador / sampler                                    │
│  · clic de metrónomo                                         │
│  · transporte (contador de samples)                          │
└───────────────▲──────────────────────────┬──────────────────┘
                │ FIFO lock-free           │ FIFO lock-free
                │ (comandos → RT)          │ (eventos sellados → lógica)
┌───────────────┴──────────────────────────▼──────────────────┐
│  DOMINIO DE SESIÓN   (worker propio, prioridad normal)       │
│  Permitido: asignar memoria, disco, cálculo                  │
│  · máquina de estados del ejercicio                          │
│  · seguidor de partitura (matcher en vivo)                   │
│  · grabación                                                 │
│  · evaluación offline                                        │
│  · persistencia (SQLite/JSON)                                │
└───────────────▲──────────────────────────┬──────────────────┘
                │ intención del usuario    │ snapshot inmutable
┌───────────────┴──────────────────────────▼──────────────────┐
│  DOMINIO UI   (message thread, ~60 Hz)                       │
│  Sólo lee snapshots. Nunca es notificada desde RT.           │
└─────────────────────────────────────────────────────────────┘
```

Tres reglas, y son innegociables:

1. **Nada cruza un límite salvo por FIFO lock-free o snapshot inmutable.**
2. **El dominio RT nunca espera a nadie.** Si la lógica va lenta, el audio sigue.
3. **La UI hace polling, no recibe callbacks.** Un `Timer` a 60 Hz lee el estado
   publicado; el dominio de sesión publica sin bloquear.

Nota sobre el MIDI: **el MIDI no llega en el hilo de audio.** Llega en el hilo
del driver MIDI, que es un tercer hilo. Ese detalle falta en tu punto 10 y es
justo donde se cuela el bug clásico: el callback MIDI toca directamente el
sintetizador y aparece un race con el callback de audio. La ruta correcta es
`hilo MIDI → FIFO → drenado al inicio del callback de audio`.

### Módulos (dentro de esos dominios)

Casi los tuyos, con tres correcciones:

| Módulo | Dominio | Cambio respecto a tu propuesta |
|---|---|---|
| `midi/` | RT + sesión | Se parte: ingesta/sellado en RT, interpretación en sesión. |
| `time/` | RT + sesión | **Nuevo y central.** Transporte, compás, mapa de pulsos. No estaba en tu lista y es la pieza que lo sostiene todo. |
| `audio/` | RT | — |
| `instrument/` | RT | Interfaz `IInstrument`, implementaciones intercambiables. |
| `score/` | sesión | Modelo musical + importadores. |
| `exercise/` | sesión | Máquina de estados. |
| `evaluation/` | sesión | Función **pura** sobre grabaciones. |
| `recording/` | sesión | **No es un motor**: es un *tap* sobre el stream de eventos. Sube a fase 2. |
| `persistence/` | sesión | Settings y progreso son lo mismo: almacenamiento. No dos módulos. |
| `ui/` | UI | — |

`recording` y `evaluation` no son cosas separadas conceptualmente: la evaluación
consume lo que la grabación produce. Diseñarlas juntas desde el principio evita
un refactor grande.

---

## 2. Motor de audio

### Grafo

```
[Instrument A] ─┐
[Instrument B] ─┼─→ [Mixer] ─→ [Master gain] ─→ [Limiter] ─→ salida
[Metronome]   ─┘
```

Simple a propósito. Sin reverb en fase 1: añade latencia percibida y enmascara
problemas de sonido. Se añade después, como nodo insertable.

El **limiter** sí desde el día 1: un principiante con auriculares y un sampler
sin control de ganancia puede hacerse daño de verdad.

### Contrato de la interfaz de instrumento

```cpp
class IInstrument {
public:
    virtual ~IInstrument() = default;

    // Llamados fuera del hilo de audio, con el motor parado.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void release() = 0;

    // Llamado EN el hilo de audio. Sin asignaciones, sin locks, sin I/O.
    // Los eventos vienen con offset de sample dentro del bloque.
    virtual void process(AudioBuffer& out, const MidiEventSpan& events) = 0;

    virtual InstrumentInfo info() const = 0;
};
```

Esto responde a tu punto 9: el motor de aprendizaje **nunca** conoce el
instrumento. Habla con `IInstrument`. Cambiar de piano a órgano, o meter un
`SfizzInstrument` más adelante, no toca ni una línea del motor de ejercicios.

### Elección de motor de sonido

| Opción | Veredicto |
|---|---|
| Síntesis propia (aditiva/FM/modelado) | **No** para fase 1. Un piano sintetizado creíble es un proyecto en sí mismo. |
| Sampler propio + samples multicapa | **Sí.** Un sampler básico (un sample por nota-y-capa, envolvente, round-robin no necesario) son ~400 líneas y da un piano perfectamente usable. |
| FluidSynth + SoundFont | Rápido de integrar, pero LGPL y calidad de piano mediocre. Descartado como base. |
| sfizz (SFZ) | **Sí, pero en fase 8.** Es la evolución natural: SFZ es el formato abierto de sampler y hay librerías de piano excelentes. Integrarlo detrás de `IInstrument` es directo. |
| Pianoteq/Kontakt | Fuera de alcance. |

**Recomendación fase 1**: sampler propio + un set de piano reducido (por ejemplo
Salamander Grand, CC-BY — verificar términos), recortado a 4–6 capas de velocity
y 16 bits, precargado en RAM. Sin streaming de disco: con un set recortado cabe
en memoria y el streaming es una fuente de dropouts que no necesitamos todavía.

**Samples de release y resonancia por simpatía**: no en fase 1. Son lo que separa
"suena a piano" de "suena bien", pero son optimización de realismo y tú mismo
dijiste que no es la prioridad.

---

## 3. El modelo temporal (la pieza clave)

### Principio: el reloj de audio es el reloj maestro

Todo evento —una nota que tocas, un clic de metrónomo, una nota esperada de un
ejercicio— se expresa como una **posición en el contador de samples del stream**.
No en milisegundos del sistema, no en `std::chrono`.

```cpp
struct Transport {
    uint64_t streamSamplePos;   // monótono desde que arrancó el stream
    double   sampleRate;
    double   bpm;
    uint64_t barZeroSample;     // dónde cae el pulso 0
    TimeSignature meter;

    double sampleToBeat(uint64_t s) const;
    uint64_t beatToSample(double beat) const;
};
```

Consecuencias directas:

- El metrónomo no deriva: el pulso *N* está en
  `barZeroSample + N * (60/bpm) * sampleRate`. Exacto, siempre.
- Un cambio de tempo es un cambio de mapa, no un `Timer` reprogramado.
- La grabación y la partitura viven en el **mismo espacio de coordenadas**, así
  que la evaluación es aritmética, no sincronización.

### Sellado temporal del MIDI entrante

```
hilo MIDI:
    on_midi(msg, hostTicks):
        fifo.push({ msg, hostTicks })       // sin locks, sin allocs

hilo de audio, al inicio del callback:
    hostAtCallbackStart = highResTicksNow()
    while fifo.pop(e):
        dSec  = ticksToSeconds(e.hostTicks - hostAtCallbackStart)   // negativo
        exact = streamSamplePos + dSec * sampleRate                 // ← evaluación
        render= clamp(exact, streamSamplePos, streamSamplePos + n)  // ← síntesis
        ...
```

La distinción entre `exact` y `render` es deliberada y es la clave de todo:

- Para **sintetizar**, el evento se cuantiza al bloque actual: ya ocurrió, hay
  que sonarlo ya. La granularidad del buffer es un límite físico.
- Para **evaluar**, se conserva la posición fraccionaria real. Así la precisión
  de medida **no está limitada por el tamaño del buffer**. Con buffers de 128
  samples (2,7 ms) puedes medir timing con precisión muy inferior a 1 ms.

Esto responde a tu punto 5: la precisión de medición y el tamaño de buffer se
desacoplan. El límite real de precisión pasa a ser el transporte USB-MIDI
(tramas de 1 ms), no nuestra arquitectura.

### Corrección del offset perceptual

Como se explica en el doc 01 §1.4, antes de reportar cualquier error de timing:

```
error_reportado = (sample_nota - sample_esperado)
                  - latencia_salida_audio      // el clic lo oíste tarde
                  - latencia_entrada_midi      // tu tecla llegó tarde
```

Ambas constantes salen de la calibración del doc 04. Sin esto, todo alumno
aparece sistemáticamente atrasado.

---

## 4. Motor MIDI

### Qué capturamos

Todo mensaje entrante se guarda crudo con su sello temporal. Interpretados desde
el día 1:

- Note On / Note Off (con nota, canal, velocity) — Note On con velocity 0 se
  normaliza a Note Off.
- **CC64 sustain**, con umbral a 64 y soporte de *half-pedal* (el valor continuo
  se guarda aunque de momento sólo se use el umbral).
- CC1 modulación, CC7 volumen, CC11 expresión, pitch bend: se registran, se
  enrutan al instrumento, no se evalúan.

Todo lo demás se graba crudo y se ignora. Grabar crudo es gratis y evita tener
que repetir sesiones cuando en un año quieras analizar algo nuevo.

### Estado de teclado y pedal

Un `KeyboardState` en el dominio de sesión: qué está pulsado, desde cuándo, con
qué velocity, y qué está sonando *por pedal* aunque la tecla esté suelta. Esa
distinción (tecla suelta vs nota sonando) es necesaria tanto para la
visualización correcta como para evaluar legato y uso del pedal.

### Calibración de velocity

Un asistente corto: "toca lo más suave que puedas", "toca normal", "toca
fuerte", varias repeticiones. De ahí sale la curva real de tu controlador y su
rango útil.

Sin esto, "velocity demasiado fuerte" es una afirmación sobre el hardware, no
sobre tu forma de tocar. Es barato (una tarde) y hace honesta una función que en
otras apps es decorativa.

---

## 5. Motor de evaluación

### Modelo de datos

```cpp
struct RecordedNote {                struct ExpectedEvent {   // un evento = acorde o nota
    uint8_t  pitch, velocity, channel;    double   onsetBeat;
    uint64_t onsetSample, offsetSample;   double   durationBeats;
    bool     sustainedByPedal;            std::vector<uint8_t> pitches;
};                                        Hand     hand;
                                          std::optional<uint8_t> finger;
                                      };
```

Las notas esperadas están en **pulsos** (independiente del tempo); las tocadas en
**samples**. El transporte convierte entre ambos. Así el mismo ejercicio se
practica a 60 o a 120 BPM sin duplicar datos y la evaluación se normaliza sola.

### Alineación en dos pasadas

**Pasada 1 — en vivo (barata, tolerante).** Cursor sobre los eventos esperados y
ventana de ±2 eventos. Para cada nota entrante: buscar el evento no satisfecho
más cercano en la ventana que contenga esa altura. Si no hay, la nota es extra.
El cursor avanza cuando un evento se satisface o cuando el tiempo lo rebasa. Sólo
sirve para saber *dónde está el usuario* y mover el cursor visual.

**Pasada 2 — al terminar (óptima).** Alineación completa por distancia de edición
sobre toda la ejecución, con coste en altura + coste temporal. Produce el
etiquetado definitivo:

| Etiqueta | Cómo se deriva |
|---|---|
| correcta | emparejada, error temporal dentro de tolerancia |
| adelantada / atrasada | emparejada, error fuera de tolerancia, con signo |
| altura incorrecta | emparejada por posición, altura distinta → reportar **el intervalo** ("un semitono abajo" es diagnóstico; "incorrecta" no) |
| omitida | evento esperado sin emparejar |
| adicional | nota tocada sin emparejar |
| orden incorrecto | dos emparejamientos que se cruzan |
| sostenida de más / soltada pronto | sobre la duración, con tolerancia mucho más laxa |

Que la pasada 2 sea **una función pura sobre datos** significa que es testeable
con ficheros, sin hardware, sin audio y sin UI. Ahí es donde va el grueso de los
tests.

### Métricas (informe del intento)

Un porcentaje de acierto no enseña nada. Estas sí:

| Métrica | Qué significa | Por qué importa |
|---|---|---|
| Precisión de altura | % de eventos con altura correcta | Nivel base |
| **Sesgo temporal** | error medio *con signo*, en ms | Adelantarse siempre ≠ ir errático. Problemas distintos, correcciones distintas. |
| **Consistencia** | σ del error temporal | Es *la* métrica de músico. Mejora antes que la velocidad. |
| **Deriva de tempo** | pendiente de la regresión del error sobre el tiempo | Detecta acelerar/frenar, que el error medio esconde. |
| **Regularidad** (escalas) | σ de los intervalos entre ataques | Objetivo, brutalmente honesto, y exactamente lo que un profesor te corrige en escalas. |
| Uniformidad de velocity | σ de velocity dentro de un pasaje | Detecta dedos débiles (4º y 5º) |
| Índice de pulgar | timing/velocity en los pasos de pulgar de la escala | El bache clásico en escalas, medible |

La *regularidad* y la *deriva* son las dos que convierten esto en una herramienta
de práctica real en vez de un juego de aciertos.

### Modos de ejercicio (distinción que falta en la visión)

Son dos evaluadores distintos y hay que separarlos:

- **Modo espera** (sin tempo): no avanza hasta que aciertas. Sin evaluación
  temporal — evaluar ritmo aquí no significa nada. Para aprender notas, posición
  y lectura.
- **Modo tempo** (con metrónomo): el reloj no espera. Evaluación temporal
  completa. Para consolidar.

Mezclarlos produce el error clásico de reportar errores de timing en un modo
donde el timing no existe.

---

## 6. Motor de partituras y formatos

### Qué formato primero: **Standard MIDI File (SMF)**

Razones, en orden:

1. Contiene exactamente lo que la evaluación necesita: alturas, onsets,
   duraciones, tempo, compás, velocity.
2. Parser sencillo y bien especificado (o el de JUCE, ya probado).
3. Hay una cantidad enorme de material disponible.
4. Es lo que exporta *cualquier* herramienta, incluido nuestro propio grabador.

Lo que MIDI **no** trae y hay que asumir: separación fiable de manos (heurística
por pista/canal/altura, corregible a mano), digitación, articulación, enarmonía
(Do♯ vs Re♭ — irrelevante hasta que haya notación).

### Después: MusicXML

Sólo cuando exista notación real. Aporta lo que falta (enarmonía, manos,
digitación, articulaciones, dinámica escrita) y no aporta nada hasta entonces.

### Y siempre: formato propio para ejercicios

Los ejercicios generados (escalas, arpegios, patrones) **no deben ser ficheros
MIDI**. Son declarativos:

```json
{
  "id": "scale.c-major.1oct.rh",
  "type": "scale",
  "root": "C4", "mode": "major", "octaves": 1, "hand": "right",
  "fingering": [1,2,3,1,2,3,4,5],
  "rhythm": "quarters",
  "tempoRange": { "min": 50, "target": 100 }
}
```

Ventajas: transponer, cambiar octavas o cambiar el ritmo es cambiar un campo, no
generar otro fichero. **El generador expande esto al modelo interno**, el mismo
al que el importador de MIDI convierte sus canciones. Un solo modelo interno, dos
fuentes.

Orden recomendado: **modelo interno → generador de ejercicios → importador MIDI →
(mucho después) MusicXML**.

---

## 7. Visualización

Prioridad, de mayor a menor valor pedagógico:

1. **Teclado virtual** con teclas iluminadas — imprescindible, barato, y es lo
   que conecta pantalla y manos.
2. **Notas cayendo** (piano-roll vertical hacia el teclado) — mejor que la
   partitura para principiantes, y trivial comparado con notación.
3. **Barra de posición/compás** — dónde estás.
4. **Informe post-intento** — la línea de tiempo con los errores marcados y las
   métricas. Aquí sí va la información densa.
5. **Notación** — al final, y sólo para ejercicios de lectura.

Regla de interfaz, alineada con tu punto 8 y con el 13: **durante la ejecución,
como máximo dos elementos vivos en pantalla**. El resto, después. Tu instinto de
"que no parezca un DAW" se implementa así: no con un tema visual bonito, sino
mostrando menos cosas.

El panel técnico (dispositivos, buffer, latencia, dropouts) existe pero vive
detrás de un botón, no en la pantalla principal.
