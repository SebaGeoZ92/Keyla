# 01 — Crítica de la visión y riesgos técnicos

Este documento responde a tu punto 3 ("cuestiona mis ideas") y al punto 14.1–14.3.
No es un "sí a todo": aquí está lo que creo que está mal planteado, lo que es
más difícil de lo que parece y lo que es más fácil de lo que crees.

---

## 1. Errores conceptuales en el planteamiento

### 1.1 "Detectar el Nektar SE49"

**No lo hagas.** El SE49 es un dispositivo USB-MIDI *class compliant*: Windows lo
expone como un puerto MIDI genérico y no hay nada específico del modelo que
podamos aprovechar. Escribir código que "detecte el SE49" es acoplar el producto a
un hardware concreto sin ganar nada.

Lo correcto: enumerar todos los puertos MIDI de entrada, dejar que el usuario
elija (o auto-seleccionar el único disponible), **recordar el nombre del puerto**
y reconectar automáticamente cuando reaparezca. Eso resuelve tu caso de uso real
(abrir la app y tocar) y funciona igual con cualquier teclado futuro.

Lo único específico del SE49 que sí merece código es genérico por diseño:

- **Rango de teclado limitado (49 teclas, 4 octavas).** Muchos ejercicios y
  prácticamente cualquier canción real no caben. La app debe conocer el rango
  útil del controlador (aprendido observando lo que tocas, o configurado) y, si
  un ejercicio se sale, **transponer u avisar** en lugar de marcarte notas
  imposibles como "omitidas". Esto no lo suele hacer nadie y en tu caso concreto
  es la diferencia entre que la app sirva o no.
- **Acción sintetizador, no contrapesada.** Puedes entrenar notas, ritmo y
  lectura perfectamente, pero el entrenamiento de *dinámica* transfiere sólo
  parcialmente a un piano acústico o de martillos. No es un problema para el
  proyecto, pero conviene no vender "evaluación de dinámica" como si midiera
  técnica pianística real.
- **Pedal de sustain.** Revisa si tu unidad trae jack de footswitch. Si no lo
  trae, ese pedal (unos 20–25 €) es de lejos la compra de hardware con mejor
  retorno para este proyecto — muy por delante de una interfaz de audio. El
  pedal es parte de la técnica de piano desde el primer mes; una interfaz de
  audio, como verás en el doc 03, probablemente no la necesites nunca.

### 1.2 "Baja latencia" no es un número, son tres

Estás tratando la latencia como una magnitud única. Son tres cadenas
independientes con requisitos muy distintos:

| Cadena | Objetivo | Comentario |
|---|---|---|
| MIDI-in → audio-out | **< 10 ms** | Es la única que afecta a la sensación de tocar. |
| MIDI-in → feedback visual | 30–60 ms está bien | El monitor va a 60 Hz: 16,7 ms por frame + latencia del panel. Intentar bajar de ahí es tirar esfuerzo. |
| Evaluación / medición | **precisión, no baja latencia** | Puede tardar 200 ms en calcular. Lo que no puede es medir mal. |

La consecuencia arquitectónica es importante: **no acoples el teclado visual al
hilo de audio**. La UI debe leer una instantánea del estado, no ser notificada
desde el callback. Mezclarlas es la causa nº 1 de dropouts en aplicaciones de
audio amateur.

### 1.3 "Timestamps de alta precisión" — sí, pero el reloj correcto no es el del sistema

Aciertas en no fiarte del momento en que la UI recibe el evento. Pero la
solución habitual (usar `QueryPerformanceCounter` y comparar contra el reloj del
sistema) sigue siendo incorrecta para este caso.

El reloj maestro tiene que ser **el reloj de la tarjeta de audio**, no el del
sistema operativo. Razón: el metrónomo que oyes se genera con el reloj de audio,
y ese reloj deriva respecto al del sistema (decenas de ppm — varios ms por
minuto). Si mides tu ejecución contra el reloj del sistema y el clic contra el
de audio, tus errores de timing tendrán una deriva lenta que no es tuya, es del
hardware. En una sesión de 10 minutos eso es perfectamente medible.

El modelo correcto está detallado en el doc 02 §3. En una frase: **todo evento
se convierte a una posición en el contador de samples del stream de audio, y
toda la evaluación ocurre en ese dominio.**

### 1.4 El error que arruina la evaluación rítmica: el offset perceptual

Este es sutil y lo implementa mal casi todo el mundo.

