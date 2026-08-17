#include "LoopbackCalibrator.h"
#include "Utf8.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace keyla::probe
{

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;
    constexpr double chirpSeconds = 0.006;      // 6 ms
    constexpr double chirpStartHz = 500.0;
    constexpr double chirpEndHz = 8000.0;
    constexpr double searchSeconds = 0.35;      // ventana de búsqueda tras emitir
}

LoopbackCalibrator::LoopbackCalibrator (int numPulsesIn, double spacingSecondsIn)
    : numPulses (juce::jmax (1, numPulsesIn)),
      spacingSeconds (juce::jmax (0.2, spacingSecondsIn))
{
}

void LoopbackCalibrator::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();

    // ── Plantilla: chirp lineal con ventana de Hann ─────────────────────────
    const auto chirpLen = static_cast<int> (chirpSeconds * sampleRate);
    chirp.assign (static_cast<std::size_t> (chirpLen), 0.0f);

    const double k = (chirpEndHz - chirpStartHz) / chirpSeconds;

    for (int i = 0; i < chirpLen; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const double phase = twoPi * (chirpStartHz * t + 0.5 * k * t * t);
        const double window = 0.5 - 0.5 * std::cos (twoPi * static_cast<double> (i)
                                                    / static_cast<double> (chirpLen - 1));
        chirp[static_cast<std::size_t> (i)] = static_cast<float> (0.5 * window * std::sin (phase));
    }

    // ── Reserva de todo lo que el callback va a tocar ───────────────────────
    startPadSamples = static_cast<std::int64_t> (0.5 * sampleRate);
    spacingSamples = static_cast<std::int64_t> (spacingSeconds * sampleRate);

    emissionSample.resize (static_cast<std::size_t> (numPulses));

    for (int i = 0; i < numPulses; ++i)
        emissionSample[static_cast<std::size_t> (i)] = startPadSamples + static_cast<std::int64_t> (i) * spacingSamples;

    const auto total = emissionSample.back() + static_cast<std::int64_t> (searchSeconds * sampleRate) + spacingSamples;
    capture.assign (static_cast<std::size_t> (total), 0.0f);

    streamSamplePos = 0;
    hadInput = false;
    publishedPos.store (0, std::memory_order_release);
    finished.store (false, std::memory_order_release);
}

void LoopbackCalibrator::audioDeviceStopped()
{
}

double LoopbackCalibrator::progress() const noexcept
{
    if (capture.empty())
        return 0.0;

    const auto pos = publishedPos.load (std::memory_order_acquire);
    return juce::jlimit (0.0, 1.0, static_cast<double> (pos) / static_cast<double> (capture.size()));
}

void LoopbackCalibrator::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                           int numInputChannels,
                                                           float* const* outputChannelData,
                                                           int numOutputChannels,
                                                           int numSamples,
                                                           const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    const auto blockStart = streamSamplePos;
    const auto blockEnd = blockStart + numSamples;
    const auto chirpLen = static_cast<std::int64_t> (chirp.size());

    // ── Emisión ─────────────────────────────────────────────────────────────
    for (auto s : emissionSample)
    {
        if (s + chirpLen <= blockStart || s >= blockEnd)
            continue;

        const auto from = std::max (s, blockStart);
        const auto to = std::min (s + chirpLen, blockEnd);

        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            if (outputChannelData[ch] == nullptr)
                continue;

            for (auto n = from; n < to; ++n)
                outputChannelData[ch][n - blockStart] += chirp[static_cast<std::size_t> (n - s)];
        }
    }

    // ── Captura ─────────────────────────────────────────────────────────────
    if (numInputChannels > 0 && inputChannelData != nullptr && inputChannelData[0] != nullptr)
    {
        hadInput = true;

        const auto capacity = static_cast<std::int64_t> (capture.size());
        const auto to = std::min (blockEnd, capacity);

        for (auto n = blockStart; n < to; ++n)
            capture[static_cast<std::size_t> (n)] = inputChannelData[0][n - blockStart];
    }

    streamSamplePos = blockEnd;
    publishedPos.store (blockEnd, std::memory_order_release);

    if (streamSamplePos >= static_cast<std::int64_t> (capture.size()))
        finished.store (true, std::memory_order_release);
}

