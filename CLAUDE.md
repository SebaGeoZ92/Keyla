# Keyla — contexto para Claude Code

App de escritorio Windows para aprender piano con teclado MIDI. Independiente:
sin DAW, sin plugins. Instrumento + profesor en el mismo programa.

Hardware de referencia: Nektar SE49 (USB-MIDI class compliant) sobre Windows.

## Antes de tocar nada, leer

El diseño completo está en `docs/`. Lo esencial:

- `docs/01-critica-y-riesgos.md` — errores conceptuales corregidos y riesgos
- `docs/02-arquitectura.md` — dominios de ejecución, modelo temporal, evaluación
- `docs/03-stack-y-estructura.md` — stack, ASIO vs WASAPI, estructura de directorios
- `docs/04-medicion-de-latencia.md` — cómo se mide y criterios de aceptación
- `docs/05-hoja-de-ruta.md` — fases y definición del prototipo actual

## Estado

**Fase 0 cerrada.** `src/tools/audio_probe` implementado y medido sobre el
hardware de referencia (SE49 + H510-PRO, WASAPI Exclusive 48 kHz / 144 samples).
Se cumple la columna *objetivo* de `docs/04-medicion-de-latencia.md` §8 entera:

| Criterio | Objetivo | Medido | De dónde |
|---|---|---|---|
| Latencia del sistema p50 | ≤ 8 ms | **7,54 ms** | tirada en vivo, 467 notas |
| Latencia del sistema máx | ≤ 12 ms | **9,50 ms** | ídem |
| Dropouts en 10 min | 0 | **0** en 200 010 callbacks | tirada de 600 s |
| Carga de CPU, 32 voces | < 30 % | **1,5 % (p95)**, 20,2 % pico | ídem |
| Jitter de entrada MIDI σ | < 1,5 ms | **0,025 ms** | `--selftest` vía loopMIDI |

Las cifras salen de **tres tiradas distintas**, no de una sola: son propiedades
independientes y ninguna necesita medirse a la vez que las demás. Los dropouts y
la CPU no dependen de que haya notas.

Conclusión para el doc 03 §2: **no hace falta interfaz de audio** — con la
salvedad de abajo sobre la latencia declarada.

Dos salvedades que hay que arrastrar:

- La latencia de salida son los **6,00 ms que declara el driver**, no una medida.
  Lo único medido con exactitud es la espera de buffer (1,5 ms). Con un receptor
  inalámbrico de 2,4 GHz esa cifra declarada es lo menos fiable del informe:
  hace falta el cable de loopback y `--calibrate` (doc 04 §3) antes de escribir
  en ningún sitio que no hace falta interfaz de audio.
- El jitter MIDI de 0,025 ms es de un **puerto virtual**, no del bus USB. Prueba
  que el código no añade jitter; el suelo real del USB-MIDI sigue siendo 1–2 ms.

`midi_monitor` (fase 0-A del doc 05) no se ha llegado a necesitar: `audio_probe`
cubre lo que iba a medir. No se escribe hasta que haga falta.

**Fase 1 hecha** salvo grabación y calibrador de loopback: aplicación con seis
instrumentos, reverberación, teclado iluminado, reconocimiento de acordes,
volumen general y ajustes persistentes en `%APPDATA%\Keyla`.

**Fase 3 empezada**: modo espera funcionando. Generador de escalas y arpegios,
máquina de estados que no avanza hasta que aciertas, evaluación de alturas
—nunca de ritmo, que aquí no existe— y adaptación al rango del teclado.

Se saltó la fase 2 (metrónomo y grabación) a propósito: el modo espera no
necesita reloj ni grabación, sólo emparejar alturas. La fase 2 hace falta antes
del **modo tempo**, no antes de éste.

Siguiente paso: fase 2 (metrónomo + grabación) para poder abordar la evaluación
temporal de la fase 4, que es donde está el valor real del producto.

## Invariantes de arquitectura

No negociables. Si algo obliga a romper uno, es señal de que el diseño está mal,
no el invariante.

1. **El hilo de audio no asigna memoria, no toma locks, no hace I/O, no loguea y
   no lanza excepciones.** Nunca.
2. **El reloj de audio es el reloj maestro.** Todo evento se sella con una
   posición de sample del stream. Nada de `std::chrono` ni del reloj del sistema
   para nada musical.
3. **Precisión de medida ≠ granularidad de render.** Los eventos MIDI conservan
   su posición fraccionaria exacta para evaluar, aunque se cuanticen al bloque
   para sintetizar.
4. **El MIDI llega en su propio hilo**, no en el de audio. Ruta obligatoria:
   callback MIDI → FIFO lock-free → drenado al inicio del callback de audio.
5. **La UI hace polling de snapshots inmutables.** Jamás se la notifica desde el
   dominio de tiempo real.
6. **`src/core/` no depende de `juce_gui_*` ni de `juce_audio_devices`.** Sólo
   módulos headless (`juce_core`, `juce_audio_basics`). Dispositivos y UI viven
   en `src/app/`, detrás de interfaces que define `core/`.
7. **Antes de reportar cualquier error de timing se resta el offset perceptual**
   (latencia de salida de audio + latencia de entrada MIDI). Sin esto todo alumno
   aparece sistemáticamente atrasado.
