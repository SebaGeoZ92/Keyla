#include "StruckStringSynth.h"

#include <algorithm>
#include <cmath>

namespace keyla::core
{

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;

    double midiNoteToHertz (int note) noexcept
    {
        return 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0);
    }

    /** Coeficiente de inarmonicidad. En una cuerda real crece hacia el agudo
        —las cuerdas cortas son proporcionalmente más rígidas— y es lo que hace
        que los graves de un piano suenen "gordos" y los agudos algo desafinados
        hacia arriba respecto a la serie armónica pura. */
    double inharmonicity (int pitch) noexcept
    {
        const double t = std::clamp ((pitch - 21) / 66.0, 0.0, 1.0);
        return 0.00008 + 0.0009 * t * t;
    }

    /** Tiempo de caída del fundamental, en segundos. Los graves duran mucho más
        que los agudos: una nota baja de piano suena varios segundos, una alta
        se apaga enseguida. */
    double fundamentalDecaySeconds (int pitch) noexcept
    {
        const double t = std::clamp ((pitch - 21) / 66.0, 0.0, 1.0);
        return 9.0 * std::pow (0.12, t);        // ~9 s abajo, ~1 s arriba
    }
}

double StruckStringSynth::Voice::peakLevel() const noexcept
{
    double sum = 0.0;

    for (auto value : level)
        sum += value;

    return sum * amplitude * releaseGain;
}

StruckStringSynth::StruckStringSynth (int maxVoicesToUse)
    : maxVoices (std::max (1, maxVoicesToUse))
{
    voices.resize (static_cast<std::size_t> (maxVoices));
}

void StruckStringSynth::prepare (double sampleRateToUse, int /*maxBlockSize*/)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;

    // 4 ms de ataque: suficiente para que no chasquee, corto para que el tacto
    // no se sienta blando. Es de lo primero que habrá que ajustar a oído.
    attackInc = 1.0 / (0.004 * sampleRate);

    // El apagador de un piano no corta en seco: ~120 ms.
    releaseCoef = std::exp (-1.0 / (0.120 * sampleRate));

    reset();
}

void StruckStringSynth::release()
{
    reset();
}

void StruckStringSynth::reset()
{
    for (auto& voice : voices)
        voice = Voice {};

    sustainDown = false;
    voiceCounter = 0;
    activeVoices = 0;
}

InstrumentInfo StruckStringSynth::info() const
{
    return { "Cuerda percutida (provisional)", maxVoices };
}

// ── Hilo de audio ───────────────────────────────────────────────────────────

void StruckStringSynth::process (juce::AudioBuffer<float>& output, const MidiEventSpan& events)
{
    const int numSamples = output.getNumSamples();
    const int numChannels = output.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    float* left = output.getWritePointer (0);
    float* right = numChannels > 1 ? output.getWritePointer (1) : nullptr;

    // Se renderiza por tramos entre eventos, para que cada nota empiece en su
    // sample y no al principio del bloque. Es la mitad barata del invariante 3:
    // la posición exacta ya se conservó para medir, y aquí al menos no se
    // pierden los 3 ms del buffer al sonar.
    int position = 0;

    for (const auto& event : events)
    {
        const int offset = std::clamp (event.renderOffset, 0, numSamples);

        if (offset > position)
        {
            renderVoices (left, right, position, offset - position);
            position = offset;
        }

        handleMessage (event.message);
    }

    if (position < numSamples)
        renderVoices (left, right, position, numSamples - position);
}

void StruckStringSynth::renderVoices (float* left, float* right, int startSample, int numSamples) noexcept
{
    int active = 0;

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        ++active;

        for (int i = 0; i < numSamples; ++i)
        {
            double sample = 0.0;

            for (int p = 0; p < numPartials; ++p)
            {
                sample += std::sin (voice.phase[static_cast<std::size_t> (p)])
                        * voice.level[static_cast<std::size_t> (p)];

                voice.phase[static_cast<std::size_t> (p)] += voice.phaseInc[static_cast<std::size_t> (p)];

                if (voice.phase[static_cast<std::size_t> (p)] >= twoPi)
                    voice.phase[static_cast<std::size_t> (p)] -= twoPi;

                voice.level[static_cast<std::size_t> (p)] *= voice.decay[static_cast<std::size_t> (p)];
            }

            if (voice.attackGain < 1.0)
            {
                voice.attackGain += attackInc;

                if (voice.attackGain > 1.0)
                    voice.attackGain = 1.0;
            }

            if (voice.releasing)
                voice.releaseGain *= releaseCoef;

            const auto value = static_cast<float> (sample * voice.amplitude
                                                   * voice.attackGain * voice.releaseGain);

            left[startSample + i] += value;

            if (right != nullptr)
                right[startSample + i] += value;
        }

        // Una voz se libera cuando ya no aporta nada audible. Sin esto, las
        // notas graves ocuparían una voz durante nueve segundos y el robo de
        // voces se dispararía en cualquier pasaje con pedal.
        if (voice.peakLevel() < 1.0e-5)
        {
            voice = Voice {};
            --active;
        }
    }

    activeVoices = active;
}

