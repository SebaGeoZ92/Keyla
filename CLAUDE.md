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

## Para qué es esto

**Keyla es la herramienta de el autor para aprender piano.** No es un producto
que haya que lanzar. Las decisiones se miden por si le ayudan a practicar
mañana, no por completitud de la hoja de ruta ni por hueco de mercado. Los
`docs/` siguen siendo la referencia técnica, pero su parte de producto pesa
menos que esto.

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

**Fase 1 hecha** salvo grabación y calibrador de loopback: aplicación con once
instrumentos, reverberación, teclado iluminado, reconocimiento de acordes,
volumen general y ajustes persistentes en `%APPDATA%\Keyla`.

**Once instrumentos, y uno de ellos por modelo físico.** Piano, piano eléctrico,
clavecín, órgano, acordeón, guitarra, cuerdas, coro, vibráfono, marimba y
flauta. La guitarra y el clavecín son **Karplus-Strong** —una cuerda pulsada de
verdad: ruido recirculando por un buffer del largo de un periodo— y salen
sorprendentemente baratos: **0,22 % y 0,27 % de tiempo real** frente al 12,4 %
del piano aditivo. El coro usa formantes fijos, que es lo que separa una vocal
de un órgano.

`allInstrumentIds()` es la única lista del catálogo: de ella cuelgan el
desplegable, la validación de ajustes y los tests, de modo que un instrumento
nuevo no se puede quedar sin probar ni sin poder elegirse. Los valores del enum
son un contrato de persistencia y sólo se añade al final.

Dos cosas que costaron y conviene no repetir:

- **La ganancia del bucle de Karplus-Strong se aplica por vuelta al buffer, no
  por sample.** Calculada como si fuera por sample, un Sol3 tardaba veinticinco
  segundos en apagarse y la voz no se liberaba nunca. La fórmula correcta lleva
  la frecuencia de la nota dentro.
- **El retardo tiene que ser fraccionario.** Redondeado a entero, un Do6 cae 13
  cents. Hay un test de afinación por autocorrelación que lo vigila — la primera
  versión contaba cruces por cero y daba disparates, porque un clavecín recién
  pulsado cruza el cero seis veces por ciclo.

Los recortes de ganancia del catálogo salen de medir, no de estimar: los once
instrumentos quedan en 0,33 de pico para una nota sola. El test `[catalogue]`
falla si alguien desequilibra uno, y `[cpu]` imprime el coste de cada uno.

**Arranque con Windows, con una regla que manda sobre la comodidad: Keyla no
retiene la tarjeta de sonido mientras no se la ve.** El modo exclusivo deja
mudo al resto del equipo, así que un arranque automático ingenuo significaría
quedarse sin sonido en el navegador cada vez que enciendes el PC, sin ninguna
pista de por qué. Arrancada por Windows (`--startup`), Keyla espera minimizada
y **sin abrir el dispositivo**; abre la tarjeta y se muestra cuando aparece el
teclado MIDI, y la suelta y se aparta cuando lo apagas. Encender el piano es la
señal inequívoca de que quieres tocar; arrancar el PC no lo es.

El acceso directo va en la carpeta de Inicio, no en la clave `Run`: se ve en el
Explorador y se borra a mano. `--startup-on` / `--startup-off` hacen lo mismo
que la casilla sin abrir ventana, que es también la única forma de verificar
que el acceso directo se escribe.

**Icono.** Desde que arranca sola, Keyla se ve en la barra de tareas, en el
acceso directo de Inicio y en el Administrador de tareas, así que el icono pasó
a tener función. Lo genera `scripts/make_logo.py` —sin dependencias: rasterizado
por barrido y el PNG escrito con zlib— y CMake se lo pasa a juceaide por
`ICON_BIG`. El diseño se edita ahí, no en un PNG suelto. Criterio con el que se
eligió: lo que decide un icono es si se reconoce a 16 px, no cómo se ve a 256.

**Mandos del teclado, aprendibles y con tres destinos.** Volumen, trémolo y
sala. El aprendizaje era antes cosa sólo del volumen; en cuanto apareció el
segundo destino se generalizó a `ControlTarget`, y añadir uno más es una línea
en el enum y un caso en el ruteo. Por defecto CC7, CC1 y CC91, que son los
estándar — pero se reaprenden, porque hay controladores que no respetan ninguno.

