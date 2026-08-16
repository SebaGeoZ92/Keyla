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

**Fase 0** — spike de latencia (`tools/audio_probe`). Todavía sin código.
No empezar fases posteriores hasta que se cumplan los criterios de
`docs/04-medicion-de-latencia.md` §8.

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

Pendiente: se documenta aquí cuando exista `CMakeLists.txt`.

## Convenciones

- Documentación y comentarios en español.
- Identificadores y nombres de fichero en inglés.
- `core/` se testea sin hardware. Las sesiones grabadas reales se guardan como
  fixtures en `tests/` — son el mejor material de test que tiene el proyecto.
