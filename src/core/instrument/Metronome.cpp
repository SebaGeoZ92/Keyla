#include "Metronome.h"

#include <algorithm>
#include <cmath>

namespace keyla::core
{

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;

    // El acento va una quinta por encima y dura un pelín más. Distinguirlos por
    // altura y no sólo por volumen es lo que permite oír el compás cuando el
    // metrónomo está bajo, debajo de lo que estás tocando.
    constexpr double accentHz = 1800.0;
    constexpr double normalHz = 1200.0;
    constexpr double accentSeconds = 0.040;
    constexpr double normalSeconds = 0.028;
}

void Metronome::prepare (double sampleRateToUse) noexcept
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;
    reset();
}

void Metronome::reset() noexcept
{
    phase = 0.0;
    phaseInc = 0.0;
    envelope = 0.0;
    envelopeDecay = 0.0;
    amplitude = 0.0;
    lastPulse = -1;
    lastWasDownbeat = false;
}

void Metronome::triggerClick (bool accented) noexcept
{
    phase = 0.0;
    phaseInc = twoPi * (accented ? accentHz : normalHz) / sampleRate;
    envelope = 1.0;
    envelopeDecay = std::exp (-1.0 / ((accented ? accentSeconds : normalSeconds) * sampleRate));
    amplitude = accented ? 0.55 : 0.34;
}

void Metronome::renderInto (float* left, float* right, int startSample, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (envelope < 1.0e-4)
            return;

        const auto value = static_cast<float> (std::sin (phase) * envelope * amplitude * gain);

        left[startSample + i] += value;

        if (right != nullptr)
            right[startSample + i] += value;

        phase += phaseInc;

        if (phase >= twoPi)
            phase -= twoPi;

        envelope *= envelopeDecay;
    }
}

void Metronome::process (juce::AudioBuffer<float>& output,
                         std::uint64_t blockStartSample,
                         const Transport& transport) noexcept
{
    const int numSamples = output.getNumSamples();
    const int numChannels = output.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    float* left = output.getWritePointer (0);
    float* right = numChannels > 1 ? output.getWritePointer (1) : nullptr;

    if (! enabled)
    {
        // Aunque esté apagado hay que dejar que el clic en curso se apague, o
        // se corta en seco y se oye un chasquido.
        renderInto (left, right, 0, numSamples);
        return;
    }

    const double blockStart = static_cast<double> (blockStartSample);
    const double blockEnd = blockStart + numSamples;

    // Qué pulsos caen dentro de este bloque. Se calculan desde la rejilla, no
    // contando desde el anterior: por eso no puede derivar.
    const double firstPulse = transport.beatToPulse (transport.sampleToBeat (blockStart));
    const double lastPulseInBlock = transport.beatToPulse (transport.sampleToBeat (blockEnd));

    auto pulse = static_cast<std::int64_t> (std::ceil (firstPulse));

    int position = 0;

    while (static_cast<double> (pulse) < lastPulseInBlock)
    {
        const double pulseSample = transport.pulseToSample (static_cast<double> (pulse));
        const int offset = std::clamp (static_cast<int> (std::llround (pulseSample - blockStart)),
                                       0, numSamples - 1);

        if (offset > position)
        {
            renderInto (left, right, position, offset - position);
            position = offset;
        }

        const bool accented = transport.isDownbeat (pulse);
        triggerClick (accented);

        lastPulse = pulse;
        lastWasDownbeat = accented;

        ++pulse;
    }

    if (position < numSamples)
        renderInto (left, right, position, numSamples - position);
}

} // namespace keyla::core
