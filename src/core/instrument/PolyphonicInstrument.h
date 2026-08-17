#pragma once

#include "IInstrument.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace keyla::core
{

/** Maquinaria compartida por todos los instrumentos: reparto de voces, robo,
    pedal de sustain y ruteo de mensajes.

    Existe porque esa lógica es sutil —la distinción entre "tecla suelta" y
    "nota apagada" con el pedal pisado ya costó bugs una vez— y copiarla en cada
    instrumento sería copiar también sus errores. Escrita una vez, cada
    instrumento nuevo cuesta sólo su sonido.

    `VoiceType` es un parámetro de plantilla y no una interfaz virtual: se llama
    una vez por sample, y ahí una llamada indirecta no se paga sólo en ciclos,
    se paga en que el compilador no puede vectorizar nada.

    Contrato que debe cumplir `VoiceType`:

        void   start (int pitch, int velocity, double sampleRate);
        void   startRelease (double sampleRate);   // baja el apagador
        double nextSample();                       // avanza un sample
        double currentLevel() const;               // 0 = inaudible
*/
template <typename VoiceType>
class PolyphonicInstrument : public IInstrument
{
public:
    /** `gainTrim` iguala el volumen entre instrumentos. No es un adorno: sin él
        el acordeón sonaba al doble que el piano y cambiar de timbre obligaba a
        tocar el volumen del sistema. Los valores salen de medir, no de estimar
        — ver el test "[catalogue]".

        Se iguala por **pico** y no por RMS a propósito. Un piano decae y tiene
        picos altos con poca energía media; un órgano sostiene. Igualar el RMS
        obligaría a subir tanto el piano que su margen desaparecería, y quedarse
        sin margen es exactamente el error que produjo la saturación de los
        graves. Con el pico igualado, un instrumento sostenido se sigue oyendo
        algo más lleno — que es lo que pasa también con los de verdad. */
    PolyphonicInstrument (int maxVoicesToUse, const char* instrumentName, double gainTrim = 1.0)
        : name (instrumentName),
          maxVoices (std::max (1, maxVoicesToUse)),
          outputGain (gainTrim)
    {
        slots.resize (static_cast<std::size_t> (maxVoices));
    }

    void prepare (double sampleRateToUse, int /*maxBlockSize*/) override
    {
        sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;
        reset();
    }

    void release() override { reset(); }

    void reset() override
    {
        for (auto& slot : slots)
            slot = Slot {};

        sustainDown = false;
        voiceCounter = 0;
        activeVoices = 0;
    }

    InstrumentInfo info() const override { return { name, maxVoices }; }

    int activeVoiceCount() const noexcept override { return activeVoices; }

    void process (juce::AudioBuffer<float>& output, const MidiEventSpan& events) override
    {
        const int numSamples = output.getNumSamples();
        const int numChannels = output.getNumChannels();

        if (numSamples <= 0 || numChannels <= 0)
            return;

        float* left = output.getWritePointer (0);
        float* right = numChannels > 1 ? output.getWritePointer (1) : nullptr;

        // Se renderiza por tramos entre eventos para que cada nota empiece en su
        // sample y no al principio del bloque.
        int position = 0;

        for (const auto& event : events)
        {
            const int offset = std::clamp (event.renderOffset, 0, numSamples);

            if (offset > position)
            {
                renderRange (left, right, position, offset - position);
                position = offset;
            }

            handleMessage (event.message);
        }

        if (position < numSamples)
            renderRange (left, right, position, numSamples - position);
    }

protected:
    double currentSampleRate() const noexcept { return sampleRate; }

private:
    struct Slot
    {
        VoiceType voice {};
        bool active { false };
        bool keyDown { false };
        bool heldByPedal { false };
        int pitch { -1 };
        std::uint64_t order { 0 };
    };

    void renderRange (float* left, float* right, int startSample, int numSamples) noexcept
    {
        int active = 0;

        for (auto& slot : slots)
        {
            if (! slot.active)
                continue;

            ++active;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto value = static_cast<float> (slot.voice.nextSample() * outputGain);

                left[startSample + i] += value;

                if (right != nullptr)
                    right[startSample + i] += value;
            }

            // Una voz que ya no se oye se libera. Si no, las notas largas
            // ocupan el pool mucho después de ser audibles y el robo de voces
            // se dispara en cualquier pasaje con pedal.
            if (slot.voice.currentLevel() < 1.0e-4)
            {
                slot = Slot {};
                --active;
            }
        }

        activeVoices = active;
    }

    void handleMessage (const RawMidiMessage& message) noexcept
    {
        if (message.isNoteOn())
            noteOn (message.noteNumber(), message.velocity());
        else if (message.isNoteOff())
            noteOff (message.noteNumber());
        else if (message.isSustainPedal())
            setSustain (message.controllerValue());
        else if (message.isController() && message.controllerNumber() == 123)
            reset();
    }

    void noteOn (int pitch, int velocity) noexcept
    {
        if (pitch < 0 || pitch > 127)
            return;

        auto* slot = allocate();

        if (slot == nullptr)
            return;

        *slot = Slot {};
        slot->active = true;
        slot->keyDown = true;
        slot->pitch = pitch;
        slot->order = ++voiceCounter;
        slot->voice.start (pitch, velocity, sampleRate);
    }

    void noteOff (int pitch) noexcept
    {
        for (auto& slot : slots)
        {
            if (! slot.active || slot.pitch != pitch || ! slot.keyDown)
                continue;

            slot.keyDown = false;

            // Soltar la tecla con el pedal pisado no apaga la nota: la traspasa
            // al pedal (doc 02 §4).
            if (sustainDown)
                slot.heldByPedal = true;
            else
                slot.voice.startRelease (sampleRate);
        }
    }

    void setSustain (int value) noexcept
    {
        const bool nowDown = value >= sustainPedalThreshold;

        if (sustainDown && ! nowDown)
            for (auto& slot : slots)
                if (slot.active && slot.heldByPedal && ! slot.keyDown)
                {
                    slot.heldByPedal = false;
                    slot.voice.startRelease (sampleRate);
                }

        sustainDown = nowDown;
    }

    Slot* allocate() noexcept
    {
        for (auto& slot : slots)
            if (! slot.active)
                return &slot;

        // Se roba la voz más silenciosa, no la más antigua: robar por
        // antigüedad se lleva por delante la nota grave que sostiene el
        // acorde, que es justo la que más se nota.
        Slot* quietest = &slots.front();
        double lowest = quietest->voice.currentLevel();

        for (auto& slot : slots)
        {
            const double level = slot.voice.currentLevel();

            if (level < lowest)
            {
                lowest = level;
                quietest = &slot;
            }
        }

        return quietest;
    }

    const char* name;
    const int maxVoices;
    const double outputGain;

    std::vector<Slot> slots;
    double sampleRate { 48000.0 };
    bool sustainDown { false };
    std::uint64_t voiceCounter { 0 };
    int activeVoices { 0 };
};

} // namespace keyla::core
