#pragma once

#include "../score/Exercise.h"

#include <juce_core/juce_core.h>

#include <set>
#include <vector>

namespace keyla::core
{

/** Cómo terminó cada evento esperado. */
enum class EventOutcome
{
    pending,
    clean,          // acertado a la primera
    withMistakes    // acertado, pero después de tocar otra cosa
};

/** Una nota tocada que no tocaba. Se guarda **qué** se tocó, no sólo que se
    falló: "un semitono abajo" es un diagnóstico, "incorrecta" no lo es
    (doc 02 §5). */
struct WrongNote
{
    int eventIndex { 0 };
    int playedPitch { 0 };
    int expectedPitch { 0 };
    int semitonesOff { 0 };     // con signo: negativo = tocaste más grave
};

/** Informe del intento. Deliberadamente **no** hay porcentaje de aciertos: un
    porcentaje no enseña nada (doc 01 §1.6). Lo que enseña es dónde te trabaste
    y qué tocaste en su lugar. */
struct ExerciseReport
{
    int totalEvents { 0 };
    int cleanEvents { 0 };
    int eventsWithMistakes { 0 };
    std::vector<WrongNote> wrongNotes;
    double secondsTaken { 0.0 };

    /** Las alturas donde más veces te equivocaste, de peor a mejor. Es lo único
        del informe que sirve para decidir qué practicar mañana. */
    std::vector<int> troubleSpots() const;

    /** Una frase honesta. Sin adjetivos vacíos y sin felicitar por respirar. */
    juce::String summary() const;
};

/** La máquina de estados del ejercicio en **modo espera** (doc 02 §5).

    Modo espera significa que el reloj no corre: el cursor no avanza hasta que
    aciertas. Por eso aquí **no se evalúa el ritmo** — evaluar timing donde no
    hay tempo es el error clásico que produce informes que no significan nada.
    La evaluación temporal es otro evaluador y llega con el metrónomo.

    Es una función de estado pura sobre eventos de nota: no sabe de audio, ni de
    hilos, ni de relojes. Por eso se puede testear entera con una lista de
    enteros, que es donde el doc 01 §1.8 dice que está el valor.
*/
class ExerciseRunner
{
public:
    void start (Exercise exerciseToRun, double startSeconds);
    void stop();

    bool isRunning() const noexcept { return running; }
    bool isFinished() const noexcept { return finished; }

    /** Alimenta una nota pulsada. Devuelve true si el cursor avanzó. */
    bool noteOn (int pitch, double seconds);
    void noteOff (int pitch);

    // ── Estado para la pantalla ─────────────────────────────────────────────

    const Exercise& exercise() const noexcept { return current; }
    int currentEventIndex() const noexcept { return cursor; }
    int totalEvents() const noexcept { return static_cast<int> (current.events.size()); }

    /** El evento que toca ahora, o nullptr si terminó. */
    const ExpectedEvent* currentEvent() const noexcept;

    /** Alturas que faltan por pulsar del evento actual. En un acorde se van
        tachando a medida que caen, que es lo que permite montarlo nota a nota
        sin que el ejercicio se queje. */
    std::vector<int> pendingPitches() const;

    /** Cuántas veces seguidas se ha fallado el evento actual. La pantalla lo usa
        para ofrecer una pista sólo cuando de verdad hace falta, en vez de estar
        gritando constantemente. */
    int consecutiveMistakesHere() const noexcept { return mistakesOnCurrentEvent; }

    /** La última nota que sobró, para poder decir *qué* se tocó. */
    bool hasLastWrongNote() const noexcept { return lastWrong.eventIndex >= 0; }
    WrongNote lastWrongNote() const noexcept { return lastWrong; }

    ExerciseReport report() const;

private:
    void advanceCursor (double seconds);

    Exercise current;
    std::vector<EventOutcome> outcomes;
    std::set<int> satisfiedInCurrentEvent;
    std::set<int> heldPitches;

    std::vector<WrongNote> wrongNotes;
    WrongNote lastWrong { -1, 0, 0, 0 };

    int cursor { 0 };
    int mistakesOnCurrentEvent { 0 };
    bool running { false };
    bool finished { false };
    double startedAt { 0.0 };
    double endedAt { 0.0 };
};

} // namespace keyla::core