Tú oyes el clic del metrónomo **retrasado** por la latencia de salida de audio
(digamos 6 ms). Cuando tocas "perfectamente a tiempo con lo que oyes", tu
pulsación llega al programa **retrasada** por la latencia de entrada MIDI
(1–3 ms). Si comparas ingenuamente la posición de tu nota contra la posición
teórica del pulso, el resultado es que **todo alumno aparece sistemáticamente
tarde** por una constante de ~8–10 ms que no tiene nada que ver con su ritmo.

Hay que **restar ese offset constante** (medido, no adivinado — ver doc 04)
antes de reportar cualquier error de timing. Sin esta corrección, la función
estrella del producto miente.

### 1.5 Evaluar nota a nota con ✓/✗ es mala pedagogía

Tu punto 6 dice explícitamente "no quiero una evaluación ingenua". Estoy de
acuerdo, y va más allá del ritmo: **mostrar un ✗ rojo en tiempo real cada vez que
fallas es contraproducente**. Rompe la concentración, incentiva parar y repetir
compás a compás (el peor hábito de práctica que existe) y en un principiante
genera ansiedad de ejecución.

Lo que hace un profesor real: te deja terminar la frase y luego comenta. Propuesta:

- **Durante la ejecución**: feedback mínimo y no valorativo — sólo *dónde estás*
  (posición en el ejercicio) y, como mucho, un realce suave cuando te pierdes.
- **Al terminar el intento**: informe detallado, con la línea de tiempo
  navegable y las métricas.

Esto además simplifica el motor: el análisis fino se hace *offline* sobre la
grabación, sin restricciones de tiempo real (ver §2.2).

### 1.6 La ingeniería es el 40% del producto

El "profesor de piano digital" no falla por no detectar las notas — detectar
notas es fácil. Falla por:

- no tener un **currículum** coherente (qué ejercicio toca ahora y por qué);
- dar feedback **verdadero pero inútil** ("tu error medio es 23 ms" no le dice
  nada a nadie);
- no saber **cuándo subir el tempo** o cuándo insistir.

Y hay cosas que el MIDI **no puede** evaluar y conviene no prometer: posición de
la mano, postura, independencia de dedos, digitación real, producción de sonido,
matices de pedal. Un teclado MIDI ve *qué* tecla, *cuándo* y *con cuánta fuerza*.
Nada más.

Esto no invalida el proyecto — hay muchísimo valor en notas, ritmo, regularidad y
constancia — pero conviene tenerlo escrito desde el día 1.

### 1.7 La partitura renderizada es un subsistema enorme, no una pantalla

Tu fase 4 ("partituras/canciones") mezcla dos cosas de coste radicalmente
distinto:

- **Ejecutar** una canción (leer un MIDI, saber qué notas se esperan cuándo):
  barato, un par de días.
- **Grabar/renderizar notación musical** (pentagramas, plicas, ligaduras,
  agrupación de corcheas, alteraciones, dos manos, saltos de sistema): es uno de
  los problemas clásicamente difíciles de la informática musical. Hacerlo bien
  desde cero son meses.

Recomendación: **notación real muy al final**, y mientras tanto usar
visualización de "notas cayendo" (estilo piano-roll). No es una versión pobre de
la partitura: para un principiante es *mejor*, porque el mapeo nota→tecla es
directo y no exige saber leer antes de poder tocar. La partitura llega cuando el
objetivo explícito sea *aprender a leer*, que es una fase pedagógica distinta.

### 1.8 Orden equivocado: la grabación no va al final

En tu hoja de ruta el motor de grabación aparece tarde. Pero:

> **Evaluar es analizar una grabación.**

Si el motor de grabación existe primero, el motor de evaluación se convierte en
una función pura `(grabación, partitura esperada) → informe`, que es *testeable
sin hardware, sin audio y sin UI*. Si lo dejas para el final, acabas escribiendo
la evaluación acoplada al flujo en vivo y no puedes testearla.

La grabación tiene que estar en la **fase 2**, antes que cualquier evaluación.

---

## 2. Riesgos técnicos

### 2.1 Riesgos de audio/latencia (los serios)

