// Aquí es donde el proyecto se juega si vale algo. El riesgo de un evaluador
// no es clasificar, es **alinear**: el enfoque ingenuo de índice contra índice
// se rompe en cuanto el alumno omite o añade una nota, y a partir de ahí todo
// el informe es basura (doc 01 §2.2).
//
// Todo esto es una función pura sobre datos: se prueba con listas de números.

#include <core/evaluation/Metrics.h>
#include <core/evaluation/OfflineAligner.h>
#include <core/recording/SessionRecorder.h>
#include <core/score/ExerciseGenerator.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace keyla::core;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;

    /** Un segundo por pulso: números redondos para poder razonar a mano. */
    constexpr double beatsToSeconds = 1.0;

    std::vector<ExpectedEvent> expectedLine (const std::vector<int>& pitches)
    {
        std::vector<ExpectedEvent> events;

        for (std::size_t i = 0; i < pitches.size(); ++i)
        {
            ExpectedEvent event;
            event.pitches = { static_cast<std::uint8_t> (pitches[i]) };
            event.onsetBeat = static_cast<double> (i);
            events.push_back (event);
        }

        return events;
    }

    /** Notas tocadas con un desfase en ms respecto a su pulso, una por evento. */
    std::vector<PlayedEvent> playedLine (const std::vector<int>& pitches,
                                         const std::vector<double>& offsetsMs,
                                         int velocity = 90)
    {
        std::vector<PlayedEvent> events;

        for (std::size_t i = 0; i < pitches.size(); ++i)
        {
            const double offsetMs = i < offsetsMs.size() ? offsetsMs[i] : 0.0;

            RecordedNote note;
            note.pitch = pitches[i];
            note.velocity = velocity;
            note.onsetSample = (static_cast<double> (i) + offsetMs / 1000.0) * sampleRate;
            note.offsetSample = note.onsetSample + sampleRate * 0.4;

            PlayedEvent event;
            event.notes.push_back (note);
            events.push_back (event);
        }

        return events;
    }
}

// ── Grabación ───────────────────────────────────────────────────────────────

TEST_CASE ("La grabación conserva la posición fraccionaria", "[evaluation][recorder]")
{
    // Invariante 3: la precisión de medida no está limitada por el buffer.
    SessionRecorder recorder;
    recorder.start (sampleRate);

    recorder.noteOn (60, 100, 1234.75);
    recorder.noteOff (60, 5678.25);
    recorder.stop (10000.0);

    REQUIRE (recorder.notes().size() == 1);
    CHECK_THAT (recorder.notes().front().onsetSample, WithinAbs (1234.75, 1e-9));
    CHECK_THAT (recorder.notes().front().offsetSample, WithinAbs (5678.25, 1e-9));
}

TEST_CASE ("Las notas que seguían pulsadas al parar se cierran", "[evaluation][recorder]")
{
    // Dejarlas abiertas haría que su duración fuese cero, y el informe diría
    // que se soltaron al instante: justo lo contrario de lo que pasó.
    SessionRecorder recorder;
    recorder.start (sampleRate);
    recorder.noteOn (60, 100, 0.0);
    recorder.stop (48000.0);

    REQUIRE (recorder.notes().size() == 1);
    CHECK (recorder.notes().front().isFinished());
    CHECK_THAT (recorder.notes().front().durationSamples(), WithinAbs (48000.0, 1e-9));
}

TEST_CASE ("Repetir una nota antes de soltarla no descuadra las duraciones", "[evaluation][recorder]")
{
    SessionRecorder recorder;
    recorder.start (sampleRate);

    recorder.noteOn (60, 100, 0.0);
    recorder.noteOn (60, 100, 1000.0);      // otra vez, sin soltar
    recorder.noteOff (60, 2000.0);          // cierra la más reciente

    REQUIRE (recorder.notes().size() == 2);
    CHECK (! recorder.notes()[0].isFinished());
    CHECK_THAT (recorder.notes()[1].offsetSample, WithinAbs (2000.0, 1e-9));
}