8. **La evaluación es una función pura sobre grabaciones**, no un proceso en
   vivo. El matcher en vivo sólo mueve el cursor visual.
9. **Cero dropouts manda sobre latencia baja.** Se optimiza en ese orden.
10. **Nada específico del SE49.** Cualquier controlador MIDI debe funcionar.

## Stack

C++17/20 · JUCE · CMake · MSVC (Visual Studio 2022 Build Tools).

- WASAPI Exclusive por defecto. ASIO tras el flag `KEYLA_ENABLE_ASIO` (OFF):
  el SDK de Steinberg no es redistribuible y no se versiona en este repo.
- Los samples de piano no van en git — `scripts/fetch_samples`.

## Entorno de desarrollo

- **Nativo en Windows, no WSL.** WSL no da acceso útil a WASAPI ni al USB-MIDI.
- **loopMIDI** para puertos MIDI virtuales: permite inyectar eventos sintéticos y
  probar la cadena completa sin el teclado físico. Todo ejecutable de `tools/`
  debe tener un modo automatizable (`--selftest`) que no requiera intervención.
- Lo que sigue necesitando al humano: juzgar el tacto y el sonido, y enchufar el
  cable de loopback para calibrar latencia.

## Build

Requiere CMake ≥ 3.22 y MSVC. JUCE se descarga solo la primera vez en
`libs/_deps/` (ignorado por git); con `-DKEYLA_JUCE_PATH=<ruta>` usa una copia
local y no descarga nada.

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target audio_probe
```

El binario sale en
`build/src/tools/audio_probe/audio_probe_artefacts/Release/audio_probe.exe`.

**Siempre en Release.** En Debug las cifras de carga de CPU no significan nada;
el propio informe lo avisa.

Opciones de CMake:

| Opción | Por defecto | Para qué |
|---|---|---|
| `KEYLA_ENABLE_ASIO` | OFF | Compilar con ASIO. Exige `KEYLA_ASIO_SDK_PATH`. |
| `KEYLA_ASIO_SDK_PATH` | — | Ruta a `<sdk>/common` de Steinberg. No se versiona. |
| `KEYLA_JUCE_TAG` | 8.0.15 | Tag de JUCE a descargar. |
| `KEYLA_JUCE_PATH` | — | Checkout de JUCE ya existente. |

### Lo que sabemos de esta máquina

Medido con `audio_probe`, no supuesto:

- **De los dispositivos virtuales de SteelSeries Sonar, unos dan stream y otros
  no.** `Sonar - Gaming` acepta `open()`, levanta el flag de abierto y no
  entrega ni un callback, ni en exclusivo ni en compartido. `Sonar - Chat` en
  cambio funciona: 128 samples, 5,3 ms declarados, sin dropouts. La primera
  versión de esta nota decía que ninguno funcionaba, y era generalizar de una
  sola prueba.

  En cualquier caso son un **mezclador por software**, no la tarjeta: añaden
  proceso y latencia que el driver no declara, así que medir sobre ellos falsea
  el resultado. Uno de ellos es la salida *predeterminada* de Windows aquí, de
  modo que hay que elegir la salida física a mano — `--audio-out` en las
  herramientas, y en la app se prefiere automáticamente la física y se avisa si
  la elegida es virtual.
- **El H510-PRO (inalámbrico de 2,4 GHz) sólo admite buffers de 144 en adelante**
  a 48 kHz en exclusivo, y declara 6 ms de latencia de salida. En modo compartido
  el mínimo sube a 480. Los 128 samples del doc 03 no son universales.
- Con 32 voces senoidales el hilo de audio va al **1,5 % de CPU (p95)** y el
  jitter del callback es de **0,17 ms σ (5,7 % del periodo)**, dentro del 15 %
  que pide el doc 04 §5. Sitio de sobra para un sampler.
- El SE49 expone dos entradas MIDI (`SE49` y `MIDIIN2 (SE49)`) y **también una
  salida**. Ojo: aparecer en las dos listas no lo convierte en un bucle.

### Smart App Control

Este equipo tiene Smart App Control activado
(`HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy\VerifiedAndReputablePolicyState = 1`),
que **bloquea cualquier ejecutable recién compilado**, incluido un hola-mundo.
Sin desactivarlo no se puede ejecutar nada de lo que compilemos. Desactivarlo es
irreversible sin reinstalar Windows, así que es decisión del usuario, no de
Claude. Mientras esté activo, Claude puede compilar pero no ejecutar: la salida
la aporta el usuario.

## Convenciones

- Documentación y comentarios en español.
- Identificadores y nombres de fichero en inglés.
- **Todo literal con acentos que acabe en un `juce::String` lleva el sufijo
  `_u8`** (ver `Utf8.h`). `juce::String (const char*)` construye con
  `CharPointer_ASCII`: interpreta byte a byte como Latin-1 y un texto UTF-8 sale
  doblemente codificado por consola. No hay aviso del compilador; sólo se ve en
  la salida.
- **Nada con acentos viaja por la línea de comandos en Windows.** Es la otra
  cara del punto anterior: los nombres de test en español rompen
  `catch_discover_tests`, que se los pasa al ejecutable como argumento y recibe
  basura. `tests/` registra una sola entrada de ctest por eso.
- `core/` se testea sin hardware. Las sesiones grabadas reales se guardan como
  fixtures en `tests/` — son el mejor material de test que tiene el proyecto.
