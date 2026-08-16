# 04 — Cómo medimos la latencia objetivamente

Responde al punto 14.10, que es la pregunta más importante del proyecto: sin una
medición objetiva, "baja latencia" es una opinión.

---

## 1. Descomposición de la cadena

Lo que sientes al tocar es la suma de:

```
  tecla pulsada
    │  (1) escaneo de matriz del SE49                  ~1–3 ms   no medible, no controlable
    ▼
  mensaje MIDI generado
    │  (2) transporte USB-MIDI (tramas de 1 ms)        ~1–2 ms   medible como jitter
    ▼
  driver de Windows → callback MIDI
    │  (3) espera hasta el siguiente callback de audio 0–B ms    B = tamaño de buffer
    ▼
  render en el hilo de audio
    │  (4) buffering del driver                        1–2 × B   controlable
    ▼
  DAC + amplificación
    │  (5) conversión + salida analógica               ~1–2 ms   reportada por el driver
    ▼
  sonido en el auricular
```

**(1) no es medible desde software.** Cualquier cifra "MIDI→sonido" que dé la app
excluye el escaneo del teclado. Hay que ser honestos en la UI: llamarlo
*latencia del sistema*, no *latencia percibida*.

Los tres métodos siguientes miden cosas distintas y se complementan.

---

## 2. Método A — Latencia reportada (barato, aproximado, siempre disponible)

Sumar lo que declaran los drivers:

```
latencia_reportada = outputLatencySamples / sampleRate
                   + bufferSize / sampleRate
```

`AudioIODevice::getOutputLatencyInSamples()` da la parte del driver.

- **Ventaja**: instantánea, sin hardware extra. Es lo que se muestra siempre en
  el panel de salud.
- **Límite**: **los drivers mienten**, sobre todo los integrados. Sirve como
  estimación y para detectar configuraciones absurdas, no como verdad.

---

## 3. Método B — Loopback físico (verdad absoluta del lado de audio)

**El método de referencia.** Un cable de 3,5 mm de la salida de auriculares a la
entrada de micrófono/línea del PC.

Procedimiento (implementado en `tools/latency_probe/`):

1. Abrir el stream con entrada y salida a la vez.
2. Emitir un impulso (o mejor, un *chirp* corto, más robusto frente a ruido) en
   una posición de sample conocida `S_out`.
3. Correlacionar la señal de entrada con la emitida y localizar el pico:
   `S_in`.
4. `round_trip_samples = S_in - S_out`.
5. Repetir 20–50 veces: reportar mediana y dispersión. Si la dispersión es alta,
   el sistema tiene un problema de estabilidad, no de latencia.

Esto da **round-trip = salida + entrada**, exacto, con precisión de un sample
(0,02 ms) y sin creerle nada al driver.

Para separar la parte de salida, que es la que nos interesa: se hace una segunda
medición cambiando sólo el tamaño de buffer y se resuelve el sistema, o se usa la
proporción declarada in/out como reparto. En la práctica, para un dispositivo
integrado simétrico, **salida ≈ round-trip / 2** es suficientemente bueno, y la
cifra que importa (§4) se calibra de todos modos de forma conjunta.

---

## 4. Método C — Calibración MIDI→sonido de extremo a extremo (la que se usa)

Es la cifra que importa y se obtiene combinando A, B y una medición del lado MIDI.

Dentro de la app, para cada nota tocada conocemos:
- `t_midi_exact`: posición de sample exacta en la que llegó el mensaje (doc 02 §3).
- El sample en que empieza a sonar: `inicio_bloque_actual`.
- La latencia de salida medida con el método B.

```
latencia_sistema = (inicio_bloque - t_midi_exact) / sampleRate    ← espera al buffer
                 + latencia_salida_medida                          ← método B
```

Se acumula sobre muchas notas y se reporta **la distribución, no la media**:
`p50 / p95 / máximo`. La media es engañosa; lo que arruina la sensación de tocar
es el máximo.

### Validación externa (una vez, para confirmar que no nos engañamos)

Merece la pena hacerlo una sola vez, porque valida todo lo anterior:

- Grabar con un móvil (o con otra entrada del PC) **dos canales**: el clic
  mecánico de la tecla del SE49 y la salida de audio de la app.
- Medir en el editor la distancia entre ambos transitorios.
- Eso incluye el escaneo del teclado (el término (1) que el software no ve).

Si el número coincide con `latencia_sistema + 2 ms`, el modelo es correcto y a
partir de ahí basta la medición interna.

---

## 5. Lo que hay que medir además de la latencia

La latencia media es la mitad de la historia. Estas métricas explican por qué a
veces "se siente raro" con la misma configuración:

| Métrica | Cómo se obtiene | Umbral aceptable |
|---|---|---|
| **Dropouts (XRuns)** | contador en el callback: discontinuidades en el reloj del stream | **0** en 10 min. Cualquier otro valor es un fallo. |
| **Jitter del callback** | σ del intervalo real entre callbacks | < 15% del periodo de buffer |
| **Carga de CPU del hilo de audio** | tiempo de callback / periodo de buffer | < 50% con polifonía plena |
| **Jitter de entrada MIDI** | σ de los deltas de timestamp con notas repetidas | ~1–2 ms es el suelo del USB-MIDI |
| **Salida Bluetooth** | consultar el tipo de endpoint | Detectarlo y **avisar** |

Un sistema con 5 ms de latencia y un dropout por minuto es peor que uno con 10 ms
y ninguno. Se optimiza en ese orden: **primero cero dropouts, después latencia**.

---

## 6. La constante de calibración de la evaluación

De todo lo anterior sale un único número que consume el motor de evaluación:

```
offset_evaluacion = latencia_salida_audio + latencia_entrada_midi
```

Que es lo que se resta a todo error de timing (doc 01 §1.4, doc 02 §3). Se
recalcula al cambiar de dispositivo o de buffer, y **se persiste por
configuración de dispositivo**.

Sin esta constante, la evaluación rítmica está sesgada. Con ella, es correcta.

---

## 7. Panel de salud de audio

Una pantalla, detrás de un botón, nunca en la principal:

```
Dispositivo   Altavoces (Realtek)  ·  WASAPI Exclusive
Formato       48 000 Hz  ·  buffer 128 samples

Latencia del sistema      7,3 ms      (p95: 8,1  ·  máx: 9,4)
  ├ espera de buffer      2,7 ms
  └ salida (medida)       4,6 ms

Dropouts                  0           últimos 10 min
Carga del hilo de audio   18 %
Jitter MIDI               1,1 ms

  [ Calibrar con cable de loopback ]   [ Modo tutorial (audio compartido) ]
```

Con el criterio de tu punto 13 (esconder la complejidad): la pantalla principal
sólo muestra un indicador verde/ámbar/rojo. Los números están aquí, para cuando
haga falta.

---

## 8. Criterios de aceptación numéricos

Estos son los que deciden si la fase 0 pasa o no. Sin ambigüedad:

| Criterio | Objetivo | Mínimo aceptable |
|---|---|---|
| Latencia del sistema (p50) | ≤ 8 ms | ≤ 12 ms |
| Latencia del sistema (máx) | ≤ 12 ms | ≤ 20 ms |
| Dropouts en 10 min de ejecución continua | 0 | 0 |
| Carga de CPU del hilo de audio, 32 voces | < 30 % | < 60 % |
| Jitter de entrada MIDI (σ) | < 1,5 ms | < 3 ms |

Si con WASAPI Exclusive y el audio integrado se cumple la columna "objetivo"
—y creo que se cumplirá— queda demostrado que **no hace falta interfaz de audio**,
que era tu pregunta del punto 4.