TEST_CASE ("Un acorde abierto se agrupa; una escala rápida no", "[evaluation][recorder][chord]")
{
    SessionRecorder recorder;
    recorder.start (sampleRate);

    // Acorde de tres notas repartido en 25 ms: es un acorde, no tres errores.
    // Una mano humana rara vez abre más que esto.
    recorder.noteOn (60, 90, 0.0);
    recorder.noteOn (64, 90, sampleRate * 0.011);
    recorder.noteOn (67, 90, sampleRate * 0.025);

    // Y una nota mucho después: evento aparte.
    recorder.noteOn (72, 90, sampleRate * 0.500);
    recorder.stop (sampleRate);

    const auto events = recorder.groupIntoEvents();

    REQUIRE (events.size() == 2);
    CHECK (events[0].notes.size() == 3);
    CHECK (events[1].notes.size() == 1);
}

TEST_CASE ("Una escala rápida no se convierte en un solo acorde gigante", "[evaluation][recorder][chord]")
{
    // Dos errores posibles y los dos dan lo mismo de mal: comparar contra la
    // nota anterior en vez de contra el inicio del grupo —encadenando, treinta
    // notas seguidas acabarían en un solo acorde—, o una tolerancia tan ancha
    // que se coma el pasaje.
    //
    // 60 ms entre notas son unas 16 por segundo: una escala rápida pero
    // perfectamente normal. Cada nota tiene que ser su propio evento.
    SessionRecorder recorder;
    recorder.start (sampleRate);

    for (int i = 0; i < 10; ++i)
        recorder.noteOn (60 + i, 90, sampleRate * 0.060 * i);

    recorder.stop (sampleRate);

    CHECK (recorder.groupIntoEvents().size() == 10);
}

TEST_CASE ("Por encima de cierta velocidad, acorde y pasaje son lo mismo", "[evaluation][recorder][chord]")
{
    // Este test no comprueba que el código acierte: comprueba que el límite es
    // el que creemos que es. A 25 ms entre notas —40 por segundo— ninguna regla
    // basada sólo en el reloj puede distinguir una escala de un racimo, porque
    // no hay nada que las distinga. Resolverlo exige saber qué estaba escrito,
    // y eso llega con el modelo de partitura.
    SessionRecorder recorder;
    recorder.start (sampleRate);

    for (int i = 0; i < 6; ++i)
        recorder.noteOn (60 + i, 90, sampleRate * 0.025 * i);

    recorder.stop (sampleRate);

    CHECK (recorder.groupIntoEvents().size() < 6);
}

// ── Alineación ──────────────────────────────────────────────────────────────

TEST_CASE ("Una ejecución perfecta se empareja una a una", "[evaluation][align]")
{
    const auto expected = expectedLine ({ 60, 62, 64, 65 });
    const auto played = playedLine ({ 60, 62, 64, 65 }, { 0, 0, 0, 0 });

    const auto alignment = alignPerformance (played, expected, sampleRate, beatsToSeconds);

    CHECK (alignment.matched() == 4);
    CHECK (alignment.omitted() == 0);
    CHECK (alignment.extra() == 0);

    for (const auto& pair : alignment.pairs)
        CHECK (pair.label == NoteLabel::correct);
}

TEST_CASE ("Omitir una nota NO descuadra todo lo que viene después", "[evaluation][align]")
{
    // Éste es el test que justifica la distancia de edición. Con índice contra
    // índice, saltarse el Mi haría que Fa, Sol y La salieran todos como altura
    // incorrecta, y el informe sería basura.
    const auto expected = expectedLine ({ 60, 62, 64, 65, 67 });

    std::vector<PlayedEvent> played;
    const std::vector<int> pitches { 60, 62, 65, 67 };
    const std::vector<double> beats { 0.0, 1.0, 3.0, 4.0 };

    for (std::size_t i = 0; i < pitches.size(); ++i)
    {
        RecordedNote note;
        note.pitch = pitches[i];
        note.velocity = 90;
        note.onsetSample = beats[i] * sampleRate;

        PlayedEvent event;
        event.notes.push_back (note);
        played.push_back (event);
    }

    const auto alignment = alignPerformance (played, expected, sampleRate, beatsToSeconds);

    CHECK (alignment.omitted() == 1);
    CHECK (alignment.matched() == 4);
    CHECK (alignment.wrongPitch() == 0);        // lo demás sigue bien emparejado

    // Y se sabe *cuál* faltó.
    bool foundMissingE = false;

    for (const auto& pair : alignment.pairs)
        if (pair.label == NoteLabel::omitted && pair.expectedPitch == 64)
            foundMissingE = true;

    CHECK (foundMissingE);
}

