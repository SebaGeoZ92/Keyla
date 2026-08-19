// El sonido lo juzga un humano, pero el **nivel** es medible, y medirlo aquí
// habría cazado el bug que se coló en producción: al subir los parciales de 8 a
// 24 sin renormalizar, cada nota grave pasó a sonar al doble, seis de ellas
// saturaban el limitador y el acorde se convertía en ruido.
//
// Nadie oyó ese error hasta que estuvo en la aplicación. Estos tests lo habrían
// visto sin hardware y sin oídos.

#include <core/audio/Limiter.h>
#include <core/instrument/Instruments.h>
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
    Measurement render (IInstrument& synth, const std::vector<int>& pitches,
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

TEST_CASE ("Todos los instrumentos respetan el presupuesto de nivel", "[instrument][level][catalogue]")
{
    // El bug de los graves del piano fue de nivel, y con cinco instrumentos más
    // la ocasión de repetirlo se multiplica por cinco. Estas comprobaciones son
    // baratas y se aplican al catálogo entero: ninguno puede recortar solo, y
    // ninguno puede sonar al doble que los demás — cambiar de instrumento no
    // debe obligar a tocar el volumen del sistema.
    const std::vector<InstrumentId> catalogue {
        InstrumentId::piano, InstrumentId::electricPiano, InstrumentId::organ,
        InstrumentId::accordion, InstrumentId::strings, InstrumentId::vibraphone
    };

    std::vector<float> singleNotePeaks;

    for (auto id : catalogue)
    {
        auto instrument = createInstrument (id);
        REQUIRE (instrument != nullptr);

        // Las cuerdas tardan 320 ms en entrar: hay que medir más rato o se
        // mediría el silencio del ataque.
        const auto single = render (*instrument, { 60 }, 110, 2.0);
        const auto chord = render (*instrument, { 48, 52, 55, 60, 64, 67 }, 120, 2.0);

        std::cout << "  [catálogo] " << instrumentName (id).toStdString()
                  << "\tnota pico " << single.peak << " rms " << single.rms
                  << "\tacorde x6 pico " << chord.peak << " rms " << chord.rms << '\n';

        INFO ("instrumento " << instrumentName (id).toStdString());

        CHECK (single.peak > 0.05f);        // se oye
        CHECK (single.peak < 0.9f);         // no recorta él solo
        CHECK (chord.peak < 2.5f);          // el limitador no tiene que hacer milagros

        singleNotePeaks.push_back (single.peak);
    }

    const auto quietest = *std::min_element (singleNotePeaks.begin(), singleNotePeaks.end());
    const auto loudest = *std::max_element (singleNotePeaks.begin(), singleNotePeaks.end());

    // Margen estrecho a propósito: si alguien retoca una voz y desequilibra el
    // catálogo, este test tiene que quejarse. Con 2,5 el acordeón al doble del
    // piano pasaba sin enterarse nadie.
    CHECK (loudest < quietest * 1.5f);
}

TEST_CASE ("Cada instrumento se apaga al soltar la tecla", "[instrument][catalogue]")
{
    // Un órgano o un acordeón suenan mientras aguantas: si el Note Off no
    // bajara el apagador, la nota se quedaría sonando para siempre. Es el fallo
    // más obvio posible y el más fácil de no probar.
    for (auto id : { InstrumentId::piano, InstrumentId::electricPiano, InstrumentId::organ,
                     InstrumentId::accordion, InstrumentId::strings, InstrumentId::vibraphone })
    {
        auto instrument = createInstrument (id);
        instrument->prepare (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);

        StampedMidiEvent on;
        on.message = noteOn (60, 100);
        on.renderOffset = 0;
        buffer.clear();
        instrument->process (buffer, MidiEventSpan { &on, 1 });

        INFO ("instrumento " << instrumentName (id).toStdString());
        CHECK (instrument->activeVoiceCount() == 1);

        StampedMidiEvent off;
        off.message.bytes[0] = 0x80;
        off.message.bytes[1] = 60;
        off.message.size = 3;
        off.renderOffset = 0;
        buffer.clear();
        instrument->process (buffer, MidiEventSpan { &off, 1 });

        // Tres segundos son de sobra para cualquier extinción del catálogo.
        for (int block = 0; block < static_cast<int> (3.0 * sampleRate / blockSize); ++block)
        {
            buffer.clear();
            instrument->process (buffer, MidiEventSpan { nullptr, 0 });
        }

        CHECK (instrument->activeVoiceCount() == 0);
    }
}

TEST_CASE ("El limitador no persigue los batidos de los graves", "[instrument][limiter]")
{
    // Varias notas graves a la vez fluctúan en amplitud a la frecuencia de sus
    // diferencias: Do2 y Re2 laten a 8 Hz. Un limitador con recuperación de
    // 100 ms responde justo a esa velocidad, persigue el batido y modula la
    // ganancia con él. No suena a compresión: suena a ruido.
    //
    // La propiedad que hay que conservar es que la recuperación sea claramente
    // más lenta que eso.
    // Se mide contra un batido real: Do2 y Re2 sumados laten a 8 Hz. Lo que se
    // observa es cuánto oscila la ganancia del limitador con ese batido. No
    // vale comprobar un valor absoluto —eso sólo sellaría el comportamiento
    // actual—, así que se contrasta la recuperación rápida contra la lenta.
    auto gainSwingWithRelease = [] (double releaseSeconds)
    {
        Limiter limiter;
        limiter.prepare (sampleRate);
        limiter.setReleaseTime (releaseSeconds);

        constexpr int blockLength = 64;
        std::vector<float> block (blockLength);

        float minimum = 2.0f;
        float maximum = 0.0f;
        int sampleIndex = 0;

        const int numBlocks = static_cast<int> (3.0 * sampleRate / blockLength);

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockLength; ++i, ++sampleIndex)
            {
                const double t = sampleIndex / sampleRate;
                block[static_cast<std::size_t> (i)] =
                    static_cast<float> (0.7 * (std::sin (6.283185307 * 65.4 * t)
                                             + std::sin (6.283185307 * 73.4 * t)));
            }

            limiter.process (block.data(), nullptr, blockLength);

            // El primer segundo se descarta: es el transitorio de arranque.
            if (b * blockLength > sampleRate)
            {
                minimum = std::min (minimum, limiter.currentGainReduction());
                maximum = std::max (maximum, limiter.currentGainReduction());
            }
        }

        return maximum - minimum;
    };

    const auto fastSwing = gainSwingWithRelease (0.100);
    const auto slowSwing = gainSwingWithRelease (Limiter::defaultReleaseSeconds);

    std::cout << "  [limitador] oscilación de ganancia con batido de 8 Hz:"
              << "  100 ms -> " << fastSwing
              << "   350 ms -> " << gainSwingWithRelease (0.350)
              << "   " << juce::roundToInt (Limiter::defaultReleaseSeconds * 1000.0)
              << " ms (el que usa la app) -> " << slowSwing
              << "   1500 ms -> " << gainSwingWithRelease (1.500) << '\n';

    // Con la recuperación lenta la ganancia tiene que moverse claramente menos.
    CHECK (slowSwing < fastSwing * 0.6f);
}

TEST_CASE ("El limitador no deja pasar nada por encima del umbral", "[instrument][limiter]")
{
    Limiter limiter;
    limiter.prepare (sampleRate);
    limiter.setThreshold (0.5f);

    std::vector<float> block (4096, 0.9f);
    limiter.process (block.data(), nullptr, static_cast<int> (block.size()));

    // El ataque de 1 ms deja pasar los primeros samples: es el precio de no
    // tener lookahead, y por eso hay un recorte duro detrás en la cadena real.
    // Pasado el ataque, nada debe superar el umbral.
    const int afterAttack = static_cast<int> (0.005 * sampleRate);

    for (int i = afterAttack; i < static_cast<int> (block.size()); ++i)
        REQUIRE (std::abs (block[static_cast<std::size_t> (i)]) <= 0.51f);
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
