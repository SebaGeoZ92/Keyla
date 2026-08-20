#pragma once

#include "../recording/SessionRecorder.h"
#include "../score/Exercise.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace keyla::core
{

enum class NoteLabel
{
    correct,
    early,
    late,
    wrongPitch,
    omitted,
    extra
};

/** Un emparejamiento entre lo que se esperaba y lo que se tocó. */
struct AlignedPair
{
    int expectedIndex { -1 };       // -1 = nota tocada de más
    int playedIndex { -1 };         // -1 = evento omitido

    NoteLabel label { NoteLabel::correct };

    /** Error temporal **ya corregido** por el offset perceptual, en ms y con
        signo. Negativo = te adelantaste. */
    double timingErrorMs { 0.0 };

    /** Sólo para wrongPitch: cuántos semitonos y hacia dónde. Es lo que
        convierte "incorrecta" en un diagnóstico (doc 02 §5). */
    int semitonesOff { 0 };

    int expectedPitch { 0 };
    int playedPitch { 0 };
};

struct AlignmentOptions
{
    /** Tolerancia para considerar una nota "a tiempo". Por debajo de esto no se
        etiqueta como adelantada ni atrasada. */
    double toleranceMs { 45.0 };

    /** **El offset perceptual del doc 01 §1.4 y del invariante 7.**

        Oyes el clic retrasado por la latencia de salida, y tu pulsación llega
        retrasada por la latencia de entrada MIDI. Si no se resta esta constante,
        *todo* alumno aparece sistemáticamente tarde por unos 8-10 ms que no
        tienen nada que ver con su ritmo, y la función estrella del producto
        miente. Sale de la calibración del doc 04 §6. */
    double perceptualOffsetMs { 0.0 };

    /** Coste, en "milisegundos equivalentes", de emparejar dos alturas
        distintas. Alto a propósito: es preferible declarar una nota omitida y
        otra añadida que casar dos alturas que no tienen nada que ver. */
    double wrongPitchCostMs { 400.0 };

    /** Coste de omitir un evento esperado o de añadir uno tocado. */
    double skipCostMs { 500.0 };
};

struct Alignment
{
    std::vector<AlignedPair> pairs;

    int matched() const noexcept;
    int omitted() const noexcept;
    int extra() const noexcept;
    int wrongPitch() const noexcept;
};

/** Alineación óptima entre una grabación y lo que se esperaba.

    **El riesgo real de un evaluador no es clasificar, es alinear** (doc 01
    §2.2). Las etiquetas —correcta, adelantada, omitida— se asignan *después* de
    decidir a qué nota esperada corresponde cada nota tocada. El enfoque ingenuo
    de índice contra índice se rompe en cuanto el alumno omite o añade una nota:
    a partir de ahí *todo* sale mal y el informe es basura. Y eso pasa en el
    primer minuto de uso real.

    Aquí se resuelve como lo que es: una distancia de edición sobre secuencias,
    con coste de altura más coste temporal. Es una **función pura sobre datos**,
    así que se testea con ficheros y sin hardware — que es donde el doc 01 §1.8
    dice que va el grueso de los tests.
*/
Alignment alignPerformance (const std::vector<PlayedEvent>& played,
                            const std::vector<ExpectedEvent>& expected,
                            double sampleRate,
                            double beatsToSeconds,
                            const AlignmentOptions& options = {});

juce::String labelName (NoteLabel label);

} // namespace keyla::core