TEST_CASE ("Una nota de más se marca como añadida y no arrastra al resto", "[evaluation][align]")
{
    const auto expected = expectedLine ({ 60, 62, 64 });

    std::vector<PlayedEvent> played;
    const std::vector<int> pitches { 60, 61, 62, 64 };
    const std::vector<double> beats { 0.0, 0.5, 1.0, 2.0 };

    for (std::size_t i = 0; i < pitches.size(); ++i)
    {
        RecordedNote note;
        note.pitch = pitches[i];
        note.velocity = 90;
        note.onsetSample = beats[i] * sampleRate;

        PlayedEvent event;
        event.notes.push_back (note);
        played.push_back (event);
    }

    const auto alignment = alignPerformance (played, expected, sampleRate, beatsToSeconds);

    CHECK (alignment.extra() == 1);
    CHECK (alignment.matched() == 3);
}

TEST_CASE ("Una altura equivocada se reporta con su intervalo", "[evaluation][align]")
{
    // "Un semitono abajo" es diagnóstico; "incorrecta" no lo es (doc 02 §5).
    const auto expected = expectedLine ({ 60, 62, 64 });
    const auto played = playedLine ({ 60, 61, 64 }, { 0, 0, 0 });

    const auto alignment = alignPerformance (played, expected, sampleRate, beatsToSeconds);

    bool found = false;

    for (const auto& pair : alignment.pairs)
    {
        if (pair.label != NoteLabel::wrongPitch)
            continue;

        found = true;
        CHECK (pair.expectedPitch == 62);
        CHECK (pair.playedPitch == 61);
        CHECK (pair.semitonesOff == -1);
    }

    CHECK (found);
}

TEST_CASE ("Adelantarse y atrasarse se distinguen, con signo", "[evaluation][align]")
{
    const auto expected = expectedLine ({ 60, 62, 64 });
    const auto played = playedLine ({ 60, 62, 64 }, { -120.0, 0.0, 130.0 });

    const auto alignment = alignPerformance (played, expected, sampleRate, beatsToSeconds);

    REQUIRE (alignment.pairs.size() == 3);
    CHECK (alignment.pairs[0].label == NoteLabel::early);
    CHECK (alignment.pairs[1].label == NoteLabel::correct);
    CHECK (alignment.pairs[2].label == NoteLabel::late);

    CHECK (alignment.pairs[0].timingErrorMs < 0.0);
    CHECK (alignment.pairs[2].timingErrorMs > 0.0);
}

TEST_CASE ("El offset perceptual se resta ANTES de reportar nada", "[evaluation][align][offset]")
{
    // Invariante 7 y doc 01 §1.4. Sin esto, todo alumno aparece sistemáticamente
    // tarde por una constante de 8-10 ms que no tiene nada que ver con su ritmo,
    // y la función estrella del producto miente.
    const auto expected = expectedLine ({ 60, 62, 64, 65 });

    // Toca 9 ms tarde: exactamente la latencia del sistema, no un error suyo.
    const auto played = playedLine ({ 60, 62, 64, 65 }, { 9.0, 9.0, 9.0, 9.0 });

    AlignmentOptions withoutOffset;
    const auto naive = alignPerformance (played, expected, sampleRate, beatsToSeconds, withoutOffset);

    AlignmentOptions withOffset;
    withOffset.perceptualOffsetMs = 9.0;
    const auto corrected = alignPerformance (played, expected, sampleRate, beatsToSeconds, withOffset);

    // Sin corregir, sale tarde en todas.
    CHECK (naive.pairs.front().timingErrorMs > 8.0);

    // Corrigiendo, sale clavado.
    CHECK_THAT (corrected.pairs.front().timingErrorMs, WithinAbs (0.0, 1e-6));

    const auto metrics = computeMetrics (corrected, played, sampleRate);
    CHECK_THAT (metrics.timingBiasMs, WithinAbs (0.0, 1e-6));
}

