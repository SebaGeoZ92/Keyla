#pragma once

#include <algorithm>
#include <cmath>

namespace keyla::core
{

/** Trémolo: el volumen subiendo y bajando solo, varias veces por segundo.

    Es el efecto del piano eléctrico de toda la vida y del vibráfono, y cuesta
    una multiplicación por sample.

    **Los dos canales no van exactamente a la vez.** Un trémolo de verdad de
    Rhodes es un paneo: el sonido se va a un lado y al otro, y los dos canales
    van justo al revés el uno del otro. Eso suena muy bien en estéreo y tiene un
    problema serio: si la salida acaba sumándose a mono —una barra de sonido en
    modo mono, un altavoz de portátil, un Bluetooth barato— los dos canales se
    cancelan, la suma queda **constante** y el efecto desaparece del todo. El
    usuario movería el mando y no pasaría nada.

    Así que aquí los canales van desfasados sólo un tercio de ciclo: hay
    movimiento estéreo de verdad, y la suma en mono sigue subiendo y bajando. Se
    oye en cualquier salida, que es la condición para que el mando sirva.

    La profundidad se interpola: pasar de 0 a 1 de golpe se oye como un
    chasquido, y el mando de un teclado manda saltos de 1/127.
*/
class Tremolo
{
public:
    /** Ni tan lento que parezca que algo va mal, ni tan rápido que suene a
        motor. 5,2 Hz es el terreno clásico de los pianos eléctricos. */
    static constexpr double defaultRateHz = 5.2;

    /** A profundidad máxima el volumen no llega a cero: un trémolo que corta
        del todo suena a cable suelto, no a instrumento. */
    static constexpr float maxModulation = 0.85f;

    void prepare (double sampleRateToUse) noexcept
    {
        sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;
        phase = 0.0;
        currentDepth = targetDepth;
        setRate (rateHz);
    }

    void setRate (double hertz) noexcept
    {
        rateHz = std::clamp (hertz, 0.1, 20.0);
        increment = kTwoPi * rateHz / sampleRate;
    }

    /** 0 = apagado, 1 = todo lo que se permite. */
    void setDepth (float depth) noexcept { targetDepth = std::clamp (depth, 0.0f, 1.0f); }

    float depth() const noexcept { return targetDepth; }

    /** Si está apagado no toca nada y no gasta nada: el caso normal es que el
        mando esté a cero, y no tiene sentido pagar un seno por sample por un
        efecto que nadie ha pedido. */
    bool isActive() const noexcept { return targetDepth > 0.0005f || currentDepth > 0.0005f; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (! isActive() || left == nullptr || numSamples <= 0)
        {
            // La fase sigue corriendo aunque esté apagado, para que volver a
            // subir el mando no dé un salto desde donde se quedó hace un rato.
            phase += increment * numSamples;
            phase -= kTwoPi * std::floor (phase / kTwoPi);
            return;
        }

        const float step = (targetDepth - currentDepth) / static_cast<float> (numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            currentDepth += step;

            const auto modulation = static_cast<float> (currentDepth) * maxModulation;

            const float leftGain  = 1.0f - modulation * (0.5f + 0.5f * static_cast<float> (std::sin (phase)));
            const float rightGain = 1.0f - modulation * (0.5f + 0.5f * static_cast<float> (std::sin (phase + stereoOffset)));

            left[i] *= leftGain;

            if (right != nullptr)
                right[i] *= rightGain;

            phase += increment;

            if (phase >= kTwoPi)
                phase -= kTwoPi;
        }

        currentDepth = targetDepth;
    }

private:
    static constexpr double kTwoPi = 6.283185307179586476925286766559;

    /** Un tercio de ciclo. Ver la explicación de arriba: medio ciclo sonaría
        mejor en estéreo y desaparecería en mono. */
    static constexpr double stereoOffset = kTwoPi / 3.0;

    double sampleRate { 48000.0 };
    double rateHz { defaultRateHz };
    double increment { kTwoPi * defaultRateHz / 48000.0 };
    double phase { 0.0 };

    float targetDepth { 0.0f };
    float currentDepth { 0.0f };
};

} // namespace keyla::core
