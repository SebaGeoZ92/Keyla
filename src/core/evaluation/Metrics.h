#pragma once

#include "OfflineAligner.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace keyla::core
{

/** Las métricas del informe (doc 02 §5).

    Un porcentaje de acierto no enseña nada. Éstas sí, y cada una responde a una
    pregunta distinta que exige una corrección distinta:

    - **Sesgo**: ¿te adelantas siempre, o vas errático? Adelantarse de forma
      constante se corrige escuchando; ir errático se corrige despacio.
    - **Consistencia**: es *la* métrica de músico. Mejora antes que la velocidad,
      y es la que de verdad indica que estás aprendiendo.
    - **Deriva**: ¿aceleras o frenas a lo largo del pasaje? El error medio lo
      esconde, porque adelantarse al principio y atrasarse al final da cero.
    - **Regularidad**: en una escala, lo brutalmente honesto. Es exactamente lo
      que un profesor corrige.
    - **Uniformidad de velocity**: delata los dedos débiles, el 4º y el 5º.
*/
struct PerformanceMetrics
{
    int totalExpected { 0 };
    int played { 0 };
    int correctPitches { 0 };
    int wrongPitches { 0 };
    int omitted { 0 };
    int extra { 0 };

    /** Error medio **con signo**, en ms. Negativo = te adelantas. */
    double timingBiasMs { 0.0 };

    /** Desviación típica del error temporal, en ms. */
    double consistencyMs { 0.0 };

    /** Pendiente de la regresión del error sobre el tiempo, en ms por segundo.
        Positiva = te vas quedando atrás (frenas). */
    double tempoDriftMsPerSecond { 0.0 };

    /** σ de los intervalos entre ataques consecutivos, en ms. Sólo tiene
        sentido en pasajes de notas iguales, como una escala. */
    double regularityMs { 0.0 };

    /** σ de la velocity dentro del pasaje. */
    double velocitySpread { 0.0 };
    double meanVelocity { 0.0 };

    /** Cuántas notas entraron en cada cálculo temporal. Con menos de un puñado,
        la desviación típica no significa nada y hay que decirlo. */
    int timingSampleCount { 0 };

    bool hasTimingData() const noexcept { return timingSampleCount >= 4; }
};

/** Calcula las métricas a partir de una alineación ya hecha. Función pura. */
PerformanceMetrics computeMetrics (const Alignment& alignment,
                                   const std::vector<PlayedEvent>& played,
                                   double sampleRate);

/** El informe en palabras.

    Deliberadamente **no** empieza por un número. Empieza por lo que hay que
    corregir, porque "tu error medio es 23 ms" es verdadero e inútil
    (doc 01 §1.6). Y no felicita por respirar: si no hay nada destacable, lo
    dice sin adornos.
*/
juce::StringArray describeMetrics (const PerformanceMetrics& metrics);

} // namespace keyla::core
