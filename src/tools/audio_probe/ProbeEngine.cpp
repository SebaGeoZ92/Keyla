#include "ProbeEngine.h"

#include <cmath>

namespace keyla::probe
{

namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;

    double midiNoteToHertz (int note) noexcept
    {
        return 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0);
    }
}

ProbeEngine::ProbeEngine (int maxVoicesIn, int stressVoicesIn, double stressGainIn)
    : maxVoices (maxVoicesIn),
      numStressVoices (stressVoicesIn),
      stressGain (stressGainIn)
{
    // Toda la memoria del dominio RT se reserva aquí, nunca en el callback.
    voices.resize (static_cast<std::size_t> (maxVoices));
    stressVoicePool.resize (static_cast<std::size_t> (juce::jmax (0, numStressVoices)));
}

// ── Preparación (fuera del hilo de audio, con el stream parado) ─────────────

void ProbeEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    bufferSize = device->getCurrentBufferSizeSamples();

    outputLatencySec = outputLatencyOverrideMs >= 0.0
                     ? outputLatencyOverrideMs * 0.001
                     : static_cast<double> (device->getOutputLatencyInSamples()) / sampleRate;

    // Ataque de 2 ms (sin clic), sostenido a nivel pleno mientras se aguanta la
    // tecla, y 60 ms de extinción al soltar.
    attackInc   = 1.0 / (0.002 * sampleRate);
    releaseCoef = std::exp (-1.0 / (0.060 * sampleRate));

    for (auto& v : voices)
        v = Voice {};

    streamSamplePos = 0;
    voiceCounter = 0;
    lastCallbackHostSeconds = 0.0;

    cpuStats = RunningStats {};
    deltaStats = RunningStats {};
    callbackCount = 0;
    noteOnCount = 0;
    noteOffCount = 0;
    gapDropouts = 0;
    voiceSteals = 0;
    lateEvents = 0;

    startStressVoices();
}

void ProbeEngine::audioDeviceStopped()
{
}

void ProbeEngine::startStressVoices() noexcept
{
    // Repartidas por el teclado para que no compartan fase ni frecuencia.
    for (std::size_t i = 0; i < stressVoicePool.size(); ++i)
    {
        auto& v = stressVoicePool[i];
        const int pitch = 36 + static_cast<int> (i);

        v.stage = Voice::Stage::sustain;
        v.pitch = pitch;
        v.phase = static_cast<double> (i) * 0.37;
        v.phaseInc = twoPi * midiNoteToHertz (pitch) / sampleRate;
        v.envelope = 1.0;
        v.amplitude = 0.5 / std::sqrt (static_cast<double> (stressVoicePool.size()) + 1.0);
    }
}

// ── Hilo MIDI ───────────────────────────────────────────────────────────────

void ProbeEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    const auto size = message.getRawDataSize();

    if (size < 1 || size > 3)
        return;                     // sysex y similares no interesan en el spike

    TimedMidi e {};
    e.hostSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    e.driverSeconds = message.getTimeStamp();
    e.size = static_cast<std::uint8_t> (size);

    const auto* raw = message.getRawData();

    for (int i = 0; i < size; ++i)
        e.bytes[i] = raw[i];

    midiFifo.push (e);              // si está llena se descarta y se cuenta
}

// ── Hilo de audio ───────────────────────────────────────────────────────────

