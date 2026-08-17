#include "Transport.h"

#include <cmath>

namespace keyla::core
{

void Transport::prepare (double sampleRateToUse) noexcept
{
    if (sampleRateToUse > 0.0)
        rate = sampleRateToUse;

    streamSample = 0;
}

void Transport::advance (int numSamples) noexcept
{
    if (numSamples > 0)
        streamSample += static_cast<std::uint64_t> (numSamples);
}

void Transport::resetToZero() noexcept
{
    streamSample = 0;
}

void Transport::setTempo (double beatsPerMinute) noexcept
{
    // Un tempo de cero o negativo dividiría por cero en cada conversión. Se
    // ignora en vez de propagar infinitos por todo el modelo temporal.
    if (beatsPerMinute > 0.0)
        bpm = beatsPerMinute;
}

void Transport::setTimeSignature (TimeSignature signature) noexcept
{
    if (signature.isValid())
        meter = signature;
}

double Transport::sampleToBeat (double samplePos) const noexcept
{
    return (samplePos - barZero) / samplesPerBeat();
}

double Transport::beatToSample (double beat) const noexcept
{
    return barZero + beat * samplesPerBeat();
}

MusicalPosition Transport::beatToPosition (double beat) const noexcept
{
    const double beatsPerBar = meter.beatsPerBar();
    const double beatsPerPulse = meter.beatsPerPulse();

    // std::floor y no una división entera: antes del pulso cero los beats son
    // negativos, y la división entera trunca hacia cero. Eso colocaría el
    // compás -1 y el compás 0 en el mismo sitio.
    const double barIndex = std::floor (beat / beatsPerBar);
    const double beatInBar = beat - barIndex * beatsPerBar;

    const double pulseInBar = beatInBar / beatsPerPulse;
    const double pulseIndex = std::floor (pulseInBar);

    MusicalPosition position;
    position.bar = static_cast<int> (barIndex) + 1;             // los músicos cuentan desde 1
    position.pulse = static_cast<int> (pulseIndex) + 1;
    position.fractionOfPulse = pulseInBar - pulseIndex;

    return position;
}

double Transport::positionToBeat (MusicalPosition position) const noexcept
{
    const double bars = static_cast<double> (position.bar - 1);
    const double pulses = static_cast<double> (position.pulse - 1) + position.fractionOfPulse;

    return bars * meter.beatsPerBar() + pulses * meter.beatsPerPulse();
}

bool Transport::isDownbeat (std::int64_t pulseIndex) const noexcept
{
    const auto perBar = static_cast<std::int64_t> (meter.pulsesPerBar());

    if (perBar <= 0)
        return false;

    // Módulo que también funciona con índices negativos.
    const auto remainder = ((pulseIndex % perBar) + perBar) % perBar;
    return remainder == 0;
}

} // namespace keyla::core