TEST_CASE ("Un acorde tocado abierto es un acorde, no tres errores", "[evaluation][align][chord]")
{
    std::vector<ExpectedEvent> expected;
    ExpectedEvent chord;
    chord.pitches = { 60, 64, 67 };
    chord.onsetBeat = 0.0;
    expected.push_back (chord);

    // Arpegiado en 30 ms, que es lo normal en una mano humana.
    PlayedEvent played;

    for (auto [pitch, offset] : { std::pair<int, double> { 60, 0.0 },
                                  { 64, 0.014 }, { 67, 0.030 } })
    {
        RecordedNote note;
        note.pitch = pitch;
        note.velocity = 90;
        note.onsetSample = offset * sampleRate;
        played.notes.push_back (note);
    }

    const auto alignment = alignPerformance ({ played }, expected, sampleRate, beatsToSeconds);

    REQUIRE (alignment.pairs.size() == 1);
    CHECK (alignment.pairs.front().label == NoteLabel::correct);
    CHECK (alignment.wrongPitch() == 0);
}

TEST_CASE ("Alinear contra nada no revienta", "[evaluation][align]")
{
    CHECK (alignPerformance ({}, {}, sampleRate, beatsToSeconds).pairs.empty());

    const auto onlyExpected = alignPerformance ({}, expectedLine ({ 60, 62 }),
                                                sampleRate, beatsToSeconds);
    CHECK (onlyExpected.omitted() == 2);

    const auto onlyPlayed = alignPerformance (playedLine ({ 60, 62 }, { 0, 0 }), {},
                                              sampleRate, beatsToSeconds);
    CHECK (onlyPlayed.extra() == 2);
}

// ── Métricas ────────────────────────────────────────────────────────────────

TEST_CASE ("El sesgo distingue adelantarse siempre de ir errático", "[evaluation][metrics]")
{
    // Dos alumnos con el mismo error absoluto medio y problemas opuestos: uno
    // se adelanta siempre —se corrige escuchando— y el otro va disperso —se
    // corrige bajando el tempo—. Un porcentaje de acierto los daría iguales.
    const auto expected = expectedLine ({ 60, 62, 64, 65, 67, 69 });

    const auto consistentlyEarly = playedLine ({ 60, 62, 64, 65, 67, 69 },
                                               { -30, -32, -28, -31, -29, -30 });
    const auto erratic = playedLine ({ 60, 62, 64, 65, 67, 69 },
                                     { -30, 31, -29, 30, -31, 29 });

    const auto earlyMetrics = computeMetrics (
        alignPerformance (consistentlyEarly, expected, sampleRate, beatsToSeconds),
        consistentlyEarly, sampleRate);

    const auto erraticMetrics = computeMetrics (
        alignPerformance (erratic, expected, sampleRate, beatsToSeconds),
        erratic, sampleRate);

    // El que se adelanta siempre: sesgo grande, dispersión pequeña.
    CHECK (earlyMetrics.timingBiasMs < -25.0);
    CHECK (earlyMetrics.consistencyMs < 5.0);

    // El errático: sesgo casi cero, dispersión enorme.
    CHECK (std::abs (erraticMetrics.timingBiasMs) < 5.0);
    CHECK (erraticMetrics.consistencyMs > 25.0);
}

TEST_CASE ("La deriva detecta lo que el error medio esconde", "[evaluation][metrics]")
{
    // Adelantarse al principio y atrasarse al final da un error medio de cero.
    // La pendiente es lo único que lo ve.
    const auto expected = expectedLine ({ 60, 62, 64, 65, 67, 69, 71 });
    const auto slowingDown = playedLine ({ 60, 62, 64, 65, 67, 69, 71 },
                                         { -30, -20, -10, 0, 10, 20, 30 });

    const auto metrics = computeMetrics (
        alignPerformance (slowingDown, expected, sampleRate, beatsToSeconds),
        slowingDown, sampleRate);

    CHECK_THAT (metrics.timingBiasMs, WithinAbs (0.0, 1.0));    // el medio no ve nada
    CHECK (metrics.tempoDriftMsPerSecond > 5.0);                // la pendiente sí
}

