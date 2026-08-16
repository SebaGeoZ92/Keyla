# 05 — Hoja de ruta

Responde a los puntos 12, 14.8 y 14.9.

Tu propuesta era buena. Cambio **cuatro cosas**, y las cuatro por razones
técnicas, no de gusto:

| Cambio | Por qué |
|---|---|
| El **modelo temporal y el metrónomo suben a la fase 1** (tú los tenías en la 3) | El reloj es arquitectura, no una función. Meterlo después es reescribir el motor. El metrónomo en sí es media tarde. |
| La **grabación sube a la fase 2** (tú la tenías al final) | Evaluar es analizar una grabación. Con la grabación primero, la evaluación es una función pura y testeable sin hardware. |
| Los **ejercicios se parten en dos fases**: sin tempo y con tempo | Son dos evaluadores distintos (doc 02 §5). Fusionarlos produce el error de evaluar ritmo donde no hay ritmo. |
| La **notación baja al final** y entra antes la importación de MIDI | Renderizar partitura es un subsistema de meses; ejecutar una canción son días. Y el cuello de botella real va a ser el contenido. |

---

## Fase 0 — Spike de latencia · *días, no semanas*

**No es la aplicación.** Son dos ejecutables desechables en `tools/` cuyo único
propósito es responder "¿es viable con este PC?" antes de invertir en
arquitectura.

- `midi_monitor`: abre el puerto del SE49, imprime cada mensaje con su timestamp
  de alta resolución, mide el jitter entre pulsaciones.
- `audio_probe`: abre WASAPI Exclusive a 48 kHz / 128 samples, genera una onda
  sinusoidal al recibir un Note On, cuenta dropouts, mide latencia por loopback.

Es literalmente la cadena que pediste: **SE49 → MIDI → procesamiento → sonido →
auriculares.**

**Se cierra la fase cuando** se cumplen los criterios numéricos del doc 04 §8.
Si no se cumplen, se investiga *antes* de escribir nada más — porque si el PC no
puede, todo lo demás cambia.

---

## Fase 1 — Instrumento tocable

El objetivo: que puedas abrir Keyla y tocar el piano sin abrir REAPER. A partir
de aquí ya es software real y la arquitectura del doc 02 queda fijada.

- Esqueleto de los tres dominios de ejecución + colas lock-free.
- `Transport` con reloj de samples y mapa de pulsos (**infraestructura, aunque
  todavía no se use para evaluar**).
- Sampler + set de piano reducido; asignación de voces; sustain CC64.
- Teclado virtual iluminado.
- Selección de dispositivos, sample rate, buffer; persistencia de ajustes;
  reconexión automática del puerto MIDI.
- Panel de salud de audio + calibrador de loopback (doc 04 §7).
- Detección y aviso de salida Bluetooth.
- Calibración de curva de velocity.

**Se cierra cuando** puedas tocar 30 minutos seguidos con cero dropouts y no eches
de menos REAPER.

---

## Fase 2 — Tiempo y grabación

Sin funciones nuevas visibles, pero es la fase que hace posible todo lo demás.

- Metrónomo generado en el hilo de audio, alineado al mapa de pulsos, con
  acentuación de compás.
- `SessionRecorder`: captura de todos los eventos MIDI con posición de sample
  exacta.
- Formato de sesión persistente + exportación a SMF.
- Reproducción de lo grabado a través del sampler.
- **Constante de offset de evaluación** calculada y persistida (doc 04 §6).

**Se cierra cuando** puedas grabarte con el metrónomo, reproducirlo, y una sesión
guardada pueda cargarse como fixture de test.

---

## Fase 3 — Ejercicios sin tempo (modo espera)

El primer profesor, con la evaluación más simple que es correcta.

- Modelo interno de partitura + `ExpectedEvent`.
- Generador de ejercicios (escalas, arpegios, patrones) desde JSON declarativo.
- Máquina de estados del ejercicio; modo espera: no avanza hasta que aciertas.
- Evaluación **sólo de alturas**. Nada de timing todavía.
- Feedback de posición mínimo durante la ejecución + resumen al terminar.
- Adaptación al rango del teclado (49 teclas): transponer o avisar.

**Se cierra cuando** puedas practicar la escala de Do mayor a dos manos y la app
sepa exactamente dónde estás y qué fallaste.

---

## Fase 4 — Evaluación temporal

Aquí está el valor real del producto y también su dificultad.

- Modo tempo: el reloj no espera.
- `LiveMatcher` (ventana deslizante) para seguimiento en vivo.
- `OfflineAligner` (distancia de edición) para el informe definitivo.
- Agrupación de acordes con tolerancia.
- Métricas: sesgo, consistencia, deriva de tempo, regularidad, uniformidad de
  velocity (doc 02 §5).
- Vista de informe post-intento con línea de tiempo navegable.
- Batería de tests sobre sesiones grabadas reales.

**Se cierra cuando** el informe te diga algo que no sabías de tu forma de tocar.
Ese es el criterio de aceptación, y es exigente a propósito.

---

## Fase 5 — Contenido

El riesgo del doc 01 §2.4: un motor excelente con tres ejercicios.

- Importador de SMF → modelo interno.
- Separación de manos (heurística + corrección manual).
- Visualización de notas cayendo.
- Práctica por secciones (bucle sobre un rango de compases).
- **Rampa de tempo adaptativa**: empezar al 60% y subir cuando se supere un
  umbral de precisión. Es barata una vez existen las métricas, y es *la* función
  que convierte esto en una herramienta de práctica de verdad.

---

## Fase 6 — Progreso y currículum

- SQLite: sesiones, intentos, métricas por ejercicio.
- Curvas de evolución (tempo alcanzado y consistencia a lo largo del tiempo —
  no "puntos").
- Selección de qué practicar hoy, con repaso espaciado de lo flojo.
- Rachas y objetivos, con moderación.

---

## Fase 7 — Notación

- Renderizado de partitura (evaluar Verovio u otro motor; revisar licencia).
- Importación de MusicXML.
- Modo lectura a primera vista.

Última porque es cara y porque hasta aquí no es imprescindible.

---

## Fase 8 — Sonido

- Integración de sfizz → soporte SFZ → cualquier librería de piano.
- Piano eléctrico, órgano, cuerdas.
- Samples de release, resonancia por simpatía, reverb.

---

## Fase 9 — Avanzado

- Windows MIDI Services / MIDI 2.0.
- Backend de audio para macOS (CoreAudio ya está en JUCE).
- Exportación de plugin VST3.
- Detección de digitación por inferencia, ejercicios generados a partir de tus
  errores.

---

## El primer prototipo funcional (respuesta al punto 14.9)

**Fase 0-B, `tools/audio_probe`.** Un único ejecutable de consola, sin UI, de
unas 300–400 líneas:

1. Enumera dispositivos MIDI y de audio; los elige por índice de línea de
   comandos.
2. Abre WASAPI Exclusive a 48 kHz con buffer configurable.
3. Al recibir Note On, genera una nota (sinusoidal con envolvente — todavía no un
   piano) con offset de sample exacto.
4. Imprime en vivo: latencia estimada, dropouts, carga de CPU, jitter MIDI.
5. Comando `calibrate` que ejecuta la medición por loopback del doc 04 §3.

Deliberadamente **sin interfaz gráfica y sin samples**: si esto no cumple los
números del doc 04 §8, ninguna cantidad de arquitectura lo va a arreglar, y es
mucho mejor descubrirlo con 400 líneas que con 4 000.
