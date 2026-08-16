# Keyla

Aplicación de escritorio para Windows para aprender y practicar piano con un
teclado MIDI. Independiente: sin DAW, sin plugins.

```
Teclado MIDI  →  USB  →  Keyla  →  audio  →  auriculares
```

Instrumento y profesor en el mismo programa: recibe MIDI en tiempo real, genera
sonido con baja latencia, y evalúa notas, ritmo y regularidad para dar feedback
útil sobre cómo estás tocando.

## Estado

**Fase de diseño.** Todavía no hay código. Ver la hoja de ruta.

## Documentación

| Documento | Contenido |
|---|---|
| [01 — Crítica y riesgos](docs/01-critica-y-riesgos.md) | Qué está mal planteado en la visión inicial, riesgos técnicos, qué no puede evaluar el MIDI |
| [02 — Arquitectura](docs/02-arquitectura.md) | Dominios de ejecución, modelo temporal, motor de audio, evaluación, formatos |
| [03 — Stack y estructura](docs/03-stack-y-estructura.md) | Comparación de stacks, ASIO vs WASAPI, estructura de directorios |
| [04 — Medición de latencia](docs/04-medicion-de-latencia.md) | Cómo se mide objetivamente, calibración por loopback, criterios de aceptación |
| [05 — Hoja de ruta](docs/05-hoja-de-ruta.md) | Fases 0–9 y definición del primer prototipo |

## Decisiones principales

- **C++ + JUCE + CMake**, con `core/` libre de dependencias de UI y de dispositivo.
- **WASAPI Exclusive** por defecto; ASIO como build opcional (el SDK de Steinberg
  no es redistribuible).
- **El reloj de audio es el reloj maestro.** Todo evento se sella con una posición
  de sample; la evaluación no depende del reloj del sistema ni del tamaño de buffer.
- **Evaluar es analizar una grabación**: la evaluación es una función pura y
  testeable, no un proceso en vivo.
- **MIDI** como primer formato de importación; notación musical al final.

## Hardware de referencia

Nektar SE49 (USB-MIDI class compliant) sobre Windows. Ningún código es específico
de ese modelo.
