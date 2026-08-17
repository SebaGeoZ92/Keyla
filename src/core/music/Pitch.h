#pragma once

#include <juce_core/juce_core.h>

namespace keyla::core
{

/** Nombres de nota, octavas e intervalos.

    Un número MIDI **no lleva ortografía**: el 61 es Do♯ y también Re♭, y cuál
    de los dos es correcto depende de la tonalidad, no del teclado. MIDI no
    transporta esa información y nunca la va a transportar (doc 02 §6).

    Mientras no haya partitura, aquí se elige la ortografía por contexto: sola,
    una nota se escribe con sostenidos; dentro de un acorde, se escribe como la
    pide la fundamental. No es teoría musical completa y no pretende serlo —
    pretende no mentir en pantalla.

    Convenio de octava: **Do central (MIDI 60) es C4**, que es lo que usan
    Yamaha, la mayoría de DAWs y prácticamente todo el material de estudio.
*/
enum class Accidental
{
    sharps,     // C# D# F# G# A#
    flats       // Db Eb Gb Ab Bb
};

/** Nombre de la clase de altura, 0 = Do. */
juce::String pitchClassName (int pitchClass, Accidental accidental = Accidental::sharps);

/** Nombre completo con octava: "C4", "F#3", "Bb5". */
juce::String noteName (int midiNote, Accidental accidental = Accidental::sharps);

/** Octava según el convenio de arriba. MIDI 60 → 4. */
constexpr int octaveOf (int midiNote) noexcept { return midiNote / 12 - 1; }

/** 0..11, siempre positivo. */
constexpr int pitchClassOf (int midiNote) noexcept { return ((midiNote % 12) + 12) % 12; }

/** Nombre del intervalo entre dos notas, con su calidad: "3ª mayor",
    "5ª justa", "tritono". Se da en la forma simple más su número de octavas
    cuando pasa de una: "3ª mayor + 1 octava". */
juce::String intervalName (int lowNote, int highNote);

/** Nombre corto para la interfaz: "3M", "5J", "7m". */
juce::String intervalShortName (int semitones);

} // namespace keyla::core
