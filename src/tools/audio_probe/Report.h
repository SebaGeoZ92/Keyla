#pragma once

// Informe final y veredicto contra los criterios del doc 04 §8.

#include <juce_core/juce_core.h>

#include <ostream>
#include <vector>

namespace keyla::probe
{

struct ProbeResults
{
    // ── Configuración ───────────────────────────────────────────────────────
    juce::String mode;
    juce::String audioDeviceType;
    juce::String audioDeviceName;
    juce::String midiInputName;
    juce::String midiOutputName;
    double sampleRate { 48000.0 };
    int bufferSize { 128 };
    bool exclusive { true };
    bool bluetoothSuspected { false };
    bool wirelessSuspected { false };
    bool debugBuild { false };

    double driverOutputLatencyMs { 0.0 };
    double usedOutputLatencyMs { 0.0 };
    bool outputLatencyMeasured { false };   // true si viene de --output-latency-ms

    // ── Ejecución ───────────────────────────────────────────────────────────
    double runSeconds { 0.0 };
    double requestedSeconds { 0.0 };
    bool interrupted { false };     // cortado con Ctrl-C antes de tiempo
    std::uint64_t noteOnCount { 0 };
    std::uint64_t callbacks { 0 };

    // ── Latencia del sistema (doc 04 §4) ────────────────────────────────────
    bool hasLatency { false };
    double latencyP50 { 0.0 };
    double latencyP95 { 0.0 };
    double latencyMax { 0.0 };
    double bufferWaitP50 { 0.0 };
    double bufferWaitMax { 0.0 };

    // ── Estabilidad ─────────────────────────────────────────────────────────
    std::uint64_t gapDropouts { 0 };
    int driverXRuns { -1 };
    std::uint64_t midiQueueDrops { 0 };
    std::uint64_t recordQueueDrops { 0 };
    std::uint64_t lateEvents { 0 };

    // ── Carga ───────────────────────────────────────────────────────────────
    bool hasCpu { false };
    double cpuMean { 0.0 };
    double cpuP95 { 0.0 };
    double cpuMax { 0.0 };
    int stressVoices { 0 };

    // ── Jitter ──────────────────────────────────────────────────────────────
    double callbackPeriodMs { 0.0 };
    double callbackSigmaMs { 0.0 };
    double callbackMaxMs { 0.0 };

    bool hasMidiJitter { false };
    double midiJitterSigmaMs { 0.0 };
    double midiTransportMeanMs { 0.0 };
    double midiTransportMaxMs { 0.0 };
    int midiJitterSamples { 0 };
    juce::String midiJitterSource;
};

enum class Verdict
{
    objective,      // cumple la columna "objetivo"
    acceptable,     // cumple sólo el "mínimo aceptable"
    failed,
    notEvaluated
};

struct CriterionResult
{
    juce::String name;
    juce::String measured;
    juce::String target;
    juce::String minimum;
    Verdict verdict { Verdict::notEvaluated };
    juce::String note;
};

std::vector<CriterionResult> evaluateCriteria (const ProbeResults& r);

void printTextReport (std::ostream& os, const ProbeResults& r, const std::vector<CriterionResult>& criteria);
void printJsonReport (std::ostream& os, const ProbeResults& r, const std::vector<CriterionResult>& criteria);

// 0 si todo llega al menos al mínimo aceptable, 1 si algo falla.
int exitCodeFor (const std::vector<CriterionResult>& criteria);

} // namespace keyla::probe
