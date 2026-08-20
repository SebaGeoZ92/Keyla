#pragma once

#include "Exercise.h"
#include "../time/TimeSignature.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace keyla::core
{

/** Cómo se decidió qué mano toca qué. Se reporta a propósito: es una
    **heurística**, no un dato del fichero, y el usuario tiene derecho a saber
    de dónde salió para poder corregirla (doc 02 §6). */
enum class HandSeparation
{
    notAttempted,
    byTrack,        // dos pistas con notas: la más aguda es la derecha
    byChannel,      // dos canales distintos
    bySplitPoint,   // todo en una pista: se parte por altura
    singleHand      // todo cabe en una mano
};

struct MidiImportOptions
{
    /** Tolerancia para agrupar notas simultáneas en un acorde, en pulsos. A
        60 BPM, 0,06 pulsos son 60 ms. Se expresa en pulsos y no en segundos
        porque un fichero MIDI vive en pulsos: así la agrupación no cambia si el
        ejercicio se practica luego a otro tempo. */
    double chordToleranceBeats { 0.06 };

    /** Qué manos importar. `both` trae las dos, que es lo normal al empezar. */
    Hand hands { Hand::both };

    /** Un fichero enorme se importa entero y luego no se puede practicar. Se
        corta y se avisa, en vez de colgar la aplicación. */
    int maxEvents { 4000 };

    /** Nota por debajo de la cual se considera mano izquierda cuando no hay
        pistas ni canales que lo digan. Do central. */
    int splitPoint { 60 };
};

struct MidiImportResult
{
    bool ok { false };
    juce::String message;

    Exercise exercise;

    double tempoBpm { 120.0 };
    TimeSignature meter { 4, 4 };
    HandSeparation handSeparation { HandSeparation::notAttempted };

    /** True si hubo que recortar por `maxEvents`. */
    bool truncated { false };
};

/** Importa un Standard MIDI File al modelo interno.

    Ésta es la respuesta al riesgo del doc 01 §2.4 — *el fracaso más probable de
    este proyecto no es técnico: es acabar con un instrumento virtual excelente
    y tres ejercicios*. El contenido no puede depender de escribir ejercicios a
    mano, y SMF es lo que exporta cualquier herramienta, incluido nuestro propio
    grabador.

    **Lo que MIDI no trae y aquí no se inventa** (doc 02 §6): digitación,
    articulación, enarmonía, y la separación de manos, que se decide con una
    heurística y se reporta como tal para que se pueda corregir.
*/
MidiImportResult importMidiFile (juce::InputStream& stream, const MidiImportOptions& options = {});
MidiImportResult importMidiFile (const juce::File& file, const MidiImportOptions& options = {});

juce::String handSeparationDescription (HandSeparation separation);

} // namespace keyla::core
