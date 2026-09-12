#pragma once

// ¿Cuánto acertó Keyla escuchando? Se contesta comparando lo que oyó con lo
// que tocaste tú siguiendo la canción.

#include "../music/ChordRecognizer.h"

#include <vector>

namespace keyla::core
{

/** Un acorde que empieza en un instante y dura hasta que empieza el siguiente. */
struct TimedChord
{
    double seconds { 0.0 };
    int rootPitchClass { -1 };
    ChordQuality quality { ChordQuality::unknown };
};

/** Una nota del teclado, con el instante en segundos desde el inicio. */
struct TimedNote
{
    double seconds { 0.0 };
    int pitch { 0 };
    bool isOn { false };
};

struct ChordConfusion
{
    int playedRoot { -1 };
    ChordQuality playedQuality { ChordQuality::unknown };
    int heardRoot { -1 };
    ChordQuality heardQuality { ChordQuality::unknown };

    /** Segundos durante los que se confundieron. En tiempo y no en veces: un
        acorde confundido durante un compás pesa más que uno durante un
        destello, y contar "veces" los igualaría. */
    double seconds { 0.0 };
};

struct ListeningEvaluation
{
    bool valid { false };

    /** Cuánto van tus manos por detrás de lo que Keyla oye, en segundos.
        Positivo = tocas después.

        Sale de dos retrasos sumados que no se pueden separar desde aquí: el
        tuyo al seguir la canción y el del propio análisis, que mira una
        ventana de pasado. Por eso se busca en vez de suponerlo — si se
        comparara sin corregirlo, cada cambio de acorde contaría como un error
        durante medio segundo aunque los dos lo hubierais acertado. */
    double lagSeconds { 0.0 };

    /** Fracción del tiempo comparado en que coincidió la fundamental. */
    double rootAgreement { 0.0 };

    /** Fracción del tiempo en que coincidieron fundamental **y** calidad. */
    double fullAgreement { 0.0 };

    /** Tiempo en que había acorde a los dos lados. Por debajo de unos
        segundos, los porcentajes no significan nada. */
    double comparedSeconds { 0.0 };

    /** De más a menos tiempo. */
    std::vector<ChordConfusion> confusions;
};

/** El acorde vigente en un instante: el último que empezó antes. */
const TimedChord* chordAt (const std::vector<TimedChord>& timeline, double seconds);

/** Los acordes que tocaste, a partir de las notas del teclado.

    Se miran **teclas**, no sonido: con el pedal pisado una nota sigue sonando
    después de soltarla, pero lo que querías tocar es lo que tenías bajo los
    dedos.

    Un acorde sólo cuenta si se mantiene al menos `minimumHoldSeconds`. Al
    cambiar de acorde con legato las manos pasan por conjuntos intermedios —
    tres notas del viejo y una del nuevo— que pueden parecer un acorde durante
    unos milisegundos, y sin este filtro cada cambio fabricaría un acorde que
    nunca tocaste. */
std::vector<TimedChord> chordsFromNotes (const std::vector<TimedNote>& notes,
                                         double minimumHoldSeconds = 0.08);

/** Compara lo oído con lo tocado.

    Prueba desfases entre `-maxLagSeconds` y `+maxLagSeconds` y se queda con el
    que más coincide, y a ese desfase calcula el acuerdo y las confusiones.
    Función pura: dos listas y un número entran, un informe sale. */
ListeningEvaluation evaluateListening (const std::vector<TimedChord>& heard,
                                       const std::vector<TimedChord>& played,
                                       double durationSeconds,
                                       double maxLagSeconds = 2.0,
                                       double stepSeconds = 0.05);

} // namespace keyla::core
