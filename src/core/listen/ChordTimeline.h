#pragma once

// La escucha partida en sus dos mitades, para poder repetir sólo la barata.

#include "Chromagram.h"
#include "HarmonyFromAudio.h"
#include "ListeningEvaluation.h"

#include <vector>

namespace keyla::core
{

/** Un fotograma ya escuchado: lo que costó calcular, guardado. */
struct ChromaFrame
{
    double seconds { 0.0 };
    Chroma chroma;
    std::vector<double> profile;
};

/** La parte cara: pasar el audio por el análisis de Q constante.

    Cuatro minutos y medio de canción son unos cinco mil millones de
    multiplicaciones, y **no dependen de cómo se decide el acorde**. Por eso se
    calcula una vez y se guarda: probar trescientas formas de decidir sobre la
    misma canción pasa de llevar un cuarto de hora a llevar un par de segundos. */
std::vector<ChromaFrame> computeChromaFrames (const std::vector<float>& mono,
                                              double sampleRate,
                                              ChromaAnalyser::Options options = {},
                                              int* lowestPitchOut = nullptr);

/** La parte barata: decidir el acorde fotograma a fotograma. Devuelve sólo
    los cambios, con su instante. */
std::vector<TimedChord> chordsFromFrames (const std::vector<ChromaFrame>& frames,
                                          int lowestPitch,
                                          const HarmonyListener::Options& options,
                                          KeyEstimate* keyOut = nullptr);

} // namespace keyla::core
