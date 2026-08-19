// El profesor entero es lógica pura sobre alturas: se puede practicar un
// ejercicio completo desde un test, sin teclado, sin audio y sin ventana. Es
// justo lo que el doc 01 §1.8 decía que había que conseguir.

#include <core/exercise/ExerciseRunner.h>
#include <core/score/Exercise.h>
#include <core/music/Pitch.h>
#include <core/score/Exercise.h>
#include <core/score/ExerciseGenerator.h>

#include <catch2/catch_test_macros.hpp>

using namespace keyla::core;

namespace
{
    /** Toca el ejercicio entero sin fallar ni una. */
    void playPerfectly (ExerciseRunner& runner)
    {
        double time = 0.0;

        while (! runner.isFinished())
        {
            const auto* event = runner.currentEvent();
            REQUIRE (event != nullptr);

            for (auto pitch : event->pitches)
            {
                runner.noteOn (pitch, time);
                time += 0.5;
            }

            for (auto pitch : event->pitches)
                runner.noteOff (pitch);
        }
    }
}

// ── Generador ───────────────────────────────────────────────────────────────

TEST_CASE ("Una escala mayor de una octava sube y baja", "[exercise][generator]")
{
    const auto scale = generateScale ({ 60, ScaleType::major, 1, Hand::right, true });

    // 7 grados + tónica de arriba = 8 subiendo, y 7 más bajando sin repetir la
    // de arriba.
    CHECK (scale.events.size() == 15);
    CHECK (scale.events.front().pitches.front() == 60);
    CHECK (scale.events.back().pitches.front() == 60);

    // Do-Re-Mi-Fa-Sol-La-Si-Do
    const std::vector<int> expectedAscending { 60, 62, 64, 65, 67, 69, 71, 72 };

    for (std::size_t i = 0; i < expectedAscending.size(); ++i)
        CHECK (scale.events[i].pitches.front() == expectedAscending[i]);

    CHECK (scale.lowestPitch() == 60);
    CHECK (scale.highestPitch() == 72);
}

TEST_CASE ("La nota de arriba no se toca dos veces al dar la vuelta", "[exercise][generator]")
{
    const auto scale = generateScale ({ 60, ScaleType::major, 1, Hand::right, true });

    // El error clásico del generador: repetir el Do agudo al empezar a bajar.
    CHECK (scale.events[7].pitches.front() == 72);
    CHECK (scale.events[8].pitches.front() == 71);
}

TEST_CASE ("Sin descenso, la escala sólo sube", "[exercise][generator]")
{
    const auto scale = generateScale ({ 60, ScaleType::major, 1, Hand::right, false });

    CHECK (scale.events.size() == 8);
    CHECK (scale.events.back().pitches.front() == 72);
}

TEST_CASE ("Dos octavas son el doble de grados, no el doble de eventos", "[exercise][generator]")
{
    const auto scale = generateScale ({ 60, ScaleType::major, 2, Hand::right, true });

    CHECK (scale.events.size() == 29);      // 15 subiendo + 14 bajando
    CHECK (scale.highestPitch() == 84);
}

TEST_CASE ("La digitación de la escala de siete notas es la de siempre", "[exercise][generator]")
{
    const auto right = generateScale ({ 60, ScaleType::major, 1, Hand::right, false });

    // 1-2-3, pulgar por debajo, 1-2-3-4, y el 5 al cerrar.
    const std::vector<int> expected { 1, 2, 3, 1, 2, 3, 4, 5 };

    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        REQUIRE (right.events[i].fingers.size() == 1);
        CHECK (right.events[i].fingers.front() == expected[i]);
    }

    // La izquierda no es la derecha del revés en los números: empieza por el 5.
    const auto left = generateScale ({ 60, ScaleType::major, 1, Hand::left, false });
    CHECK (left.events.front().fingers.front() == 5);
    CHECK (left.events.back().fingers.front() == 1);
}

TEST_CASE ("Sin digitación conocida no se inventa ninguna", "[exercise][generator]")
{
    // Inventarse la digitación de una cromática o de una pentatónica sería
    // enseñar a tocar mal, y el alumno la seguiría.
    for (auto type : { ScaleType::chromatic, ScaleType::pentatonicMinor, ScaleType::blues })
    {
        const auto scale = generateScale ({ 60, type, 1, Hand::right, false });

        for (const auto& event : scale.events)
            CHECK (event.fingers.empty());
    }

    // Y los arpegios tampoco: sus digitaciones dependen de la inversión.
    const auto arpeggio = generateArpeggio ({ 60, ArpeggioType::major, 1, Hand::right, false });

    for (const auto& event : arpeggio.events)
        CHECK (event.fingers.empty());
}

