#pragma once

#include "../time/Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

namespace keyla::core
{

/** El metrónomo, generado en el hilo de audio y **calculado, no programado**.

    Ésta es la diferencia que justifica todo el modelo temporal del doc 02 §3:
    el pulso N no se programa con un temporizador que se vuelve a armar cada
    vez, sino que **está** en `barZeroSample + N · (60/bpm) · sampleRate`. No hay
    acumulación de error porque no hay acumulación. Un metrónomo que se reprograma
    deriva unos milisegundos por minuto, y en una sesión de práctica de diez
    minutos esa deriva se mide — y se le achacaría al alumno.

    El clic acentúa el primer pulso del compás, que es lo que permite saber
    dónde estás sin contar.
*/
class Metronome
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
    bool isEnabled() const noexcept { return enabled; }

    void setGain (float newGain) noexcept { gain = juce::jlimit (0.0f, 1.0f, newGain); }
    float currentGain() const noexcept { return gain; }

    /** Suma los clics que caen dentro de este bloque. Se llama en el hilo de
        audio: no asigna, no bloquea (invariante 1).

        `transport` se consulta pero no se modifica: quien avanza el reloj es el
        host, una sola vez por bloque. */
    void process (juce::AudioBuffer<float>& output,
                  std::uint64_t blockStartSample,
                  const Transport& transport) noexcept;

    /** Índice del último pulso emitido, para que la pantalla pueda enseñar el
        compás y el tiempo sin volver a calcularlo. */
    std::int64_t lastPulseIndex() const noexcept { return lastPulse; }
    bool lastPulseWasDownbeat() const noexcept { return lastWasDownbeat; }

private:
    void triggerClick (bool accented) noexcept;
    void renderInto (float* left, float* right, int startSample, int numSamples) noexcept;

    double sampleRate { 48000.0 };
    bool enabled { false };
    float gain { 0.5f };

    // Voz del clic: dos senoides con caída muy rápida. Suficiente para un clic
    // seco y audible por encima de un piano, y no cuesta nada.
    double phase { 0.0 };
    double phaseInc { 0.0 };
    double envelope { 0.0 };
    double envelopeDecay { 0.0 };
    double amplitude { 0.0 };

    std::int64_t lastPulse { -1 };
    bool lastWasDownbeat { false };
};

} // namespace keyla::core
