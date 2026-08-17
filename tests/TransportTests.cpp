// El modelo temporal es la pieza que sostiene todo lo demás (doc 02 §3). Si
// esto tiene un error de un sample, la evaluación rítmica miente.

#include <core/time/Transport.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace keyla::core;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    Transport makeTransport (double sampleRate = 48000.0, double bpm = 120.0)
    {
        Transport t;
        t.prepare (sampleRate);
        t.setTempo (bpm);
        return t;
    }
}

TEST_CASE ("El compás sabe cuántas negras dura", "[time]")
{
    CHECK (TimeSignature { 4, 4 }.beatsPerBar() == 4.0);
    CHECK (TimeSignature { 3, 4 }.beatsPerBar() == 3.0);

    // 6/8 son tres negras por compás, no seis. Seis son los *pulsos*.
    CHECK (TimeSignature { 6, 8 }.beatsPerBar() == 3.0);
    CHECK (TimeSignature { 6, 8 }.pulsesPerBar() == 6);
    CHECK (TimeSignature { 6, 8 }.beatsPerPulse() == 0.5);

    CHECK (TimeSignature { 2, 2 }.beatsPerBar() == 4.0);
    CHECK (TimeSignature { 2, 2 }.beatsPerPulse() == 2.0);
}

TEST_CASE ("Un compás con denominador que no es potencia de dos se rechaza", "[time]")
{
    auto t = makeTransport();
    const auto before = t.timeSignature();

    t.setTimeSignature (TimeSignature { 4, 5 });

    CHECK (t.timeSignature() == before);
}

TEST_CASE ("A 120 BPM una negra son medio segundo", "[time]")
{
    auto t = makeTransport (48000.0, 120.0);

    CHECK_THAT (t.samplesPerBeat(), WithinRel (24000.0, 1e-12));
    CHECK_THAT (t.beatToSample (1.0), WithinRel (24000.0, 1e-12));
    CHECK_THAT (t.beatToSample (4.0), WithinRel (96000.0, 1e-12));
}

TEST_CASE ("Sample y beat convierten en los dos sentidos sin perder nada", "[time]")
{
    auto t = makeTransport (44100.0, 137.0);   // valores feos a propósito
    t.setBarZeroSample (1234.5);

    for (double beat : { -8.0, -0.25, 0.0, 0.125, 1.0, 3.75, 512.0, 10000.3 })
        CHECK_THAT (t.sampleToBeat (t.beatToSample (beat)), WithinAbs (beat, 1e-9));

    for (double sample : { 0.0, 1.0, 1234.5, 99999.75 })
        CHECK_THAT (t.beatToSample (t.sampleToBeat (sample)), WithinAbs (sample, 1e-6));
}

TEST_CASE ("La posición fraccionaria sobrevive: es el invariante 3", "[time]")
{
    auto t = makeTransport (48000.0, 120.0);

    // Un evento a mitad de camino entre dos samples enteros no se redondea.
    const double beat = t.sampleToBeat (24000.5);

    CHECK (beat > 1.0);
    CHECK_THAT (beat, WithinAbs (1.0 + 0.5 / 24000.0, 1e-12));
}

TEST_CASE ("El metrónomo no deriva: el pulso N se calcula, no se acumula", "[time][metronome]")
{
    auto t = makeTransport (48000.0, 120.0);
    t.setTimeSignature ({ 4, 4 });

    // Diez minutos de pulsos. Si hubiera acumulación de error, aquí se vería.
    const double lastPulse = 1200.0;
    CHECK_THAT (t.pulseToSample (lastPulse), WithinRel (1200.0 * 24000.0, 1e-12));

    // Y en 6/8 el pulso es la corchea.
    t.setTimeSignature ({ 6, 8 });
    CHECK_THAT (t.pulseToSample (1.0), WithinRel (12000.0, 1e-12));
}

