#pragma once

#include <core/exercise/ExerciseRunner.h>
#include <core/score/ExerciseGenerator.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace keyla::app
{

/** El profesor en pantalla.

    Regla de interfaz del doc 01 §1.5: **durante la ejecución, feedback mínimo y
    no valorativo**. No hay cruces rojas ni aciertos parpadeando. Lo único que
    se muestra mientras tocas es *dónde estás* — y, si te atascas de verdad, una
    pista de qué nota toca. El juicio va al final, en el resumen.

    Que el ejercicio no avance al fallar ya es todo el feedback necesario: el
    alumno lo nota sin que nadie se lo diga.
*/
class ExerciseView final : public juce::Component
{
public:
    ExerciseView();

    /** Vuelve a leer el estado del runner. Lo llama el timer de la ventana. */
    void refresh (const core::ExerciseRunner& runner);

    void showIdle();
    void showRangeMessage (const juce::String& message);

    void paint (juce::Graphics& g) override;

private:
    juce::String title;
    juce::String hint;
    juce::String positionText;
    juce::String nextText;
    juce::String nudge;         // pista, sólo cuando se lleva un rato atascado
    juce::String rangeMessage;

    double progress { 0.0 };
    bool finished { false };
    bool idle { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExerciseView)
};

} // namespace keyla::app
