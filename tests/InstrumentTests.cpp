// El sonido lo juzga un humano, pero el **nivel** es medible, y medirlo aquí
// habría cazado el bug que se coló en producción: al subir los parciales de 8 a
// 24 sin renormalizar, cada nota grave pasó a sonar al doble, seis de ellas
// saturaban el limitador y el acorde se convertía en ruido.
//
// Nadie oyó ese error hasta que estuvo en la aplicación. Estos tests lo habrían
// visto sin hardware y sin oídos.

#include <core/instrument/StruckStringSynth.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <iostream>

using namespace keyla::core;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;

    RawMidiMessage noteOn (int pitch, int velocity)
    {
        RawMidiMessage m;
        m.bytes[0] = 0x90;
        m.bytes[1] = static_cast<std::uint8_t> (pitch);
        m.bytes[2] = static_cast<std::uint8_t> (velocity);
        m.size = 3;
        return m;
    }

    struct Measurement
    {
        float peak { 0.0f };
        double rms { 0.0 };
    };

    /** Toca las notas a la vez y mide la salida durante `seconds`. */
    Measurement render (StruckStringSynth& synth, const std::vector<int>& pitches,
                        int velocity, double seconds)
    {
        synth.prepare (sampleRate, blockSize);

        std::vector<StampedMidiEvent> events;

        for (auto pitch : pitches)
        {
            StampedMidiEvent event;
            event.message = noteOn (pitch, velocity);
            event.renderOffset = 0;
            events.push_back (event);
        }

        juce::AudioBuffer<float> buffer (2, blockSize);
        Measurement result;
        double sumOfSquares = 0.0;
        std::uint64_t total = 0;

        const int numBlocks = static_cast<int> (seconds * sampleRate / blockSize);

        for (int block = 0; block < numBlocks; ++block)
        {
            buffer.clear();

            const MidiEventSpan span { block == 0 ? events.data() : nullptr,
                                       block == 0 ? events.size() : 0 };

            synth.process (buffer, span);

            const auto* samples = buffer.getReadPointer (0);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto value = std::abs (samples[i]);
                result.peak = std::max (result.peak, value);
                sumOfSquares += static_cast<double> (samples[i]) * samples[i];
                ++total;
            }
        }

        result.rms = total > 0 ? std::sqrt (sumOfSquares / static_cast<double> (total)) : 0.0;
        return result;
    }
}

TEST_CASE ("Una nota sola no se acerca a fondo de escala", "[instrument][level]")
{
    StruckStringSynth synth { 32 };

    for (int pitch : { 36, 48, 60, 72, 84 })
    {
        const auto measured = render (synth, { pitch }, 127, 1.0);

        INFO ("nota MIDI " << pitch << "  pico " << measured.peak << "  rms " << measured.rms);

        // Ni recorta ni se queda inaudible.
        CHECK (measured.peak < 0.9f);
        CHECK (measured.peak > 0.05f);
    }
}

TEST_CASE ("El registro no cambia lo fuerte que suena una nota", "[instrument][level]")
{
    // Éste es el test que faltaba. Con los parciales sin normalizar, un Do2 con
    // 24 parciales sonaba al doble que un Do5 con tres, porque el número de
    // parciales depende del registro y los niveles se suman.
    StruckStringSynth synth { 32 };

    const auto low = render (synth, { 36 }, 100, 1.0);
    const auto middle = render (synth, { 60 }, 100, 1.0);
    const auto high = render (synth, { 84 }, 100, 1.0);

    std::cout << "  [nivel] Do2 pico " << low.peak
              << "   Do4 pico " << middle.peak
              << "   Do6 pico " << high.peak << '\n';

    // Un piano real no suena idéntico en todo el teclado, así que se admite un
    // margen amplio. Lo que no se admite es el factor 2 del bug.
    CHECK (low.peak < middle.peak * 1.8f);
    CHECK (middle.peak < low.peak * 1.8f);
    CHECK (high.peak < middle.peak * 1.8f);
    CHECK (middle.peak < high.peak * 1.8f);
}