TEST_CASE ("La regularidad mide los intervalos, no el error contra el pulso", "[evaluation][metrics]")
{
    // Alguien puede tocar toda una escala a un tempo distinto del pedido y ser
    // perfectamente regular. Son dos cosas distintas y hay que medirlas aparte.
    const auto expected = expectedLine ({ 60, 62, 64, 65, 67 });

    std::vector<PlayedEvent> steadyButSlow;

    for (int i = 0; i < 5; ++i)
    {
        RecordedNote note;
        note.pitch = expected[static_cast<std::size_t> (i)].pitches.front();
        note.velocity = 90;
        note.onsetSample = i * 1.10 * sampleRate;       // 10 % más lento, pero clavado
        PlayedEvent event;
        event.notes.push_back (note);
        steadyButSlow.push_back (event);
    }

    const auto metrics = computeMetrics (
        alignPerformance (steadyButSlow, expected, sampleRate, beatsToSeconds),
        steadyButSlow, sampleRate);

    CHECK_THAT (metrics.regularityMs, WithinAbs (0.0, 1.0));    // regularísimo
    CHECK (metrics.tempoDriftMsPerSecond > 0.0);                // pero se va quedando atrás
}

TEST_CASE ("Con muy pocas notas no se inventa una estadística", "[evaluation][metrics]")
{
    // Una desviación típica sobre tres muestras no significa nada, y darla como
    // si significara algo es peor que no darla.
    const auto expected = expectedLine ({ 60, 62 });
    const auto played = playedLine ({ 60, 62 }, { 5, -5 });

    const auto metrics = computeMetrics (
        alignPerformance (played, expected, sampleRate, beatsToSeconds), played, sampleRate);

    CHECK (! metrics.hasTimingData());

    const auto lines = describeMetrics (metrics);
    REQUIRE (! lines.isEmpty());

    bool saysNotEnough = false;

    for (const auto& line : lines)
        if (line.containsIgnoreCase ("pocas"))
            saysNotEnough = true;

    CHECK (saysNotEnough);
}

TEST_CASE ("La uniformidad de velocity delata los dedos débiles", "[evaluation][metrics]")
{
    const auto expected = expectedLine ({ 60, 62, 64, 65, 67 });

    std::vector<PlayedEvent> uneven;
    const std::vector<int> velocities { 100, 98, 102, 45, 40 };   // 4º y 5º flojos

    for (std::size_t i = 0; i < velocities.size(); ++i)
    {
        RecordedNote note;
        note.pitch = expected[i].pitches.front();
        note.velocity = velocities[i];
        note.onsetSample = static_cast<double> (i) * sampleRate;
        PlayedEvent event;
        event.notes.push_back (note);
        uneven.push_back (event);
    }

    const auto metrics = computeMetrics (
        alignPerformance (uneven, expected, sampleRate, beatsToSeconds), uneven, sampleRate);

    CHECK (metrics.velocitySpread > 25.0);

    bool mentionsFingers = false;

    for (const auto& line : describeMetrics (metrics))
        if (line.containsIgnoreCase ("fuertes"))
            mentionsFingers = true;

    CHECK (mentionsFingers);
}

TEST_CASE ("El informe no empieza por un número ni da porcentajes", "[evaluation][metrics]")
{
    // "Tu error medio es 23 ms" es verdadero e inútil (doc 01 §1.6).
    const auto expected = expectedLine ({ 60, 62, 64, 65, 67, 69 });
    const auto played = playedLine ({ 60, 62, 64, 65, 67, 69 },
                                    { -30, -32, -28, -31, -29, -30 });

    const auto metrics = computeMetrics (
        alignPerformance (played, expected, sampleRate, beatsToSeconds), played, sampleRate);

    const auto lines = describeMetrics (metrics);
    REQUIRE (! lines.isEmpty());

    for (const auto& line : lines)
        CHECK (! line.contains ("%"));

    // Y dice qué hacer, no sólo qué pasó.
    bool actionable = false;

    for (const auto& line : lines)
        if (line.containsIgnoreCase ("adelantas"))
            actionable = true;

    CHECK (actionable);
}