| Riesgo | Impacto | Mitigación |
|---|---|---|
| **Auriculares Bluetooth** | 100–300 ms. Inutiliza el producto por completo. | Detectar si el endpoint de salida es Bluetooth y **avisar de forma prominente**. Es el fallo de usuario nº 1 y ninguna app lo avisa. |
| **ASIO no es redistribuible** | Legal, no técnico. El SDK de Steinberg no se puede redistribuir y distribuir un producto con ASIO requiere firmar su acuerdo. JUCE no incluye el SDK. | Ver doc 03 §2. Resumen: **WASAPI Exclusive por defecto**, ASIO como build opcional. |
| **ASIO4ALL no se puede empaquetar** | Es un wrapper sobre WDM/KS, freeware con restricciones de redistribución. | No dependemos de él. Si el usuario ya lo tiene, funcionará; no lo asumimos. |
| **WASAPI Exclusive toma el dispositivo en exclusiva** | Nada más puede sonar. Y aprender piano implica ver tutoriales en YouTube *a la vez*. | Modo dual: Exclusive para practicar (mínima latencia), Shared para "modo tutorial". Conmutable en caliente y explicado en lenguaje humano. |
| **Plan de energía / core parking de Windows** | Dropouts intermitentes imposibles de diagnosticar. | El panel de salud detecta dropouts y sugiere el plan "Alto rendimiento". |
| **Antivirus / apps en segundo plano** | Picos de latencia esporádicos. | Medir jitter y **contar dropouts**; reportar p95 y máximo, no sólo la media. |
| **Hub USB compartido** | Jitter en el MIDI. | Documentar; el panel de salud puede mostrar el jitter de entrada MIDI. |

### 2.2 Riesgos del motor de evaluación

**El riesgo real no es clasificar, es alinear.** Tu lista ("nota correcta /
incorrecta / anticipada / atrasada / mantenida de más...") son *etiquetas que se
asignan después* de decidir a qué nota esperada corresponde cada nota tocada.

El enfoque ingenuo (índice contra índice) se rompe en cuanto el alumno omite una
nota o añade una: a partir de ahí *todo* sale mal y el informe es basura. Esto
pasa en el primer minuto de uso real.

Es un problema de alineación de secuencias (distancia de edición / DTW).
Solución en dos pasadas — ver doc 02 §5:

1. **En vivo**: matcher incremental con ventana deslizante. Barato, tolerante,
   sólo para saber dónde está el usuario.
2. **Al terminar**: alineación óptima sobre la grabación completa. Sin prisa,
   correcta, y es la que genera el informe.

Riesgos secundarios pero reales:

- **Acordes**: notas simultáneas nunca son simultáneas. Hay que agrupar en
  "eventos" con tolerancia (~50 ms) o un acorde arpegiado se reporta como tres
  errores de timing.
- **Note-off / duración**: mucho más ruidoso que el onset y perceptualmente
  menos importante. Evaluarlo con criterios *mucho* más laxos, y sólo cuando el
  ejercicio lo pida explícitamente (legato/staccato).
- **Curva de velocity sin calibrar**: evaluar "demasiado fuerte / demasiado
  suave" sin conocer la curva de tu controlador no significa nada. Hace falta
  una calibración (doc 02 §4.3).

### 2.3 Riesgos de contenido y licencias

- **Samples de piano**: un piano multicapa decente son cientos de MB. Hay que
  elegir licencia compatible y decidir empaquetado vs descarga.
- **Repertorio**: casi todo lo que quieras tocar está bajo copyright. El
  currículum propio (escalas, arpegios, ejercicios) y el dominio público son el
  camino.
- **Licencia de JUCE**: GPLv3 o licencia comercial, con un tier gratuito sujeto a
  umbral de facturación. Verifica los términos vigentes antes de distribuir.

### 2.4 Riesgo de producto (el más probable)

El fracaso más probable de este proyecto no es técnico: es acabar con un
instrumento virtual excelente y **tres ejercicios**. El motor es divertido de
construir; el currículum no. Mitigación concreta: en cuanto exista el motor de
ejercicios, la prioridad pasa a ser **importar MIDI → ejercicio automáticamente**,
para que el contenido no dependa de escribirlo a mano.

---

## 3. Lo que sí está bien planteado

Para no ser sólo crítica:

- Aplicación independiente en lugar de plugin: **correcto**. Un VST no puede
  poseer el dispositivo de audio ni gestionar la sesión de aprendizaje.
- La separación de motores del punto 10: **la intuición es buena**, aunque el eje
  de corte correcto es otro (doc 02 §1).
- "La UI nunca debe bloquear el hilo de audio": **es la regla correcta** y la más
  importante del proyecto.
- No asumir que hace falta interfaz de audio: **acertado**, y probablemente
  tengas razón (doc 03 §2).
- Priorizar funcionalidad sobre realismo en el sonido inicial: **acertado**.
- Empezar por el spike MIDI→audio: **exactamente el orden correcto**.
