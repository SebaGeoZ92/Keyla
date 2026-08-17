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
    double sum = knockLevel;

    for (int p = 0; p < activePartials; ++p)
        sum += level[static_cast<std::size_t> (p)];

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

            for (int p = 0; p < voice.activePartials; ++p)
            {
                sample += std::sin (voice.phase[static_cast<std::size_t> (p)])
                        * voice.level[static_cast<std::size_t> (p)];

                voice.phase[static_cast<std::size_t> (p)] += voice.phaseInc[static_cast<std::size_t> (p)];

                if (voice.phase[static_cast<std::size_t> (p)] >= twoPi)
                    voice.phase[static_cast<std::size_t> (p)] -= twoPi;

                voice.level[static_cast<std::size_t> (p)] *= voice.decay[static_cast<std::size_t> (p)];
            }

            if (voice.knockLevel > 1.0e-5)
            {
                voice.knockFilterState += voice.knockFilterCoef
                                        * (voice.nextNoise() - voice.knockFilterState);

                sample += voice.knockFilterState * voice.knockLevel;
                voice.knockLevel *= voice.knockDecay;
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

        // Una voz se libera cuando ya no aporta nada audible. El umbral está en
        // −80 dBFS y no más abajo a propósito: una caída exponencial tarda
        // muchísimo en llegar al último tramo, y perseguir −100 dBFS mantiene la
        // voz ocupada diez segundos después de que nadie la oiga. Eso dispara el
        // robo de voces en cualquier pasaje con pedal.
        if (voice.peakLevel() < 1.0e-4)
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
    //
    // El nivel es el de una voz con la energía ya normalizada abajo, así que
    // vale igual para un Do2 de 24 parciales que para un Do6 de dos.
    //
    // Con la energía a 1 y las fases repartidas, el pico instantáneo de una voz
    // ronda 2,2 veces esta cifra. A 0,25 una nota sola llega a ~0,55 de fondo
    // de escala y un acorde de seis deja al limitador trabajando poco. Subirlo
    // "porque suena flojo" es exactamente el error que produjo la saturación.
    voice->amplitude = 0.25 * std::pow (velocityNorm, 1.6);

    const double fundamental = midiNoteToHertz (pitch);
    const double B = inharmonicity (pitch);
    const double tau0 = fundamentalDecaySeconds (pitch);

    // Tocar fuerte añade armónicos. Suave: los parciales altos casi no suenan.
    const double brightness = 0.6 + 1.9 * velocityNorm;

    // El golpe del martillo. Nivel contenido y filtrado paso bajo: sin el
    // filtro esto se oye como un clic digital, que es exactamente la queja que
    // provocó la primera versión. Un martillo de fieltro sobre una cuerda no
    // tiene agudos por encima de un par de kHz.
    const double t = std::clamp ((pitch - 21) / 66.0, 0.0, 1.0);

    // Escala con la velocity al cuadrado: al tocar suave el martillo casi no
    // suena, y es al tocar fuerte cuando el golpe se hace evidente.
    voice->knockLevel = 0.22 * velocityNorm * velocityNorm * (1.0 - 0.4 * t);
    voice->knockDecay = std::exp (-1.0 / ((0.008 - 0.005 * t) * sampleRate));
    voice->knockFilterState = 0.0;
    voice->noiseState = static_cast<std::uint32_t> (pitch) * 2654435761u + 1u;

    // Más apagado en los graves (madera gorda), algo más claro en los agudos.
    const double knockCutoffHz = 700.0 + 1600.0 * t;
    voice->knockFilterCoef = std::clamp (1.0 - std::exp (-twoPi * knockCutoffHz / sampleRate),
                                         0.0, 1.0);

    // Cuántos parciales de verdad. Se cortan por dos sitios: por encima de
    // Nyquist habría aliasing, y por encima de unos 6 kHz el oído ya no
    // distingue parciales individuales de un sonido percutido.
    const double ceilingHz = std::min (6000.0, sampleRate * 0.45);
    int active = 0;

    for (int p = 0; p < maxPartials; ++p)
    {
        const double n = static_cast<double> (p + 1);
        const double frequency = fundamental * n * std::sqrt (1.0 + B * n * n);

        if (frequency >= ceilingHz)
            break;

        voice->phaseInc[static_cast<std::size_t> (p)] = twoPi * frequency / sampleRate;
        voice->level[static_cast<std::size_t> (p)] = std::pow (1.0 / n, 3.0 - brightness);

        // Fase inicial repartida, no cero. Con todos los parciales arrancando
        // alineados se suman en fase el primer instante y producen un pico que
        // no existe en ninguna cuerda real — y que satura el limitador justo en
        // el ataque, que es cuando más se nota.
        voice->phase[static_cast<std::size_t> (p)] = (voice->nextNoise() * 0.5 + 0.5) * twoPi;

        // Cada parcial se apaga a su ritmo, y los altos antes que el
        // fundamental: eso es lo que hace que un sonido percutido lo parezca.
        const double tau = tau0 / std::pow (n, 0.8);
        voice->decay[static_cast<std::size_t> (p)] = std::exp (-1.0 / (tau * sampleRate));

        ++active;
    }

    // Al menos el fundamental, aunque la nota sea altísima.
    voice->activePartials = std::max (1, active);

    // ── Normalización ───────────────────────────────────────────────────────
    //
    // Sin esto, cuantos más parciales tiene una nota más suena, y como el
    // número de parciales depende del registro, las graves salían al doble de
    // volumen que las agudas. Seis notas graves saturaban el limitador y el
    // recorte convertía el acorde en ruido.
    //
    // Se normaliza la **energía** (suma de cuadrados) y no la suma de niveles,
    // porque con las fases repartidas los parciales no se suman en línea recta:
    // lo que el oído sigue es la energía. Así el brillo cambia el color del
    // sonido sin cambiar lo fuerte que suena, que es justo lo que se quería.
    double energy = 0.0;

    for (int p = 0; p < voice->activePartials; ++p)
        energy += voice->level[static_cast<std::size_t> (p)] * voice->level[static_cast<std::size_t> (p)];

    if (energy > 0.0)
    {
        const double normalisation = 1.0 / std::sqrt (energy);

        for (int p = 0; p < voice->activePartials; ++p)
            voice->level[static_cast<std::size_t> (p)] *= normalisation;
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
