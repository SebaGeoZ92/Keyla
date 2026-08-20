#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace keyla::core
{

enum class Hand { unknown, left, right, both };

/** Un evento esperado: **una nota o un acorde**, nunca "una nota" a secas.

    Agrupar las notas simultáneas en un solo evento no es una comodidad, es
    correctitud: notas que deberían sonar a la vez nunca suenan a la vez, y si
    cada una fuese un evento independiente, un acorde arpegiado se reportaría
    como tres errores de timing en vez de como un acorde (doc 01 §2.2).

    La posición va en **pulsos**, no en samples ni en segundos. Así el mismo
    ejercicio se practica a 60 o a 120 BPM sin duplicar datos, y el transporte
    convierte cuando hace falta (doc 02 §5).
*/
struct ExpectedEvent
{
    std::vector<std::uint8_t> pitches;
    double onsetBeat { 0.0 };
    double durationBeats { 1.0 };

    /** Una mano **por evento**, no por altura. Cuando las dos manos tocan a la
        vez, sus notas caen en el mismo evento y esto vale `both`: se pierde a
        qué mano pertenece cada nota concreta.

        Es una pérdida consciente y hoy no molesta —para practicar una sola mano
        se filtra antes de agrupar—, pero habrá que revisarla el día que se
        pinte la digitación de cada mano por separado. */
    Hand hand { Hand::unknown };

    /** Digitación sugerida, una por altura. Vacía si no se conoce: inventarse
        una digitación es peor que no dar ninguna, porque el alumno la seguirá. */
    std::vector<std::uint8_t> fingers;

    bool isChord() const noexcept { return pitches.size() > 1; }
    bool contains (int pitch) const noexcept;
};

/** Un ejercicio ya expandido: la representación interna a la que llegan tanto
    el generador declarativo como, algún día, el importador de MIDI. Un solo
    modelo, dos fuentes (doc 02 §6). */
struct Exercise
{
    juce::String id;
    juce::String name;
    juce::String hint;              // qué mirar al practicarlo, en una línea
    std::vector<ExpectedEvent> events;

    int lowestPitch() const noexcept;
    int highestPitch() const noexcept;
    int totalNotes() const noexcept;
    bool isEmpty() const noexcept { return events.empty(); }

    /** Desplaza el ejercicio entero por octavas. */
    void transposeOctaves (int octaves);
};

/** Resultado de intentar encajar un ejercicio en un teclado concreto.

    El SE49 tiene 49 teclas y muchos ejercicios no caben (doc 01 §1.1). Marcar
    como "omitidas" unas notas que el alumno **no puede físicamente tocar** es
    el tipo de error que hace que una aplicación se desinstale, así que esto se
    resuelve antes de empezar y no durante.
*/
struct RangeFit
{
    enum class Outcome { fits, transposed, doesNotFit };

    Outcome outcome { Outcome::fits };
    int octavesMoved { 0 };
    juce::String explanation;       // en castellano, para enseñar tal cual
};

/** Encaja el ejercicio en [lowest, highest] moviéndolo por octavas. Si ni así
    cabe, lo deja como estaba y lo dice. */
RangeFit fitToKeyboardRange (Exercise& exercise, int lowest, int highest);

} // namespace keyla::core