El **trémolo** va antes de la reverberación, como en un amplificador. Detalle
que no es cosmético: los dos canales van desfasados **un tercio de ciclo**, no
medio. Un trémolo de Rhodes de verdad es un paneo con los canales en oposición,
suena mejor en estéreo y **desaparece del todo** en cuanto algo suma la salida a
mono —barra de sonido en modo mono, altavoz de portátil, Bluetooth barato—
porque la suma de dos senos opuestos es constante. Con un tercio de ciclo hay
movimiento estéreo y la suma en mono sigue vaivén: medido, 93 %. Hay un test
que se cae si alguien lo "mejora" poniéndolos en oposición.

**La interfaz, rehecha mirándola.** Se fue montando fila a fila, una por
función, sin ver nunca el resultado; cuando por fin se miró parecía un panel de
configuración: tres filas de dispositivos siempre a la vista y lo musical en
renglones grises. Ahora hay una barra de sonido (instrumento, volumen, sala,
trémolo), una del ejercicio, **tres tarjetas** —*Ejercicio*, *Tú tocas*, *Suena
en el PC*— con el acorde en grande, el teclado, y una barra de estado en
castellano que sólo avisa cuando algo va mal. Salida, buffer, MIDI, arranque
con Windows y los números técnicos van a un panel de **Ajustes**.

- `ui/Theme.h` es el único sitio con colores. **El verde es de Keyla y
  significa "esto es música"** —el acorde que suena, las teclas a tocar, el
  botón de empezar—; si decora, deja de señalar.
- **Cifrados en letras, nombres en solfeo**: "Bb" como en Chordify, "Si bemol
  mayor" como se dice. La interfaz mezclaba "Do mayor" con "Tonalidad C mayor".
  `spanishPitchClassName` + `conventionalAccidental` (Pitch.h) lo resuelven en
  un sitio; los identificadores de ejercicio siguen en letras porque se guardan.
- **`keyla_snapshot [--demo] [--settings] salida.png`** dibuja la ventana sin
  abrirla y sin tocar dispositivos. Es lo que permite diseñar mirando. La
  primera versión dejaba correr el temporizador, detectaba el teclado, salía
  del modo desatendido y abría la salida en exclusivo con Keyla sonando al lado.

**Fase 3 empezada**: modo espera funcionando. Generador de escalas y arpegios,
máquina de estados que no avanza hasta que aciertas, evaluación de alturas
—nunca de ritmo, que aquí no existe— y adaptación al rango del teclado.

Se saltó la fase 2 (metrónomo y grabación) a propósito: el modo espera no
necesita reloj ni grabación, sólo emparejar alturas. La fase 2 hace falta antes
del **modo tempo**, no antes de éste.

**Biblioteca de ejercicios generada, no escrita a mano.** Trece escalas (con
los modos griegos), seis arpegios, ocho progresiones y doce tónicas se combinan
desde los desplegables de la ventana. Las progresiones se pueden tocar en
estado fundamental, con enlace de voces o con bajo en la izquierda; el enlace
es lo que las convierte en un ejercicio de piano en vez de en una lista de
acordes.

**Importador de MIDI hecho** (doc 05 fase 5, y respuesta al riesgo del doc 01
§2.4). Un solo modelo interno y dos fuentes: el generador de escalas y el SMF.
Separa manos por pista, por canal o —como último recurso y diciéndolo— por
altura. No inventa digitación.

**Fases 2 y 4, el motor.** Metrónomo calculado desde el mapa de pulsos —no
reprogramado, por eso no deriva—, grabación con posición fraccionaria exacta,
alineación por distancia de edición y las métricas del doc 02 §5: sesgo,
consistencia, deriva de tempo, regularidad y uniformidad de velocity. La
aplicación tiene ya los dos modos: espera y tempo.

**Keyla escuchando (a medias, y la mitad hecha es la difícil).** `core/listen/`
saca acordes y tonalidad de audio, sin que nadie le diga qué notas hay. Medido
sobre el propio catálogo de instrumentos —que es material honesto, porque el
piano tiene 24 parciales y son los armónicos los que hacen difícil esto—:
**100 % de acierto en fundamental y calidad sobre 48 acordes**, y sigue una
progresión entera deduciendo la tonalidad.

