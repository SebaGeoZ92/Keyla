#pragma once

#include "Exercise.h"

#include <vector>

namespace keyla::core
{

/** Escalas soportadas. La lista es corta a propósito: cada una tiene que
    justificar su digitación, y una escala con digitación inventada enseña a
    tocar mal. */
enum class ScaleType
{
    major,
    naturalMinor,
    harmonicMinor,
    melodicMinor,       // ascendente
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    chromatic,
    pentatonicMajor,
    pentatonicMinor,
    blues
};

enum class ArpeggioType
{
    major, minor, dominant7, major7, minor7, diminished
};

struct ScaleRequest
{
    int rootPitch { 60 };           // Do central
    ScaleType type { ScaleType::major };
    int octaves { 1 };
    Hand hand { Hand::right };
    bool descend { true };          // subir y bajar, que es como se practican
};

struct ArpeggioRequest
{
    int rootPitch { 60 };
    ArpeggioType type { ArpeggioType::major };
    int octaves { 1 };
    Hand hand { Hand::right };
    bool descend { true };
};

/** Semitonos dentro de una octava, empezando en 0. */
std::vector<int> scaleIntervals (ScaleType type);
std::vector<int> arpeggioIntervals (ArpeggioType type);

juce::String scaleTypeName (ScaleType type);
juce::String arpeggioTypeName (ArpeggioType type);

/** Expande la petición al modelo interno. El generador es lo que hace que
    cambiar de tonalidad, de octavas o de mano sea cambiar un campo en vez de
    escribir otro fichero (doc 02 §6). */
Exercise generateScale (const ScaleRequest& request);
Exercise generateArpeggio (const ArpeggioRequest& request);

/** El puñado de ejercicios con los que arranca la aplicación. Deliberadamente
    pocos: el riesgo del doc 01 §2.4 es un motor excelente con tres ejercicios,
    y la respuesta a eso es el importador de MIDI, no inflar esta lista a mano. */
std::vector<Exercise> defaultExercises();

} // namespace keyla::core