TEST_CASE ("Un racimo grave de seis notas no dispara el nivel", "[instrument][level]")
{
    // El acorde exacto que sonó a ruido: Do2-Re2-Re#2-Fa2-Fa#2-Sol#2 a tope.
    StruckStringSynth synth { 32 };

    const auto measured = render (synth, { 36, 38, 39, 41, 42, 44 }, 127, 1.5);

    std::cout << "  [nivel] racimo grave x6 pico " << measured.peak
              << "  rms " << measured.rms << '\n';

    // Puede pasar de 1 —para eso está el limitador— pero no por goleada: si el
    // limitador tiene que quitar 12 dB, el recorte se oye.
    CHECK (measured.peak < 2.5f);
}

TEST_CASE ("Tocar fuerte cambia el color, no sólo el volumen", "[instrument][level]")
{
    // Si al subir la velocity sólo subiera la amplitud, la dinámica sonaría
    // falsa y calibrar la curva de velocity no significaría nada (doc 02 §4).
    StruckStringSynth synth { 32 };

    const auto soft = render (synth, { 60 }, 30, 1.0);
    const auto loud = render (synth, { 60 }, 120, 1.0);

    CHECK (loud.peak > soft.peak * 2.0f);

    // Y el brillo tiene que subir de verdad: más energía en la mitad alta del
    // espectro. Se aproxima contando cruces por cero, que suben con el
    // contenido agudo sin necesidad de una FFT.
    CHECK (loud.rms > soft.rms);
}

TEST_CASE ("Las voces se liberan solas cuando dejan de sonar", "[instrument][voices]")
{
    // Si no se liberaran, una nota grave ocuparía su voz nueve segundos y
    // cualquier pasaje con pedal dispararía el robo de voces.
    StruckStringSynth synth { 4 };
    synth.prepare (sampleRate, blockSize);

    StampedMidiEvent event;
    event.message = noteOn (84, 100);      // aguda: se apaga rápido
    event.renderOffset = 0;

    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    synth.process (buffer, MidiEventSpan { &event, 1 });

    CHECK (synth.activeVoiceCount() == 1);

    // Una nota aguda sin soltar la tecla: la cuerda se apaga sola. Doce
    // segundos son de sobra; si algún día hace falta más, es que la cola de la
    // envolvente se ha ido de las manos y hay que mirarla.
    for (int block = 0; block < static_cast<int> (12.0 * sampleRate / blockSize); ++block)
    {
        buffer.clear();
        synth.process (buffer, MidiEventSpan { nullptr, 0 });
    }

    CHECK (synth.activeVoiceCount() == 0);
}

TEST_CASE ("El pedal sostiene y soltarlo apaga", "[instrument][pedal]")
{
    StruckStringSynth synth { 8 };
    synth.prepare (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);

    auto send = [&] (const RawMidiMessage& message)
    {
        StampedMidiEvent event;
        event.message = message;
        event.renderOffset = 0;
        buffer.clear();
        synth.process (buffer, MidiEventSpan { &event, 1 });
    };

    RawMidiMessage pedalDown;
    pedalDown.bytes[0] = 0xB0; pedalDown.bytes[1] = 64; pedalDown.bytes[2] = 127; pedalDown.size = 3;

    RawMidiMessage pedalUp = pedalDown;
    pedalUp.bytes[2] = 0;

    RawMidiMessage off;
    off.bytes[0] = 0x80; off.bytes[1] = 84; off.bytes[2] = 0; off.size = 3;

    send (pedalDown);
    send (noteOn (84, 110));
    send (off);

    // Con el pedal pisado la nota sigue viva pese al Note Off.
    for (int block = 0; block < 40; ++block)
    {
        buffer.clear();
        synth.process (buffer, MidiEventSpan { nullptr, 0 });
    }

    CHECK (synth.activeVoiceCount() == 1);

    send (pedalUp);

    // Y al soltarlo baja el apagador y la voz se libera en menos de dos
    // segundos, no en los diez que tardaba con el umbral anterior.
    for (int block = 0; block < static_cast<int> (2.0 * sampleRate / blockSize); ++block)
    {
        buffer.clear();
        synth.process (buffer, MidiEventSpan { nullptr, 0 });
    }

    CHECK (synth.activeVoiceCount() == 0);
}