- `Chromagram` hace análisis de **Q constante**: una ventana distinta por nota,
  todas con el mismo número de ciclos. Una FFT reparte la frecuencia en trozos
  iguales y la música no: entre Do1 y Do#1 hay 4 Hz y entre Do6 y Do#6 hay 62.
  El precio es que el grave necesita ventanas largas y por eso siempre sale más
  borroso; no es un bug, es el compromiso entre tiempo y frecuencia.
- `HarmonyListener` compara contra las **mismas plantillas** que el reconocedor
  de MIDI (`ChordRecognizer::intervalsFor`), agudiza el cromagrama para hundir
  los armónicos parásitos, y exige acuerdo sostenido antes de cambiar el
  cifrado. Reporta confianza **y margen**: una confianza de 0,9 con margen de
  0,002 significa que había dos lecturas empatadas.
- **El bajo manda, también aquí.** No es un refinamiento: Do#m7 y Mi6 son las
  mismas cuatro notas y sus plantillas empatan al decimosexto decimal. Sin
  desempatar por el bajo, el reconocedor se quedaba mudo en un acorde de cada
  ocho. Con la regla puesta, el acierto pasó del 89,6 % al 100 %.

**El ciclo entero, cerrado y verificado sobre el equipo real.** Keyla escucha
una salida de Windows, saca el acorde y enseña dónde ponerlo en la mano
encendiendo las teclas de la pantalla.

- `LoopbackCapture` (src/app/): WASAPI en modo compartido con
  `AUDCLNT_STREAMFLAGS_LOOPBACK`, escrito con COM porque **JUCE no lo trae**
  —cero apariciones de *loopback* en todo `juce_audio_devices`—. Toda la
  apertura ocurre en el hilo de captura: los objetos de WASAPI y COM se llevan
  mal con cruzar de hilo.
- El loopback **no funciona sobre un endpoint abierto en exclusivo**. Escuchar
  y sonar en exclusivo por la misma salida son incompatibles, y se dice con
  esas palabras cuando pasa.
- `AccompanimentCoach` (core/listen/) reutiliza `voiceChordNear` y
  `bassNoteFor`, que se sacaron a público en `ProgressionGenerator`. Medido
  sobre Do-Sol-Lam-Fa: **12 semitonos de recorrido de mano enlazando voces
  contra 52 en estado fundamental**.
- `--listen-test` verifica la cadena entera sin ventana y sin humano. Tirada
  real contra un WAV de prueba: `C G Am F`, tonalidad Do mayor, y las
  colocaciones salen con enlace de voces de libro (C4-E4-G4 → B3-D4-G4 →
  C4-E4-A4 → C4-F4-A4).

**Sesiones de escucha grabadas: cómo se mejora el reconocimiento.** Keyla no
aprende sola —no hay nada dentro que cambie con el uso—, pero cada vez que
el autor toca siguiendo una canción produce un examen corregido: sus teclas
son la respuesta buena. El botón **Grabar** guarda el audio capturado y las
notas del teclado en la misma escala de tiempo; al parar se reanaliza la
grabación y se escribe `informe.txt` con acuerdo, desfase, confusiones y la
línea de tiempo *oyó / sugirió / tocaste*.

- `evaluateListening` y `chordsFromNotes` (core/listen/) son funciones puras
  y están testeadas con líneas de tiempo escritas a mano (invariante 8).
- **El desfase se busca, no se supone.** Suma el retraso de las manos y el de
  la ventana de análisis; sin corregirlo, cada cambio de acorde contaría como
  error durante medio segundo aunque los dos hubieran acertado. Verificado con
  teclas puestas 0,45 s tarde: sale 0,15 s, que son esos 0,45 menos los ~0,3
  que el análisis va por detrás.
- **El audio grabado sigue al reloj de pared.** En loopback una pausa no
  entrega silencio, no entrega nada; sin rellenar el hueco, todo lo posterior
  quedaría adelantado respecto a las teclas.
- `Keyla.exe --analyse-session <carpeta>` reanaliza sin ventana: es lo que
  permite probar otros ajustes sobre la misma canción sin volver a tocarla.
- Las grabaciones viven en `%APPDATA%\Keyla\sesiones` y **no van a git ni a
  `tests/`**: son canciones con derechos.

**Primer ajuste con música real.** Sesión grabada tocando *Una y mil veces*
(Los Forasteros) siguiendo Chordify: 4 min 32 s, cumbia con acordeón, voz y
bajo. La grabación tiene errores y trozos tocados por diversión, y **sirve
igual**: esos errores bajan la nota de todos los ajustes por igual, así que la
comparación entre ajustes sigue siendo válida aunque el porcentaje absoluto no
lo sea.

