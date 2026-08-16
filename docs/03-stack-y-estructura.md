# 03 — Stack tecnológico y estructura del proyecto

Responde a los puntos 4, 11 y 14.6–14.7.

---

## 1. Stack: comparación

Criterios en tu orden: (1) latencia, (2) estabilidad, (3) MIDI, (4) distribución,
(5) mantenibilidad, (6) capacidad de crecer.

| Stack | Latencia | MIDI | Distribución | Coste de crecer | Veredicto |
|---|---|---|---|---|---|
| **C++ + JUCE** | Excelente (ASIO/WASAPI excl./DirectSound) | Excelente, multiplataforma | Buena (CMake → .exe, instalador) | Muy bajo: VST3, notación, más plataformas ya cubiertos | **Recomendado** |
| C++ + miniaudio/RtAudio + RtMidi + Dear ImGui | Excelente | Buena | Buena, binario pequeño | Alto: UI, gráficos, ficheros de audio, packaging, todo a mano | Alternativa seria si la licencia de JUCE molesta |
| Rust + cpal/midir + egui | Muy buena | Buena | Buena | Medio: ecosistema de audio más fino, WASAPI exclusive menos maduro | Buena elección técnica, peor ecosistema musical |
| C# / .NET + NAudio | **Mala en el hilo RT** | Buena | Muy buena | — | **Descartado**: el GC en o cerca del camino de audio es incompatible con el objetivo nº 1 |
| C++ core + C# UI | Excelente | Excelente | Media | Alto: dos ecosistemas, un puente, dos builds | Sólo si la UI fuese lo dominante. No lo es. |
| Electron / Web Audio / Web MIDI | Mala (20–50 ms típicos, sin control de buffer, timestamps pobres) | Limitada | Excelente | — | **Descartado** por el objetivo nº 1 |
| Python + pygame/mido | Mala | Buena | Mala | — | Sirve para prototipar análisis offline, no para el producto |

### Recomendación: **C++17/20 + JUCE + CMake**

Tu intuición era correcta. Lo que JUCE aporta y aquí pesa de verdad:

- `AudioDeviceManager`: abstracción real sobre ASIO / WASAPI (shared y exclusive)
  / DirectSound, con selección de dispositivo, sample rate y buffer, y **reporte
  de latencia del dispositivo** — que es exactamente lo que pide tu punto 4.
- MIDI in/out con timestamps y hot-plug.
- Carga/decodificación de audio y resampling (lo necesita el sampler).
- `AudioProcessorGraph`, FIFOs lock-free, utilidades de hilo RT ya resueltas.
- Ruta a VST3 si algún día la quieres, sin rearquitectura.

**Pero con una condición de diseño**, que es la cobertura del riesgo:

> El directorio `core/` no depende de JUCE salvo de los módulos *headless*
> (`juce_core`, `juce_audio_basics`). **Nada de `juce_gui_*` ni
> `juce_audio_devices` dentro de `core/`.** Los dispositivos y la UI viven en
> `app/`, detrás de interfaces que `core/` define.

Con eso, el 70% del proyecto (evaluación, partituras, ejercicios, progreso,
modelo temporal) es C++ portable y testeable sin JUCE, y un eventual cambio de
framework tocaría la carcasa, no el producto.

### Licencia

JUCE es GPLv3 o licencia comercial, con un tier gratuito sujeto a umbral de
facturación. **Verifica los términos vigentes antes de distribuir.** Para uso
personal y aprendizaje no hay problema. Si en el futuro quieres cerrar el código
y no cumples el tier gratuito, el escape es la fila 2 de la tabla — y por eso
`core/` se mantiene limpio.

---

## 2. Audio en Windows: ASIO vs WASAPI

Tu pregunta del punto 4 ("¿cuánto se puede conseguir por software?") tiene una
respuesta bastante clara.

| API | Latencia realista de buffer | Requiere driver del fabricante | Redistribuible | Bloquea el dispositivo |
|---|---|---|---|---|
| WASAPI **Shared** | 10–30 ms | No | Sí | No |
| **WASAPI Exclusive** | **2,7–6 ms** (128–256 samples @ 48 kHz) | No | Sí | Sí |
| ASIO (driver real de fabricante) | 1,3–5 ms | Sí, y sólo lo tienen las interfaces dedicadas | **No** (SDK de Steinberg) | Sí |
| ASIO4ALL | Similar a WASAPI excl. (es un wrapper sobre WDM/KS) | Instalación aparte | No empaquetable | Sí |
| DirectSound / MME | 30–100 ms | No | Sí | No |

**Conclusiones prácticas:**

1. **WASAPI Exclusive es el objetivo por defecto.** Con el audio integrado de tu
   PC deberías conseguir buffers de 128–256 samples. Eso son 2,7–5,3 ms de
   buffer; con la latencia del DAC, el total realista es **6–10 ms**. Para
   piano, por debajo de 10 ms se siente sólido.