// ── Análisis (hilo principal, stream parado) ────────────────────────────────

LoopbackCalibrator::Result LoopbackCalibrator::analyse() const
{
    Result r;
    r.sampleRate = sampleRate;
    r.attempted = numPulses;

    if (! hadInput)
    {
        r.message = "No llegó ninguna muestra de entrada: no hay dispositivo de entrada abierto."_u8;
        return r;
    }

    const auto chirpLen = static_cast<std::int64_t> (chirp.size());
    const auto searchLen = static_cast<std::int64_t> (searchSeconds * sampleRate);
    const auto capacity = static_cast<std::int64_t> (capture.size());

    double energy = 0.0;
    for (auto c : chirp) energy += static_cast<double> (c) * c;

    if (energy <= 0.0)
    {
        r.message = "Plantilla vacía."_u8;
        return r;
    }

    std::vector<double> correlation (static_cast<std::size_t> (searchLen), 0.0);
    double worstPeakToNoise = 1e300;

    for (auto sOut : emissionSample)
    {
        if (sOut + searchLen + chirpLen > capacity)
            continue;

        // Correlación cruzada de la entrada con la plantilla emitida.
        for (std::int64_t off = 0; off < searchLen; ++off)
        {
            double acc = 0.0;

            for (std::int64_t i = 0; i < chirpLen; ++i)
                acc += static_cast<double> (capture[static_cast<std::size_t> (sOut + off + i)]) * chirp[static_cast<std::size_t> (i)];

            correlation[static_cast<std::size_t> (off)] = std::abs (acc);
        }

        const auto peakIt = std::max_element (correlation.begin(), correlation.end());
        const auto peak = *peakIt;
        const auto peakOffset = std::distance (correlation.begin(), peakIt);

        // Suelo de ruido: la mediana de la correlación fuera del pico.
        std::vector<double> sortedCorr (correlation);
        std::nth_element (sortedCorr.begin(), sortedCorr.begin() + static_cast<std::ptrdiff_t> (sortedCorr.size() / 2), sortedCorr.end());
        const auto noise = sortedCorr[sortedCorr.size() / 2];

        const double ratio = noise > 0.0 ? peak / noise : 0.0;
        worstPeakToNoise = std::min (worstPeakToNoise, ratio);

        if (ratio < 8.0)
            continue;   // no hay señal reconocible en esta repetición

        r.roundTripSamples.push_back (static_cast<double> (peakOffset));
    }

    r.detected = static_cast<int> (r.roundTripSamples.size());
    r.peakToNoise = worstPeakToNoise < 1e299 ? worstPeakToNoise : 0.0;

    if (r.detected < juce::jmax (3, numPulses / 4))
    {
        r.message = "No se detectó la señal de vuelta en suficientes repeticiones. "
                    "¿Está el cable de loopback conectado y el volumen de salida y de "
                    "entrada a un nivel razonable?"_u8;
        return r;
    }

    auto sorted = r.roundTripSamples;
    std::sort (sorted.begin(), sorted.end());
    r.medianSamples = sorted[sorted.size() / 2];

    const double mean = std::accumulate (sorted.begin(), sorted.end(), 0.0) / static_cast<double> (sorted.size());
    double acc = 0.0;
    for (auto v : sorted) acc += (v - mean) * (v - mean);
    r.sigmaSamples = sorted.size() > 1 ? std::sqrt (acc / static_cast<double> (sorted.size() - 1)) : 0.0;

    r.ok = true;
    return r;
}

} // namespace keyla::probe