TEST_CASE ("Los ejercicios de fábrica son válidos y tienen nombre", "[exercise][generator]")
{
    const auto list = defaultExercises();

    REQUIRE (! list.empty());

    for (const auto& exercise : list)
    {
        INFO ("ejercicio " << exercise.id.toStdString());
        CHECK (! exercise.isEmpty());
        CHECK (exercise.name.isNotEmpty());
        CHECK (exercise.id.isNotEmpty());
        CHECK (exercise.lowestPitch() >= 0);
        CHECK (exercise.highestPitch() <= 127);
    }
}

// ── Adaptación al teclado (doc 01 §1.1) ─────────────────────────────────────

TEST_CASE ("Lo que cabe se deja donde está", "[exercise][range]")
{
    auto scale = generateScale ({ 60, ScaleType::major, 1, Hand::right, true });
    const auto fit = fitToKeyboardRange (scale, 36, 84);

    CHECK (fit.outcome == RangeFit::Outcome::fits);
    CHECK (fit.octavesMoved == 0);
    CHECK (scale.lowestPitch() == 60);
}

TEST_CASE ("Lo que no cabe se mueve por octavas, no por semitonos", "[exercise][range]")
{
    // Mover una escala de Do a Do sostenido cambiaría la digitación y dejaría
    // de ser el ejercicio que el alumno quería practicar.
    auto scale = generateScale ({ 84, ScaleType::major, 1, Hand::right, true });
    REQUIRE (scale.highestPitch() == 96);

    const auto fit = fitToKeyboardRange (scale, 36, 84);

    CHECK (fit.outcome == RangeFit::Outcome::transposed);
    CHECK (fit.octavesMoved == -1);
    CHECK (scale.lowestPitch() == 72);
    CHECK (scale.highestPitch() == 84);
    CHECK (pitchClassOf (scale.lowestPitch()) == 0);        // sigue siendo Do
    CHECK (fit.explanation.isNotEmpty());
}

TEST_CASE ("Lo que no cabe ni moviéndolo se dice, no se recorta", "[exercise][range]")
{
    // Marcar como omitidas unas notas que no se pueden tocar físicamente es de
    // las cosas que hacen desinstalar una aplicación.
    auto scale = generateScale ({ 36, ScaleType::major, 4, Hand::right, true });

    const auto fit = fitToKeyboardRange (scale, 60, 72);

    CHECK (fit.outcome == RangeFit::Outcome::doesNotFit);
    CHECK (fit.explanation.isNotEmpty());
    CHECK (scale.lowestPitch() == 36);      // intacto: no se recorta nada
}

// ── Modo espera ─────────────────────────────────────────────────────────────

TEST_CASE ("Tocando bien, el ejercicio avanza hasta el final", "[exercise][runner]")
{
    ExerciseRunner runner;
    runner.start (generateScale ({ 60, ScaleType::major, 1, Hand::right, true }), 0.0);

    REQUIRE (runner.isRunning());
    CHECK (runner.currentEventIndex() == 0);

    playPerfectly (runner);

    CHECK (runner.isFinished());
    CHECK (! runner.isRunning());

    const auto report = runner.report();
    CHECK (report.totalEvents == 15);
    CHECK (report.cleanEvents == 15);
    CHECK (report.eventsWithMistakes == 0);
    CHECK (report.wrongNotes.empty());
}

TEST_CASE ("El cursor NO avanza con una nota equivocada", "[exercise][runner]")
{
    // Es la definición del modo espera: el reloj no corre y no se pasa de
    // pantalla hasta que aciertas.
    ExerciseRunner runner;
    runner.start (generateScale ({ 60, ScaleType::major, 1, Hand::right, false }), 0.0);

    CHECK (! runner.noteOn (61, 0.0));      // Do sostenido en vez de Do
    CHECK (runner.currentEventIndex() == 0);

    CHECK (! runner.noteOn (59, 0.1));
    CHECK (runner.currentEventIndex() == 0);

    CHECK (runner.noteOn (60, 0.2));        // ahora sí
    CHECK (runner.currentEventIndex() == 1);
}

TEST_CASE ("Se recuerda qué se tocó, no sólo que se falló", "[exercise][runner]")
{
    // "Un semitono abajo" es un diagnóstico; "incorrecta" no lo es.
    ExerciseRunner runner;
    runner.start (generateScale ({ 60, ScaleType::major, 1, Hand::right, false }), 0.0);

    runner.noteOn (60, 0.0);        // Do, bien
    runner.noteOn (61, 0.1);        // esperaba Re, tocó Do sostenido

    REQUIRE (runner.hasLastWrongNote());
    const auto wrong = runner.lastWrongNote();

    CHECK (wrong.playedPitch == 61);
    CHECK (wrong.expectedPitch == 62);
    CHECK (wrong.semitonesOff == -1);
}