// ── Eventos ─────────────────────────────────────────────────────────────────

void StruckStringSynth::handleMessage (const RawMidiMessage& message) noexcept
{
    if (message.isNoteOn())
        noteOn (message.noteNumber(), message.velocity());
    else if (message.isNoteOff())
        noteOff (message.noteNumber());
    else if (message.isSustainPedal())
        setSustain (message.controllerValue());
    else if (message.isController() && message.controllerNumber() == 123)
        reset();                                    // All Notes Off
}

StruckStringSynth::Voice* StruckStringSynth::allocateVoice() noexcept
{
    for (auto& voice : voices)
        if (! voice.active)
            return &voice;

    // Todas ocupadas: se roba la más silenciosa, no la más antigua. Robar por
    // antigüedad se lleva por delante la nota grave que sostiene el acorde,
    // que es la que más se nota.
    Voice* quietest = &voices.front();
    double lowest = quietest->peakLevel();

    for (auto& voice : voices)
    {
        const double loudness = voice.peakLevel();

        if (loudness < lowest)
        {
            lowest = loudness;
            quietest = &voice;
        }
    }

    return quietest;
}

void StruckStringSynth::noteOn (int pitch, int velocity) noexcept
{
    if (pitch < 0 || pitch > 127)
        return;

    auto* voice = allocateVoice();

    if (voice == nullptr)
        return;

    *voice = Voice {};

    voice->active = true;
    voice->keyDown = true;
    voice->pitch = pitch;
    voice->startOrder = ++voiceCounter;
    voice->attackGain = 0.0;
    voice->releaseGain = 1.0;
    voice->releasing = false;

    const double velocityNorm = std::clamp (velocity / 127.0, 0.0, 1.0);

    // La amplitud sigue una curva, no una recta: el oído es logarítmico y una
    // rampa lineal de velocity se siente muerta en el pianissimo.
    voice->amplitude = 0.20 * std::pow (velocityNorm, 1.6);

    const double fundamental = midiNoteToHertz (pitch);
    const double B = inharmonicity (pitch);
    const double tau0 = fundamentalDecaySeconds (pitch);

    // Tocar fuerte añade armónicos. Suave: los parciales altos casi no suenan.
    const double brightness = 0.6 + 1.9 * velocityNorm;

    for (int p = 0; p < numPartials; ++p)
    {
        const double n = static_cast<double> (p + 1);

        // Frecuencia del parcial con la corrección de rigidez de la cuerda.
        const double frequency = fundamental * n * std::sqrt (1.0 + B * n * n);

        // Por encima de Nyquist no se sintetiza: sólo produciría aliasing.
        if (frequency >= sampleRate * 0.48)
        {
            voice->level[static_cast<std::size_t> (p)] = 0.0;
            voice->decay[static_cast<std::size_t> (p)] = 0.0;
            voice->phaseInc[static_cast<std::size_t> (p)] = 0.0;
            continue;
        }

        voice->phaseInc[static_cast<std::size_t> (p)] = twoPi * frequency / sampleRate;
        voice->phase[static_cast<std::size_t> (p)] = 0.0;
        voice->level[static_cast<std::size_t> (p)] = std::pow (1.0 / n, 3.0 - brightness);

        // Cada parcial se apaga a su ritmo, y los altos antes.
        const double tau = tau0 / std::pow (n, 0.8);
        voice->decay[static_cast<std::size_t> (p)] = std::exp (-1.0 / (tau * sampleRate));
    }
}

void StruckStringSynth::noteOff (int pitch) noexcept
{
    for (auto& voice : voices)
    {
        if (! voice.active || voice.pitch != pitch || ! voice.keyDown)
            continue;

        voice.keyDown = false;

        // Con el pedal pisado, soltar la tecla no baja el apagador: la nota
        // pasa a estar sostenida por el pedal (doc 02 §4).
        if (sustainDown)
            voice.heldByPedal = true;
        else
            startRelease (voice);
    }
}

void StruckStringSynth::setSustain (int value) noexcept
{
    const bool nowDown = value >= sustainPedalThreshold;

    if (sustainDown && ! nowDown)
    {
        // Al levantar el pedal caen los apagadores de todo lo que sólo él
        // sostenía. Lo que sigue con la tecla pulsada no se toca.
        for (auto& voice : voices)
            if (voice.active && voice.heldByPedal && ! voice.keyDown)
                startRelease (voice);
    }

    sustainDown = nowDown;
}

void StruckStringSynth::startRelease (Voice& voice) noexcept
{
    voice.releasing = true;
    voice.heldByPedal = false;
}

} // namespace keyla::core
