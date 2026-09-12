#pragma once

#include "LoopbackCapture.h"

#include <core/listen/Chromagram.h>
#include <core/listen/HarmonyFromAudio.h>

#include <juce_core/juce_core.h>

namespace keyla::app
{

/** Lo que Keyla está oyendo ahora mismo, listo para pintar. */
struct ListeningReading
{
    bool active { false };

    /** Está llegando audio de verdad. Es distinto de "está activo": en loopback,
        si no hay ningún programa reproduciendo, Windows no entrega nada. Sin
        este dato, "no reconoce nada" y "no llega nada" se confunden, y son dos
        problemas con soluciones opuestas. */
    bool receivingAudio { false };

    /** Hay señal audible, no sólo paquetes de silencio. */
    bool hearingMusic { false };

    /** El acorde en crudo, para que quien quiera acompañarlo no tenga que
        volver a interpretar una cadena que acabamos de formatear. */
    int rootPitchClass { -1 };
    core::ChordQuality quality { core::ChordQuality::unknown };

    juce::String chordSymbol;
    juce::String chordDescription;
    juce::String keyName;
    juce::String deviceName;

    double confidence { 0.0 };
    double margin { 0.0 };

    /** Cuánto pasado mira el análisis. Un acorde detectado no es "ahora", es
        "hace esto", y la interfaz no debe fingir lo contrario. */
    double windowSeconds { 0.0 };

    juce::String progression;
};

/** Une la captura con el reconocimiento: escucha una salida de Windows y dice
    qué acordes suenan.

    Todo el análisis corre en el hilo de captura —que no es el de audio, así que
    aquí no rigen las prohibiciones del invariante 1— y la interfaz lee una copia
    bajo cerrojo. Un cerrojo es perfectamente legítimo en este lado; lo que no se
    puede es tomarlo en el hilo de audio, y ese ni se acerca a esto.
*/
class ListeningEngine
{
public:
    ListeningEngine();
    ~ListeningEngine();

    static juce::StringArray availableOutputs() { return LoopbackCapture::availableOutputs(); }
    static juce::String defaultOutputLabel() { return LoopbackCapture::defaultOutputLabel(); }

    /** Cadena vacía si arrancó; si no, el motivo en castellano. */
    juce::String start (const juce::String& deviceName);
    void stop();

    bool isRunning() const noexcept { return capture.isCapturing(); }

    ListeningReading reading() const;

    /** Olvida la progresión y la tonalidad acumuladas. Se llama al cambiar de
        canción: la tonalidad es una propiedad de lo que se está oyendo, y
        arrastrar la de la canción anterior la envenena. */
    void forget();

private:
    void handleAudio (const float* samples, int numSamples);

    LoopbackCapture capture;

    // Sólo del hilo de captura.
    core::ChromaAnalyser analyser;
    core::HarmonyListener listener;
    double preparedRate { 0.0 };
    double recentEnergy { 0.0 };

    mutable juce::CriticalSection lock;
    ListeningReading published;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListeningEngine)
};

} // namespace keyla::app
