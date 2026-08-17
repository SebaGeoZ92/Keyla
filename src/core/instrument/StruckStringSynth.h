#pragma once

#include "IInstrument.h"

#include <array>
#include <vector>

namespace keyla::core
{

/** Instrumento provisional de la fase 1: síntesis aditiva de cuerda percutida.

    **No es un piano y no pretende serlo.** El doc 02 §2 es explícito: un piano
    sintetizado creíble es un proyecto en sí mismo, y el plan es un sampler. Esto
    existe para que se pueda tocar, medir el tacto y cerrar la fase 1 sin que la
    primera impresión dependa de qué librería de samples se descargue.

    Lo que sí hace, y por eso suena musical en vez de a pitido:

    - **Parciales con decaimiento propio.** Los agudos se apagan antes que el
      fundamental, que es lo que hace que un sonido percutido suene percutido.
    - **Inarmonicidad.** En una cuerda real el parcial n no está en n·f sino
      un poco por encima, porque la cuerda tiene rigidez. Sin esto suena a
      órgano.
    - **Brillo por velocity.** Tocar fuerte no es tocar lo mismo más alto: es
      tocar con más armónicos. Si sólo se escala la amplitud, la dinámica suena
      falsa y la calibración de velocity (doc 02 §4) no significaría nada.

    Vive en `core/` y sólo depende de `juce_audio_basics`, así que se testea sin
    tarjeta de sonido.
*/
class StruckStringSynth final : public IInstrument
{
public:
    /** Tope de parciales por voz. Cuántos se usan de verdad depende del
        registro: un Do2 necesita muchos —con ocho sólo se llega a 520 Hz y lo
        que sale es un bajo eléctrico, no un piano— y un Do6 no necesita casi
        ninguno porque enseguida se sale de lo audible. */
    static constexpr int maxPartials = 24;

    explicit StruckStringSynth (int maxVoices = 32);

    // ── Fuera del hilo de audio, con el motor parado ────────────────────────
    void prepare (double sampleRate, int maxBlockSize) override;
    void release() override;
    void reset() override;
    InstrumentInfo info() const override;

    // ── En el hilo de audio (invariante 1) ──────────────────────────────────
    void process (juce::AudioBuffer<float>& output, const MidiEventSpan& events) override;

    int activeVoiceCount() const noexcept override { return activeVoices; }

    void setMasterGain (float gain) noexcept { masterGain = gain; }

private:
    struct Voice
    {
        bool active { false };
        bool keyDown { false };
        bool heldByPedal { false };
        int pitch { -1 };
        std::uint64_t startOrder { 0 };

        double amplitude { 0.0 };
        double attackGain { 0.0 };      // rampa de entrada, 0 → 1
        double releaseGain { 1.0 };     // apagador, 1 → 0 al soltar
        bool releasing { false };

        // El golpe del martillo: un ruido corto que separa "percutido" de
        // "soplado". Va filtrado paso bajo a propósito — el ruido blanco crudo
        // se oye como un *clic* digital, no como madera. La diferencia entre un
        // chasquido molesto y un golpe creíble está casi toda en ese filtro.
        double knockLevel { 0.0 };
        double knockDecay { 0.0 };
        double knockFilterCoef { 0.0 };
        double knockFilterState { 0.0 };
        std::uint32_t noiseState { 1 };

        int activePartials { 0 };
        std::array<double, maxPartials> phase {};
        std::array<double, maxPartials> phaseInc {};
        std::array<double, maxPartials> level {};
        std::array<double, maxPartials> decay {};

        double peakLevel() const noexcept;

        /** Ruido blanco barato, sin asignar ni tocar el generador global. */
        double nextNoise() noexcept
        {
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;
            return static_cast<double> (noiseState) / 2147483648.0 - 1.0;
        }
    };

    void handleMessage (const RawMidiMessage& message) noexcept;
    void noteOn (int pitch, int velocity) noexcept;
    void noteOff (int pitch) noexcept;
    void setSustain (int value) noexcept;
    void startRelease (Voice& voice) noexcept;

    Voice* allocateVoice() noexcept;
    void renderVoices (float* left, float* right, int startSample, int numSamples) noexcept;

    const int maxVoices;

    std::vector<Voice> voices;
    double sampleRate { 48000.0 };
    double attackInc { 0.0 };
    double releaseCoef { 0.0 };
    float masterGain { 0.35f };

    bool sustainDown { false };
    std::uint64_t voiceCounter { 0 };
    int activeVoices { 0 };
};

} // namespace keyla::core