2. **Casi con total seguridad no necesitas una interfaz de audio.** El hecho de
   que ya consigas latencia baja en REAPER lo confirma: REAPER está usando
   WASAPI exclusive o ASIO4ALL sobre el mismo hardware. Podemos igualarlo.
   Una interfaz externa aportaría ~2–3 ms y, sobre todo, estabilidad — no es una
   compra prioritaria. El pedal de sustain lo es más (doc 01 §1.1).
3. **ASIO: soportado, no empaquetado.** El SDK de Steinberg no es redistribuible
   y distribuir un producto con ASIO exige su acuerdo. JUCE no incluye el SDK.
   Solución: flag de CMake `KEYLA_ENABLE_ASIO` (OFF por defecto). Tú puedes
   compilar con ASIO en local descargando el SDK; el binario público sale con
   WASAPI. **Ningún cambio de arquitectura** — es la misma abstracción de
   dispositivo, que es justo lo que pedías en tu punto 4.
4. **Modo dual obligatorio.** Exclusive bloquea el dispositivo: mientras
   practicas, no suena YouTube. Como vas a aprender con tutoriales, hace falta
   conmutar entre "modo práctica" (Exclusive, mínima latencia) y "modo tutorial"
   (Shared, convive con el resto). Con un botón y explicado en lenguaje normal.
5. **Windows MIDI Services** (el stack MIDI nuevo de Microsoft, con MIDI 2.0 y
   mejores timestamps) es la evolución natural del backend MIDI. No es necesario
   ahora: el SE49 es USB-MIDI 1.0 y su jitter lo domina el bus USB (tramas de
   1 ms), no el driver. Con la ingesta MIDI detrás de una interfaz propia,
   cambiar el backend después es local.

---

## 3. Estructura del proyecto

```
Keyla/
├── CMakeLists.txt
├── cmake/                          # toolchain, opciones, FetchContent
├── docs/                           # este material + ADRs
├── libs/                           # JUCE (submódulo), Catch2/GoogleTest, SQLite
│
├── src/
│   ├── core/                       # ── SIN JUCE gui/devices. Testeable. ──
│   │   ├── time/                   # Transport, TimeSignature, BeatMap, TempoMap
│   │   ├── midi/                   # MidiEvent, LockFreeMidiQueue, KeyboardState,
│   │   │                           #   VelocityCurve
│   │   ├── audio/                  # AudioGraph, Mixer, Limiter, IAudioSource
│   │   ├── instrument/             # IInstrument, Sampler, VoiceAllocator,
│   │   │                           #   Metronome
│   │   ├── score/                  # Score, ExpectedEvent, MidiFileImporter,
│   │   │                           #   ExerciseGenerator
│   │   ├── exercise/               # Exercise, ExerciseRunner (máquina de estados)
│   │   ├── evaluation/             # LiveMatcher, OfflineAligner, Metrics, Report
│   │   ├── recording/              # SessionRecorder, SessionFile
│   │   └── persistence/            # ProgressStore (SQLite), SettingsStore (JSON)
│   │
│   ├── app/                        # ── Carcasa JUCE ──
│   │   ├── AudioDeviceHost.*       # AudioIODeviceCallback → core
│   │   ├── MidiInputHost.*         # callback MIDI → cola lock-free
│   │   ├── SessionThread.*         # el worker del dominio de sesión
│   │   ├── LatencyCalibrator.*     # medición por loopback (doc 04)
│   │   └── ui/
│   │       ├── PianoKeyboardView.* │ FallingNotesView.*
│   │       ├── ExerciseView.*      │ ReportView.*
│   │       └── SettingsView.*      │ AudioHealthPanel.*
│   │
│   └── tools/                      # ── Ejecutables de diagnóstico ──
│       ├── midi_monitor/           # spike fase 0-A
│       ├── audio_probe/            # spike fase 0-B
│       └── latency_probe/          # medición por loopback, headless
│
├── tests/                          # unitarios de core/ + fixtures de sesiones
├── assets/
│   ├── samples/                    # piano (fuera de git; script de descarga)
│   └── exercises/                  # definiciones JSON
└── scripts/                        # fetch_samples, build, package
```

### Decisiones de estructura que importan

- **`core/` y `app/` separados en el nivel superior**, no `engine/` y `gui/`
  mezclados. El límite es una regla de compilación, no una convención: si alguien
  incluye `juce_gui_basics` desde `core/`, el build falla.
- **`tools/` como ejecutables reales**, no scripts de usar y tirar. El medidor de
  latencia y el monitor MIDI hacen falta *durante toda la vida del proyecto*,
  no sólo en la fase 0.
- **`tests/` con sesiones grabadas como fixtures.** Cuando la evaluación es una
  función pura sobre grabaciones, puedes guardar una ejecución real tuya y usarla
  como caso de test para siempre. Esto vale oro y sólo es posible por el orden
  del doc 01 §1.8.
- **Los samples fuera de git**, con script de descarga y checksum. Nada de cientos
  de MB en el historial.