TEST_CASE ("Un evento fallado y luego acertado no cuenta como limpio", "[exercise][runner]")
{
    ExerciseRunner runner;
    runner.start (generateScale ({ 60, ScaleType::major, 1, Hand::right, false }), 0.0);

    runner.noteOn (61, 0.0);        // mal
    runner.noteOn (60, 0.1);        // bien, pero ya se había fallado

    while (! runner.isFinished())
    {
        const auto* event = runner.currentEvent();
        runner.noteOn (event->pitches.front(), 1.0);
    }

    const auto report = runner.report();
    CHECK (report.eventsWithMistakes == 1);
    CHECK (report.cleanEvents == 7);
}

TEST_CASE ("Los fallos seguidos en el mismo sitio se cuentan", "[exercise][runner]")
{
    // La pantalla usa esto para ofrecer la pista sólo cuando hace falta, en vez
    // de estar gritando siempre.
    ExerciseRunner runner;
    runner.start (generateScale ({ 60, ScaleType::major, 1, Hand::right, false }), 0.0);

    CHECK (runner.consecutiveMistakesHere() == 0);

    runner.noteOn (61, 0.0);
    runner.noteOn (63, 0.1);
    CHECK (runner.consecutiveMistakesHere() == 2);

    runner.noteOn (60, 0.2);
    CHECK (runner.consecutiveMistakesHere() == 0);      // se reinicia al avanzar
}

TEST_CASE ("Un acorde se puede montar nota a nota", "[exercise][runner][chord]")
{
    // Notas que deberían sonar a la vez nunca suenan a la vez, y estudiando se
    // montan de una en una. Exigir simultaneidad haría el ejercicio imposible.
    Exercise chordExercise;
    ExpectedEvent chord;
    chord.pitches = { 60, 64, 67 };
    chordExercise.events.push_back (chord);

    ExerciseRunner runner;
    runner.start (chordExercise, 0.0);

    CHECK (! runner.noteOn (64, 0.0));
    CHECK (runner.pendingPitches().size() == 2);

    CHECK (! runner.noteOn (60, 0.1));
    CHECK (runner.pendingPitches().size() == 1);

    CHECK (runner.noteOn (67, 0.2));        // completo: avanza
    CHECK (runner.isFinished());

    CHECK (runner.report().cleanEvents == 1);
}

TEST_CASE ("Repetir una nota del acorde no lo completa", "[exercise][runner][chord]")
{
    Exercise chordExercise;
    ExpectedEvent chord;
    chord.pitches = { 60, 64, 67 };
    chordExercise.events.push_back (chord);

    ExerciseRunner runner;
    runner.start (chordExercise, 0.0);

    runner.noteOn (60, 0.0);
    runner.noteOff (60);
    CHECK (! runner.noteOn (60, 0.1));
    CHECK (! runner.isFinished());
    CHECK (runner.pendingPitches().size() == 2);
}

TEST_CASE ("El informe señala dónde te trabaste, no un porcentaje", "[exercise][runner][report]")
{
    ExerciseRunner runner;
    runner.start (generateScale ({ 60, ScaleType::major, 1, Hand::right, false }), 0.0);

    // Se falla dos veces contra el Fa y una contra el Sol.
    runner.noteOn (60, 0.0);
    runner.noteOn (62, 0.1);
    runner.noteOn (64, 0.2);
    runner.noteOn (66, 0.3);        // esperaba Fa
    runner.noteOn (66, 0.4);        // otra vez
    runner.noteOn (65, 0.5);        // Fa, por fin
    runner.noteOn (68, 0.6);        // esperaba Sol
    runner.noteOn (67, 0.7);

    while (! runner.isFinished())
        runner.noteOn (runner.currentEvent()->pitches.front(), 1.0);

    const auto report = runner.report();
    const auto spots = report.troubleSpots();

    REQUIRE (! spots.empty());
    CHECK (spots.front() == 65);            // el Fa, con dos fallos, va primero
    CHECK (report.wrongNotes.size() == 3);

    const auto summary = report.summary();
    CHECK (summary.isNotEmpty());
    CHECK (summary.contains ("F4"));        // dice dónde, no un tanto por ciento
    CHECK (! summary.contains ("%"));
}

TEST_CASE ("Un intento perfecto se reconoce sin exagerar", "[exercise][runner][report]")
{
    ExerciseRunner runner;
    runner.start (generateScale ({ 60, ScaleType::major, 1, Hand::right, false }), 0.0);
    playPerfectly (runner);

    const auto summary = runner.report().summary();
    CHECK (summary.isNotEmpty());
    CHECK (! summary.contains ("%"));
}

TEST_CASE ("Un ejercicio vacío no rompe nada", "[exercise][runner]")
{
    ExerciseRunner runner;
    runner.start (Exercise {}, 0.0);

    CHECK (runner.isFinished());
    CHECK (! runner.isRunning());
    CHECK (runner.currentEvent() == nullptr);
    CHECK (! runner.noteOn (60, 0.0));
    CHECK (runner.report().totalEvents == 0);
}