| | Fundamental | Tipo | Tonalidad |
|---|---|---|---|
| Antes | 42 % | 33 % | La mayor (mal) |
| **Ahora** | **54 %** | **53 %** | **Re mayor** |

Lo que se aprendió, y que conviene no desaprender:

- **Castigar las lecturas raras es lo que más aporta.** Los 25 mejores de 288
  ajustes castigaban sus/dim/aug/6; los 5 peores no castigaban nada. La voz y
  el acordeón meten notas de melodía que encajan en plantillas de cuatro notas.
  Van en dos niveles (`complexQualityPenalty` 0,30 y `seventhQualityPenalty`
  0,05) por lo que viene a continuación.
- **Se descartó el ajuste que mejor puntuaba en la canción (57 %).** Castigaba
  las séptimas con 0,10, y en una séptima limpia la cuarta nota sólo le saca
  ~0,11 a la tríada: el test de 48 acordes limpios bajaba del 100 % al 75 % en
  tipo. En esa cumbia no se tocó ninguna séptima, así que castigarlas sólo podía
  ayudar *ahí*. Eso es aprenderse una canción, no mejorar. **El test de acordes
  limpios es el guardián contra esto** y se tiene que correr en cada ajuste.
- **La tonalidad sale de los acordes, no de las notas.** Sumar notas y comparar
  con perfiles de Krumhansl daba La mayor en una canción en Re: el quinto grado
  suena casi la mitad del tiempo en cumbia y arrastra el resultado. Ahora se
  mira en qué tonalidad encajan los acordes reconocidos. Las relativas —que
  comparten todos sus acordes— se desempatan por tónica y por **dominante**:
  Do-Sol-Lam-Fa es Do mayor porque el Sol está y el Mi no.
- Agudizar menos (1,5 en vez de 2) va mejor en mezclas: con 2 se hundía la
  tercera, que en una mezcla suele sonar más floja que la fundamental.

**Segunda canción: el ajuste generaliza.** *Algo de mí*, bolero, 4 min 53 s: lo
contrario de la cumbia —séptimas por todas partes, más cambios, voz delante—.
El ajuste sacado **sólo** de la cumbia, sin tocarlo:

| | Acorde | Tipo | Tonalidad |
|---|---|---|---|
| Cumbia | 54 % | 53 % | Re mayor ✓ |
| Bolero | 77 % | 58 % | Si♭ mayor ✓ |

`keyla_session tune-all` compara 270 ajustes sobre las dos canciones a la vez y
descalifica los que bajen del 90 % en los 48 acordes limpios. El actual queda
a 1-2 puntos del mejor que no los rompe (67 % / 57 %), y **no se cambió**: esa
diferencia es menor que el ruido de las propias grabaciones. Sólo los Dm que
el autor tocó como D en el bolero son 8,5 s de 290, un 3 %. Cambiar el ajuste
por menos que eso es perseguir los errores de quien toca.

Dos cosas que enseñó el bolero:

- **El informe también encuentra los errores del que toca.** "Tocaste D y
  Keyla oyó Dm" durante 8,5 s eran Dm tocados como D: ahí Keyla tenía razón.
  Cuenta como fallo suyo en el porcentaje y no lo es.
- **Lo que más resta en el bolero son las séptimas** (A#maj7 oído como A#,
  14,6 s). Ningún valor de las perillas lo arregla sin romper los acordes
  limpios o la cumbia. Puede que en la grabación la séptima sea débil y la
  añada quien toca; no hay forma de saberlo desde aquí.

Grabar sesiones cortas no sirve para ajustar: `tune-all` deja fuera las de
menos de dos minutos, que suelen ser arranques en falso y pesarían en la media
lo mismo que una canción entera.

**El bajo, rehecho.** El método antiguo tomaba como bajo la nota más grave que
llegara al 30 % de la más fuerte de todo el espectro, y en una mezcla fallaba
por tres lados a la vez: la voz lo dejaba por debajo del umbral, el bajo de
cumbia alterna fundamental y quinta, y en muchas mezclas los armónicos del bajo
suenan más que su nota. Ahora (`bassSalience`) se mira sólo la zona grave con
su propia escala, cada nota se suma con sus armónicos y lo que sale tiene
memoria.

