#pragma once

// Método B del doc 04 §3: latencia de ida y vuelta medida con un cable físico
// de la salida de auriculares a la entrada de línea/micrófono.
//
// Es la única cifra del lado de audio que no depende de creerle al driver.

#include <juce_audio_devices/juce_audio_devices.h>

#include <vector>

namespace keyla::probe
{

class LoopbackCalibrator final : public juce::AudioIODeviceCallback
{
public:
    LoopbackCalibrator (int numPulses, double spacingSeconds);

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    bool isFinished() const noexcept { return finished.load (std::memory_order_acquire); }
    double progress() const noexcept;

    struct Result
    {
        bool ok { false };
        juce::String message;
        std::vector<double> roundTripSamples;
        double medianSamples { 0.0 };
        double sigmaSamples { 0.0 };
        double sampleRate { 48000.0 };
        double peakToNoise { 0.0 };
        int detected { 0 };
        int attempted { 0 };
    };

    // Se llama desde el hilo principal con el stream ya parado.
    Result analyse() const;

private:
    const int numPulses;
    const double spacingSeconds;

    double sampleRate { 48000.0 };
    std::vector<float> chirp;       // plantilla emitida
    std::vector<float> capture;     // entrada grabada, indexada por sample del stream
    std::vector<std::int64_t> emissionSample;

    std::int64_t startPadSamples { 0 };
    std::int64_t spacingSamples { 0 };
    std::int64_t streamSamplePos { 0 };
    bool hadInput { false };

    std::atomic<std::int64_t> publishedPos { 0 };   // sólo para el indicador de progreso
    std::atomic<bool> finished { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackCalibrator)
};

} // namespace keyla::probe
