#pragma once

#include <algorithm>
#include <cmath>

namespace keyla::core
{

/** Limitador de pico. Va desde el día uno y no es negociable (doc 02 §2): un
    principiante con auriculares y un instrumento sin control de ganancia puede
    hacerse daño de verdad.

    Sin *lookahead* a propósito — retrasar la señal para anticipar el pico sería
    añadir latencia al único camino donde no sobra ni un milisegundo. A cambio,
    el ataque no es instantáneo y deja pasar el primer pico. Por eso hay además
    un recorte duro al final: el limitador se ocupa de que no suene mal, el
    recorte de que nada salga por encima de fondo de escala pase lo que pase.
*/
class Limiter
{
public:
    void prepare (double sampleRateToUse) noexcept
    {
        sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;

        attackCoef  = std::exp (-1.0 / (0.001 * sampleRate));   // 1 ms
        releaseCoef = std::exp (-1.0 / (0.100 * sampleRate));   // 100 ms
        envelope = 0.0;
    }

    void setThreshold (float linearThreshold) noexcept
    {
        threshold = std::clamp (linearThreshold, 0.01f, 1.0f);
    }

    void reset() noexcept { envelope = 0.0; }

    /** Procesa en sitio. `right` puede ser nullptr (mono).

        Los dos canales comparten reducción de ganancia: si cada uno llevara la
        suya, la imagen estéreo se movería al limitar. */
    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right != nullptr ? right[i] : l;
            const double peak = std::max (std::abs (l), std::abs (r));

            // Sube rápido, baja despacio: así no "bombea" con cada nota.
            const double coef = peak > envelope ? attackCoef : releaseCoef;
            envelope = peak + coef * (envelope - peak);

            const double gain = envelope > threshold ? threshold / envelope : 1.0;

            left[i] = static_cast<float> (l * gain);

            if (right != nullptr)
                right[i] = static_cast<float> (r * gain);
        }
    }

    /** Reducción aplicada ahora mismo, 0..1. Para el panel de salud. */
    float currentGainReduction() const noexcept
    {
        return envelope > threshold ? static_cast<float> (threshold / envelope) : 1.0f;
    }

private:
    double sampleRate { 48000.0 };
    double attackCoef { 0.0 };
    double releaseCoef { 0.0 };
    double envelope { 0.0 };
    double threshold { 0.891 };     // −1 dBFS
};

} // namespace keyla::core