void ProbeEngine::audioDeviceIOCallbackWithContext (const float* const*,
                                                    int,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;

    const double callbackStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double periodSec = static_cast<double> (numSamples) / sampleRate;

    // Detección de huecos: si entre dos callbacks ha pasado bastante más de un
    // periodo de buffer, el driver se ha saltado un bloque (doc 04 §5).
    double currentDeltaMs = 0.0;

    if (lastCallbackHostSeconds > 0.0)
    {
        const double delta = callbackStart - lastCallbackHostSeconds;
        currentDeltaMs = delta * 1000.0;
        deltaStats.add (currentDeltaMs);

        if (delta > periodSec * 1.5 + 0.0005)
            ++gapDropouts;
    }

    lastCallbackHostSeconds = callbackStart;

    // Silencio de partida. El canal 0 es el bus de trabajo; el resto se copia.
    float* work = numOutputChannels > 0 ? outputChannelData[0] : nullptr;

    if (work != nullptr)
        juce::FloatVectorOperations::clear (work, numSamples);

    // ── Drenado de la FIFO MIDI (invariante 4) ──────────────────────────────
    TimedMidi e {};

    while (midiFifo.pop (e))
    {
        // doc 02 §3: el evento se convierte al dominio de samples del stream.
        const double dSec = e.hostSeconds - callbackStart;              // ≤ 0
        const double blockStart = static_cast<double> (streamSamplePos);
        const double exactSample = blockStart + dSec * sampleRate;

        // Para evaluar se conserva la posición fraccionaria; para sintetizar se
        // cuantiza al bloque (invariante 3). Aquí sólo sintetizamos ya.
        double bufferWaitSamples = blockStart - exactSample;

        if (bufferWaitSamples < 0.0)
        {
            // Llegó mientras el callback ya estaba corriendo: no es un error,
            // pero la espera de buffer para este evento es cero.
            ++lateEvents;
            bufferWaitSamples = 0.0;
        }

        const auto status = static_cast<std::uint8_t> (e.bytes[0] & 0xF0u);
        const int data1 = e.size > 1 ? e.bytes[1] : 0;
        const int data2 = e.size > 2 ? e.bytes[2] : 0;

        const bool isNoteOn  = (status == 0x90 && data2 > 0);
        const bool isNoteOff = (status == 0x80) || (status == 0x90 && data2 == 0);

        if (isNoteOn)
        {
            noteOn (data1, data2);
            ++noteOnCount;

            NoteRecord rec {};
            rec.arrivalHostSeconds = e.hostSeconds;
            rec.driverSeconds = e.driverSeconds;
            rec.bufferWaitMs = bufferWaitSamples / sampleRate * 1000.0;
            rec.systemLatencyMs = rec.bufferWaitMs + outputLatencySec * 1000.0;
            rec.pitch = data1;
            rec.velocity = data2;

            noteRecords.push (rec);
        }
        else if (isNoteOff)
        {
            noteOff (data1);
            ++noteOffCount;
        }
    }

    // ── Síntesis ────────────────────────────────────────────────────────────
    int active = 0;

    if (work != nullptr)
    {
        for (auto& v : voices)
        {
            if (v.stage != Voice::Stage::off)
            {
                renderVoice (v, work, numSamples, 1.0);
                ++active;
            }
        }

        // Las voces de estrés se calculan siempre; su ganancia decide si se
        // oyen. Con --stress-gain 0 cuestan lo mismo y no molestan.
        for (auto& v : stressVoicePool)
            renderVoice (v, work, numSamples, stressGain);

        // Recorte de seguridad: auriculares y un principiante no se llevan bien.
        for (int i = 0; i < numSamples; ++i)
            work[i] = juce::jlimit (-1.0f, 1.0f, work[i]);

        for (int ch = 1; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[ch], work, numSamples);
    }
    else
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
    }

    streamSamplePos += static_cast<std::uint64_t> (numSamples);
    ++callbackCount;

    // ── Métricas ────────────────────────────────────────────────────────────
    const double callbackEnd = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double cpu = (callbackEnd - callbackStart) / periodSec;

    cpuStats.add (cpu);

    LoadRecord load {};
    load.cpuFraction = static_cast<float> (cpu);
    load.callbackDeltaMs = static_cast<float> (currentDeltaMs);
    loadRecords.push (load);

    ProbeSnapshot s {};
    s.streamSeconds = static_cast<double> (streamSamplePos) / sampleRate;
    s.callbackPeriodMs = periodSec * 1000.0;
    s.cpuMean = cpuStats.mean;
    s.cpuMax = cpuStats.n > 0 ? cpuStats.maximum : 0.0;
    s.callbackDeltaMean = deltaStats.n > 0 ? deltaStats.mean : 0.0;
    s.callbackDeltaSigma = deltaStats.stddev();
    s.callbackDeltaMax = deltaStats.n > 0 ? deltaStats.maximum : 0.0;
    s.callbacks = callbackCount;
    s.noteOnCount = noteOnCount;
    s.noteOffCount = noteOffCount;
    s.gapDropouts = gapDropouts;
    s.voiceSteals = voiceSteals;
    s.lateEvents = lateEvents;
    s.activeVoices = active;

    published.publish (s);
}

// ── Voces ───────────────────────────────────────────────────────────────────

void ProbeEngine::noteOn (int pitch, int velocity) noexcept
{
    Voice* chosen = nullptr;

    for (auto& v : voices)
    {
        if (v.stage == Voice::Stage::off)
        {
            chosen = &v;
            break;
        }
    }

    if (chosen == nullptr)
    {
        // Robo de la voz más antigua. Sin asignar nada.
        std::uint64_t oldest = ~std::uint64_t (0);

        for (auto& v : voices)
        {
            if (v.order < oldest)
            {
                oldest = v.order;
                chosen = &v;
            }
        }

        ++voiceSteals;
    }

    if (chosen == nullptr)
        return;

    chosen->stage = Voice::Stage::attack;
    chosen->pitch = pitch;
    chosen->order = ++voiceCounter;
    chosen->phase = 0.0;
    chosen->phaseInc = twoPi * midiNoteToHertz (pitch) / sampleRate;
    chosen->envelope = 0.0;
    // Bajo a propósito: ahora las notas se sostienen, y un acorde de seis con
    // 0,25 cada una satura el limitador. Esto va a unos auriculares.
    chosen->amplitude = 0.12 * (static_cast<double> (velocity) / 127.0);
}

void ProbeEngine::noteOff (int pitch) noexcept
{
    for (auto& v : voices)
        if (v.pitch == pitch && v.stage != Voice::Stage::off && v.stage != Voice::Stage::release)
            v.stage = Voice::Stage::release;
}

void ProbeEngine::renderVoice (Voice& v, float* dest, int numSamples, double gain) noexcept
{
    const double amp = v.amplitude * gain;

    for (int i = 0; i < numSamples; ++i)
    {
        switch (v.stage)
        {
            case Voice::Stage::attack:
                v.envelope += attackInc;
                if (v.envelope >= 1.0) { v.envelope = 1.0; v.stage = Voice::Stage::sustain; }
                break;

            case Voice::Stage::sustain:
                break;      // se queda quieto hasta el Note Off

            case Voice::Stage::release:
                v.envelope *= releaseCoef;
                if (v.envelope < 1.0e-4) { v.stage = Voice::Stage::off; v.pitch = -1; return; }
                break;

            case Voice::Stage::off:
                return;
        }

        dest[i] += static_cast<float> (std::sin (v.phase) * v.envelope * amp);

        v.phase += v.phaseInc;

        if (v.phase >= twoPi)
            v.phase -= twoPi;
    }
}

} // namespace keyla::probe