TEST_CASE ("Los acentos de compás caen donde deben", "[time][metronome]")
{
    auto t = makeTransport();
    t.setTimeSignature ({ 4, 4 });

    CHECK (t.isDownbeat (0));
    CHECK (! t.isDownbeat (1));
    CHECK (! t.isDownbeat (3));
    CHECK (t.isDownbeat (4));
    CHECK (t.isDownbeat (400));

    // También antes del origen: una anacrusa no rompe la rejilla.
    CHECK (t.isDownbeat (-4));
    CHECK (! t.isDownbeat (-1));

    t.setTimeSignature ({ 3, 4 });
    CHECK (t.isDownbeat (0));
    CHECK (t.isDownbeat (3));
    CHECK (! t.isDownbeat (4));
}

TEST_CASE ("La posición musical se cuenta desde 1, como la cuenta un músico", "[time]")
{
    auto t = makeTransport();
    t.setTimeSignature ({ 4, 4 });

    auto p = t.beatToPosition (0.0);
    CHECK (p.bar == 1);
    CHECK (p.pulse == 1);
    CHECK_THAT (p.fractionOfPulse, WithinAbs (0.0, 1e-12));

    p = t.beatToPosition (2.5);
    CHECK (p.bar == 1);
    CHECK (p.pulse == 3);
    CHECK_THAT (p.fractionOfPulse, WithinAbs (0.5, 1e-12));

    p = t.beatToPosition (4.0);
    CHECK (p.bar == 2);
    CHECK (p.pulse == 1);

    p = t.beatToPosition (9.75);
    CHECK (p.bar == 3);
    CHECK (p.pulse == 2);
    CHECK_THAT (p.fractionOfPulse, WithinAbs (0.75, 1e-12));
}

TEST_CASE ("Antes del pulso cero los compases no se solapan", "[time]")
{
    auto t = makeTransport();
    t.setTimeSignature ({ 4, 4 });

    // Con división entera, -1 y -4 caerían los dos en el compás 0. Es el fallo
    // clásico de las anacrusas y por eso se usa floor().
    const auto justBefore = t.beatToPosition (-1.0);
    const auto oneBarBefore = t.beatToPosition (-4.0);

    CHECK (justBefore.bar == 0);
    CHECK (justBefore.pulse == 4);
    CHECK (oneBarBefore.bar == 0);
    CHECK (oneBarBefore.pulse == 1);
}

TEST_CASE ("Posición y beat convierten en los dos sentidos", "[time]")
{
    auto t = makeTransport();
    t.setTimeSignature ({ 6, 8 });

    for (double beat : { 0.0, 0.5, 1.25, 3.0, 7.5 })
    {
        const auto position = t.beatToPosition (beat);
        CHECK_THAT (t.positionToBeat (position), WithinAbs (beat, 1e-9));
    }
}

TEST_CASE ("El reloj avanza en samples y no en tiempo del sistema", "[time]")
{
    auto t = makeTransport (48000.0, 120.0);

    CHECK (t.samplePosition() == 0);

    t.advance (144);
    t.advance (144);

    CHECK (t.samplePosition() == 288);
    CHECK_THAT (t.seconds(), WithinRel (288.0 / 48000.0, 1e-12));

    t.advance (-5);                     // absurdo: se ignora
    CHECK (t.samplePosition() == 288);

    t.resetToZero();
    CHECK (t.samplePosition() == 0);
}

TEST_CASE ("Un tempo imposible no envenena el modelo con infinitos", "[time]")
{
    auto t = makeTransport (48000.0, 120.0);

    t.setTempo (0.0);
    CHECK (t.tempo() == 120.0);

    t.setTempo (-60.0);
    CHECK (t.tempo() == 120.0);

    t.setTempo (60.0);
    CHECK (t.tempo() == 60.0);
    CHECK_THAT (t.samplesPerBeat(), WithinRel (48000.0, 1e-12));
}

TEST_CASE ("El pulso cero desplaza la rejilla entera", "[time]")
{
    auto t = makeTransport (48000.0, 120.0);
    t.setBarZeroSample (10000.0);

    CHECK_THAT (t.sampleToBeat (10000.0), WithinAbs (0.0, 1e-12));
    CHECK_THAT (t.beatToSample (0.0), WithinRel (10000.0, 1e-12));
    CHECK_THAT (t.sampleToBeat (0.0), WithinRel (-10000.0 / 24000.0, 1e-12));
}
