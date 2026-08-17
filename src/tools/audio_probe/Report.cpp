#include "Report.h"
#include "Utf8.h"

#include <algorithm>
#include <iostream>

namespace keyla::probe
{

namespace
{
    juce::String ms (double v, int decimals = 2)
    {
        return juce::String (v, decimals) + " ms";
    }

    juce::String pct (double fraction, int decimals = 1)
    {
        return juce::String (fraction * 100.0, decimals) + " %";
    }

    juce::String verdictLabel (Verdict v)
    {
        switch (v)
        {
            case Verdict::objective:    return "OBJETIVO";
            case Verdict::acceptable:   return "ACEPTABLE";
            case Verdict::failed:       return "FALLO";
            case Verdict::notEvaluated: return "SIN DATOS";
        }

        return "?";
    }

    Verdict gradeLowerIsBetter (double value, double target, double minimum)
    {
        if (value <= target)  return Verdict::objective;
        if (value <= minimum) return Verdict::acceptable;
        return Verdict::failed;
    }

    juce::String pad (const juce::String& s, int width)
    {
        return s.paddedRight (' ', width);
    }

    void line (std::ostream& os, const juce::String& s)
    {
        os << s.toStdString() << '\n';
    }
}

std::vector<CriterionResult> evaluateCriteria (const ProbeResults& r)
{
    std::vector<CriterionResult> out;

    // ── Latencia p50 ────────────────────────────────────────────────────────
    {
        CriterionResult c;
        c.name = "Latencia del sistema (p50)";
        c.target = "<= 8 ms";
        c.minimum = "<= 12 ms";

        if (r.hasLatency)
        {
            c.measured = ms (r.latencyP50);
            c.verdict = gradeLowerIsBetter (r.latencyP50, 8.0, 12.0);
        }
        else
        {
            c.measured = "—"_u8;
            c.note = "no se recibió ninguna nota"_u8;
        }

        if (! r.outputLatencyMeasured)
            c.note = c.note.isEmpty()
                   ? "latencia de salida según el driver, sin calibrar (--calibrate)"_u8
                   : c.note;

        out.push_back (c);
    }

    // ── Latencia máxima ─────────────────────────────────────────────────────
    {
        CriterionResult c;
        c.name = "Latencia del sistema (max)";
        c.target = "<= 12 ms";
        c.minimum = "<= 20 ms";

        if (r.hasLatency)
        {
            c.measured = ms (r.latencyMax);
            c.verdict = gradeLowerIsBetter (r.latencyMax, 12.0, 20.0);
        }
        else
        {
            c.measured = "—"_u8;
            c.note = "no se recibió ninguna nota"_u8;
        }

        out.push_back (c);
    }

    // ── Dropouts ────────────────────────────────────────────────────────────
    {
        CriterionResult c;
        c.name = "Dropouts";
        c.target = "0";
        c.minimum = "0";

        const auto xruns = r.driverXRuns > 0 ? static_cast<std::uint64_t> (r.driverXRuns) : 0ull;
        const auto total = r.gapDropouts + xruns;

        c.measured = juce::String (total);

        // Cero dropouts porque el stream nunca arrancó no es cero dropouts.
        c.verdict = r.callbacks == 0 ? Verdict::notEvaluated
                  : total == 0       ? Verdict::objective
                                     : Verdict::failed;

        if (r.callbacks == 0)
        {
            c.measured = "—"_u8;
            c.note = "el dispositivo no produjo ningún callback de audio"_u8;
        }
        else
        {
            c.note = r.driverXRuns >= 0
                   ? "huecos " + juce::String (r.gapDropouts) + ", xruns del driver " + juce::String (r.driverXRuns)
                   : juce::String ("huecos detectados; este driver no reporta xruns");

            if (r.runSeconds < 600.0)
                c.note += ". No concluyente: el criterio exige 10 min (--duration 600)";
        }

        out.push_back (c);
    }

    // ── CPU ─────────────────────────────────────────────────────────────────
    {
        CriterionResult c;
        c.name = "Carga de CPU del hilo de audio";
        c.target = "< 30 %";
        c.minimum = "< 60 %";

        if (r.hasCpu)
        {
            c.measured = pct (r.cpuP95) + " (p95)";
            c.note = "media " + pct (r.cpuMean) + ", max " + pct (r.cpuMax)
                   + ", con " + juce::String (r.stressVoices) + " voces";

            // El criterio está definido a 32 voces. Medir con menos y dar el
            // criterio por cumplido es la misma mentira que contar cero
            // dropouts de un stream que nunca arrancó.
            if (r.stressVoices < 32)
            {
                c.verdict = Verdict::notEvaluated;
                c.note += ". Medido sin polifonía plena: repite con --stress 32"_u8;
            }
            else
            {
                c.verdict = gradeLowerIsBetter (r.cpuP95 * 100.0, 30.0, 60.0);
            }
        }
        else
        {
            c.measured = "—"_u8;
        }

        if (r.debugBuild)
            c.note = "BUILD DE DEBUG: la cifra no vale para nada. Recompila en Release.";

        out.push_back (c);
    }

    // ── Jitter MIDI ─────────────────────────────────────────────────────────
    {
        CriterionResult c;
        c.name = "Jitter de entrada MIDI (sigma)";
        c.target = "< 1,5 ms";
        c.minimum = "< 3 ms";

        if (r.hasMidiJitter)
        {
            c.measured = ms (r.midiJitterSigmaMs);
            c.verdict = gradeLowerIsBetter (r.midiJitterSigmaMs, 1.5, 3.0);
            c.note = r.midiJitterSource;
        }
        else
        {
            c.measured = "—"_u8;
            c.note = "hace falta una fuente de tempo conocido (--selftest con loopMIDI)";
        }

        out.push_back (c);
    }

    return out;
}

int exitCodeFor (const std::vector<CriterionResult>& criteria)
{
    for (const auto& c : criteria)
        if (c.verdict == Verdict::failed || c.verdict == Verdict::notEvaluated)
            return 1;

    return 0;
}

void printTextReport (std::ostream& os, const ProbeResults& r, const std::vector<CriterionResult>& criteria)
{
    line (os, "");
    line (os, "================================================================================");
    line (os, "  audio_probe — informe ("_u8 + r.mode + ")");
    line (os, "================================================================================");
    line (os, "");
    line (os, "  Dispositivo    " + r.audioDeviceName);
    line (os, "  Backend        " + r.audioDeviceType + (r.exclusive ? "  ·  modo exclusivo"_u8 : "  ·  modo compartido"_u8));
    line (os, "  Formato        " + juce::String (r.sampleRate, 0) + " Hz  ·  buffer "_u8
          + juce::String (r.bufferSize) + " samples ("
          + juce::String (1000.0 * r.bufferSize / r.sampleRate, 2) + " ms)");
    line (os, "  MIDI in        " + (r.midiInputName.isEmpty() ? juce::String ("(ninguno)") : r.midiInputName));

    if (r.midiOutputName.isNotEmpty())
        line (os, "  MIDI out       " + r.midiOutputName);

    line (os, "  Duración       "_u8 + juce::String (r.runSeconds, 1) + " s  ·  "_u8
          + juce::String (r.callbacks) + " callbacks  ·  "_u8
          + juce::String (r.noteOnCount) + " Note On"
          + (r.interrupted ? "   (CORTADO: pediste " + juce::String (r.requestedSeconds, 0) + " s)"
                           : juce::String()));
    line (os, "");

    line (os, "  Latencia de salida");
    line (os, "    declarada por el driver   " + ms (r.driverOutputLatencyMs));
    line (os, "    usada en el cálculo       "_u8 + ms (r.usedOutputLatencyMs)
          + (r.outputLatencyMeasured ? "   (medida por loopback)"_u8 : "   (la del driver — los drivers mienten)"_u8));
    line (os, "");

    if (r.hasLatency)
    {
        line (os, "  Latencia del sistema (espera de buffer + salida)");
        line (os, "    p50 " + ms (r.latencyP50) + "   p95 " + ms (r.latencyP95) + "   max " + ms (r.latencyMax));
        line (os, "    espera de buffer: p50 " + ms (r.bufferWaitP50) + "   max " + ms (r.bufferWaitMax));
        line (os, "");
    }

    line (os, "  Estabilidad del hilo de audio");
    line (os, "    periodo nominal           " + ms (r.callbackPeriodMs));
    line (os, "    jitter del callback       sigma " + ms (r.callbackSigmaMs, 3)
          + "  (" + juce::String (r.callbackPeriodMs > 0.0 ? 100.0 * r.callbackSigmaMs / r.callbackPeriodMs : 0.0, 1)
          + " % del periodo)   max " + ms (r.callbackMaxMs, 2));
    line (os, "    eventos MIDI descartados  " + juce::String (r.midiQueueDrops)
          + "   registros perdidos " + juce::String (r.recordQueueDrops));
    line (os, "    eventos fuera de bloque   " + juce::String (r.lateEvents));
    line (os, "");

    if (r.hasMidiJitter)
    {
        line (os, "  Entrada MIDI  (" + r.midiJitterSource + ", " + juce::String (r.midiJitterSamples) + " muestras)");
        line (os, "    retardo de transporte     media " + ms (r.midiTransportMeanMs, 3)
              + "   max " + ms (r.midiTransportMaxMs, 3));
        line (os, "    jitter                    sigma " + ms (r.midiJitterSigmaMs, 3));
        line (os, "");
    }

    if (r.bluetoothSuspected)
    {
        line (os, "  *** AVISO: la salida parece ser Bluetooth. 100-300 ms de latencia.");
        line (os, "      Con auriculares Bluetooth este producto no sirve (doc 01 §2.1)."_u8);
        line (os, "");
    }
    else if (r.wirelessSuspected)
    {
        line (os, "  *** AVISO: la salida parece inalámbrica (receptor de 2,4 GHz)."_u8);
        line (os, "      Añade latencia que el driver no siempre declara. La cifra de arriba"_u8);
        line (os, "      sólo es de fiar si la has calibrado con --calibrate."_u8);
        line (os, "");
    }

    if (r.debugBuild)
    {
        line (os, "  *** AVISO: build de Debug. Las cifras de CPU no significan nada.");
        line (os, "");
    }

    line (os, "  Criterios de aceptación — doc 04 §8"_u8);
    line (os, "  " + juce::String::repeatedString ("-", 76));
    line (os, "  " + pad ("Criterio", 32) + pad ("Medido", 18) + pad ("Objetivo", 12) + "Veredicto");
    line (os, "  " + juce::String::repeatedString ("-", 76));

    for (const auto& c : criteria)
    {
        line (os, "  " + pad (c.name, 32) + pad (c.measured, 18) + pad (c.target, 12) + verdictLabel (c.verdict));

        if (c.note.isNotEmpty())
            line (os, "      " + c.note);
    }

    line (os, "  " + juce::String::repeatedString ("-", 76));

    // Un criterio sin medir y un criterio que falla no son lo mismo, y hay que
    // decirlo distinto: "no se cumplen los criterios" ante una tabla llena de
    // OBJETIVO sólo porque falta una medición es un informe que miente.
    juce::StringArray failed, missing;

    for (const auto& c : criteria)
    {
        if (c.verdict == Verdict::failed)         failed.add (c.name);
        if (c.verdict == Verdict::notEvaluated)   missing.add (c.name);
    }

    const bool allObjective = failed.isEmpty() && missing.isEmpty()
                           && std::all_of (criteria.begin(), criteria.end(),
                                           [] (const auto& c) { return c.verdict == Verdict::objective; });

    line (os, "");

    if (! failed.isEmpty())
    {
        line (os, "  VEREDICTO: FALLA " + failed.joinIntoString (", ") + ".");
        line (os, "  No empezar la fase 1 hasta resolverlo (doc 05).");
    }
    else if (! missing.isEmpty())
    {
        line (os, "  VEREDICTO: lo medido cumple. Faltan mediciones, la fase 0 no se puede cerrar.");
        line (os, "  Sin medir: " + missing.joinIntoString (", ") + ".");
    }
    else if (allObjective)
    {
        line (os, "  VEREDICTO: se cumple la columna \"objetivo\" en todo. La fase 0 pasa.");
    }
    else
    {
        line (os, "  VEREDICTO: se cumple el mínimo aceptable, no el objetivo. Investigar antes de seguir."_u8);
    }

    line (os, "");
}

void printJsonReport (std::ostream& os, const ProbeResults& r, const std::vector<CriterionResult>& criteria)
{
    auto* root = new juce::DynamicObject();

    auto* cfg = new juce::DynamicObject();
    cfg->setProperty ("mode", r.mode);
    cfg->setProperty ("audioDeviceType", r.audioDeviceType);
    cfg->setProperty ("audioDeviceName", r.audioDeviceName);
    cfg->setProperty ("midiInput", r.midiInputName);
    cfg->setProperty ("midiOutput", r.midiOutputName);
    cfg->setProperty ("sampleRate", r.sampleRate);
    cfg->setProperty ("bufferSize", r.bufferSize);
    cfg->setProperty ("exclusive", r.exclusive);
    cfg->setProperty ("debugBuild", r.debugBuild);
    cfg->setProperty ("bluetoothSuspected", r.bluetoothSuspected);
    root->setProperty ("config", juce::var (cfg));

    auto* run = new juce::DynamicObject();
    run->setProperty ("seconds", r.runSeconds);
    run->setProperty ("callbacks", static_cast<juce::int64> (r.callbacks));
    run->setProperty ("noteOns", static_cast<juce::int64> (r.noteOnCount));
    root->setProperty ("run", juce::var (run));

    auto* lat = new juce::DynamicObject();
    lat->setProperty ("driverOutputLatencyMs", r.driverOutputLatencyMs);
    lat->setProperty ("usedOutputLatencyMs", r.usedOutputLatencyMs);
    lat->setProperty ("outputLatencyMeasured", r.outputLatencyMeasured);
    lat->setProperty ("available", r.hasLatency);
    lat->setProperty ("p50Ms", r.latencyP50);
    lat->setProperty ("p95Ms", r.latencyP95);
    lat->setProperty ("maxMs", r.latencyMax);
    lat->setProperty ("bufferWaitP50Ms", r.bufferWaitP50);
    root->setProperty ("systemLatency", juce::var (lat));

    auto* stab = new juce::DynamicObject();
    stab->setProperty ("gapDropouts", static_cast<juce::int64> (r.gapDropouts));
    stab->setProperty ("driverXRuns", r.driverXRuns);
    stab->setProperty ("midiQueueDrops", static_cast<juce::int64> (r.midiQueueDrops));
    stab->setProperty ("callbackPeriodMs", r.callbackPeriodMs);
    stab->setProperty ("callbackSigmaMs", r.callbackSigmaMs);
    stab->setProperty ("callbackMaxMs", r.callbackMaxMs);
    root->setProperty ("stability", juce::var (stab));

    auto* cpu = new juce::DynamicObject();
    cpu->setProperty ("available", r.hasCpu);
    cpu->setProperty ("mean", r.cpuMean);
    cpu->setProperty ("p95", r.cpuP95);
    cpu->setProperty ("max", r.cpuMax);
    cpu->setProperty ("stressVoices", r.stressVoices);
    root->setProperty ("cpu", juce::var (cpu));

    auto* midi = new juce::DynamicObject();
    midi->setProperty ("available", r.hasMidiJitter);
    midi->setProperty ("sigmaMs", r.midiJitterSigmaMs);
    midi->setProperty ("transportMeanMs", r.midiTransportMeanMs);
    midi->setProperty ("transportMaxMs", r.midiTransportMaxMs);
    midi->setProperty ("samples", r.midiJitterSamples);
    midi->setProperty ("source", r.midiJitterSource);
    root->setProperty ("midiJitter", juce::var (midi));

    juce::Array<juce::var> arr;

    for (const auto& c : criteria)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", c.name);
        o->setProperty ("measured", c.measured);
        o->setProperty ("target", c.target);
        o->setProperty ("minimum", c.minimum);
        o->setProperty ("verdict", verdictLabel (c.verdict));
        o->setProperty ("note", c.note);
        arr.add (juce::var (o));
    }

    root->setProperty ("criteria", arr);
    root->setProperty ("pass", exitCodeFor (criteria) == 0);

    os << juce::JSON::toString (juce::var (root), false).toStdString() << '\n';
}

} // namespace keyla::probe