| | Media | Cumbia | Bolero | Limpios | Inversiones |
|---|---|---|---|---|---|
| Método antiguo | 66 % / 55 % | 54 / 53 | 77 / 58 | 100 % | 100 % |
| **Bajo nuevo** | **71 % / 56 %** | **59 / 55** | **83 / 57** | **100 %** | **100 %** |

Cuatro trampas por el camino, las cuatro cazadas antes de dar números:

- **Sumar armónicos sin límite convertía la voz en un bajo fantasma.** Un Do5
  cantado es exactamente el tercer armónico de un Fa3. Cada armónico aporta
  ahora como mucho el doble de lo que suena la propia nota: pueden reforzar una
  nota que suena, no inventar una que no suena.
- **El guardián de acordes limpios no podía ver el riesgo principal.** Todos
  llevan la fundamental en el bajo, así que un bajo fuerte siempre les ayuda. Se
  añadió un segundo guardián con 48 inversiones, y el ajuste que mejor puntuaba
  en las canciones (72 %) resultó tener **0 %** en ellas: llamaba Mi menor a
  todos los C/E. Toda subida de peso del bajo por encima de 0,08 las hundía.
- **Eso no se arreglaba con perillas, era un defecto de modelo:** el bajo
  sumaba puntos a candidatos que no encajaban. Ahora `bassFitGate` escala el
  premio por la nota más floja del candidato — el bajo decide entre lecturas que
  encajan, no convierte en acorde algo a lo que le faltan notas. Con eso los
  mismos seis puntos salen con las inversiones al 100 %.
- **La memoria del bajo se deshacía sola.** Se reescalaba por su propio máximo
  en cada fotograma y un bajo apagado volvía al 100 %: una progresión salía
  `C G/C Am/C F/C`. Los tests sólo miraban la fundamental y no lo vieron; ahora
  hay uno que exige que no aparezcan barras inventadas. Y la barra del cifrado
  sólo se escribe si el bajo **suena ahora**, aunque la decisión use la memoria.

Se eligió peso 0,40 con encaje 0,35 y no el primero de la tabla (0,25/0,20):
puntúan igual en las canciones, y el primero exige más encaje, que es lo
prudente ante música que no se ha probado.

Lo que sigue sin resolver: los tipos de acorde (el tipo apenas mejora, 55 → 56,
porque el bajo decide la fundamental y no si es mayor o menor) y las séptimas
del bolero. Lo siguiente que movería eso sería un suavizado temporal que conozca
qué cambios de acorde son probables en una tonalidad.

**`keyla_session`** (src/tools/session_tool/): analiza y ajusta sesiones grabadas
desde consola. Existe aparte porque enlazar `Keyla.exe` exige cerrarla, y el
análisis no tiene nada que ver con la ventana. `keyla_session tune` prueba
cientos de ajustes en segundos porque el análisis de Q constante —la parte
cara— se calcula una vez (`computeChromaFrames`) y sólo se repite la decisión.
**Antes de fiarse de un informe, comprobar que el ejecutable es más nuevo que
el código:** en la primera tirada salió un 57 % de un binario que aún tenía el
ajuste anterior.

Lo que falta para cerrar de verdad:

- **El calibrador de loopback en la app.** El offset perceptual (invariante 7,
  doc 04 §6) está implementado y persistido, pero vale cero mientras nadie lo
  mida. Con él a cero, el informe de ritmo arrastra la latencia del sistema como
  si fuera del alumno. `audio_probe --calibrate` ya sabe medirlo; falta traerlo.
- Persistencia de sesiones y exportación a SMF (doc 05 fase 2).
- Reproducción de lo grabado.
- Calibración de la curva de velocity (doc 02 §4.3): sin ella, decir "tocas
  demasiado fuerte" es una afirmación sobre el teclado, no sobre el alumno.

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
- **Para escuchar, el predeterminado de Windows aquí no sirve.** Medido con
  `Keyla.exe --listen-test`: `Sonar - Chat` es el endpoint predeterminado tanto
  para `eConsole` como para `eMultimedia`, y en loopback **no entrega ni un
  sample**. `Sonar - Gaming` y el H510-PRO físico sí, y con los dos se saca la
  progresión de prueba entera. Ojo al contraste con la nota de arriba: para
  *reproducir*, Gaming no daba callbacks y Chat sí; para *escuchar* es al revés.
  Son dispositivos virtuales y no se comportan igual en las dos direcciones, así
  que de ninguno se puede deducir el otro.
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
