#include "ListeningEngine.h"

#include <core/text/Utf8.h>

namespace keyla::app
{

using keyla::operator""_u8;

ListeningEngine::ListeningEngine()
{
    capture.onAudio = [this] (const float* samples, int numSamples)
    {
        handleAudio (samples, numSamples);
    };
}

ListeningEngine::~ListeningEngine()
{
    stop();
}

juce::String ListeningEngine::start (const juce::String& deviceName)
{
    forget();

    const auto error = capture.start (deviceName);

    const juce::ScopedLock scoped (lock);

    published = ListeningReading {};
    published.active = error.isEmpty();
    published.deviceName = capture.deviceName();

    return error;
}

void ListeningEngine::stop()
{
    capture.stop();

    const juce::ScopedLock scoped (lock);
    published = ListeningReading {};
}

void ListeningEngine::forget()
{
    listener.reset();
    preparedRate = 0.0;
    recentEnergy = 0.0;

    const juce::ScopedLock scoped (lock);
    published.chordSymbol.clear();
    published.chordDescription.clear();
    published.keyName.clear();
    published.progression.clear();
}

void ListeningEngine::startRecording()
{
    const juce::ScopedLock scoped (recordLock);

    session = ListeningSessionData {};
    session.deviceName = capture.deviceName();
    session.sampleRate = capture.sampleRate();
    recordStartSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    recordingFull = false;
    recording = true;
}

juce::File ListeningEngine::stopRecording (juce::String& error)
{
    ListeningSessionData finished;

    {
        const juce::ScopedLock scoped (recordLock);

        if (! recording)
            return {};

        recording = false;
        finished = std::move (session);
        session = ListeningSessionData {};
    }

    // El disco, fuera del cerrojo y fuera del hilo de captura.
    return writeListeningSession (finished, error);
}

bool ListeningEngine::isRecording() const
{
    const juce::ScopedLock scoped (recordLock);
    return recording;
}

bool ListeningEngine::recordingIsFull() const
{
    const juce::ScopedLock scoped (recordLock);
    return recordingFull;
}

void ListeningEngine::recordNote (int pitch, bool isOn, double wallSeconds)
{
    const juce::ScopedLock scoped (recordLock);

    if (! recording)
        return;

    session.notes.push_back ({ wallSeconds - recordStartSeconds, pitch, isOn });
}

void ListeningEngine::appendToRecording (const float* samples, int numSamples, double rate)
{
    const juce::ScopedLock scoped (recordLock);

    if (! recording || recordingFull || rate <= 0.0)
        return;

    if (session.sampleRate <= 0.0)
        session.sampleRate = rate;

    // **El audio tiene que seguir al reloj de pared, no al revés.** En loopback,
    // si la canción se pausa Windows no entrega silencio: no entrega nada. Sin
    // rellenar ese hueco, todo lo que viniera después quedaría adelantado
    // respecto a tus teclas, y el informe te acusaría de ir tarde por haber
    // pausado la canción. Se rellena sólo si el hueco pasa de una décima: por
    // debajo es el vaivén normal de llegada de los paquetes.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto expectedEnd = static_cast<std::int64_t> ((now - recordStartSeconds) * session.sampleRate);
    const auto written = static_cast<std::int64_t> (session.audio.size());
    const auto tolerance = static_cast<std::int64_t> (0.1 * session.sampleRate);
    const auto limit = static_cast<std::size_t> (maxRecordingSeconds * session.sampleRate);

    if (written + numSamples < expectedEnd - tolerance)
        session.audio.resize (std::min<std::size_t> (limit, static_cast<std::size_t> (expectedEnd - numSamples)), 0);

    for (int i = 0; i < numSamples && session.audio.size() < limit; ++i)
        session.audio.push_back (static_cast<std::int16_t> (
            juce::jlimit (-32767, 32767, static_cast<int> (std::lrint (samples[i] * 32767.0f)))));

    if (session.audio.size() >= limit)
        recordingFull = true;
}

ListeningReading ListeningEngine::reading() const
{
    const juce::ScopedLock scoped (lock);
    return published;
}

void ListeningEngine::handleAudio (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;

    // La frecuencia de muestreo la decide el dispositivo capturado, no Keyla:
    // el mezclador de Windows puede estar a 44,1 y el análisis tiene que
    // ajustarse a eso, no al revés. Se prepara aquí, en el mismo hilo que luego
    // empuja, para no tener que sincronizar nada.
    const auto rate = capture.sampleRate();

    if (rate > 0.0 && rate != preparedRate)
    {
        analyser.prepare (rate);
        listener.reset();
        preparedRate = rate;
    }

    if (preparedRate <= 0.0)
        return;

    appendToRecording (samples, numSamples, rate);

    double blockEnergy = 0.0;

    for (int i = 0; i < numSamples; ++i)
        blockEnergy += static_cast<double> (samples[i]) * samples[i];

    blockEnergy = std::sqrt (blockEnergy / numSamples);

    // Seguidor lento: lo que interesa es "hay música sonando", no el pico de
    // este bloque concreto.
    recentEnergy += 0.12 * (blockEnergy - recentEnergy);

    analyser.push (samples, numSamples);

    core::Chroma frame;
    bool produced = false;

    while (analyser.popFrame (frame))
    {
        listener.observe (frame, analyser.pitchProfile(), analyser.lowestPitch());
        produced = true;
    }

    if (! produced)
        return;

    const auto chord = listener.current();
    const auto key = listener.key();

    juce::StringArray recent;

    const auto& history = listener.progression();
    const int shown = juce::jmin (8, static_cast<int> (history.size()));

    for (int i = static_cast<int> (history.size()) - shown; i < static_cast<int> (history.size()); ++i)
        recent.add (history[static_cast<std::size_t> (i)].symbol);

    const juce::ScopedLock scoped (lock);

    published.active = true;
    published.receivingAudio = capture.samplesCaptured() > 0;

    // Un umbral bajo pero no cero: una salida abierta sin nada reproduciéndose
    // entrega paquetes de silencio, y eso no es música.
    published.hearingMusic = recentEnergy > 1.0e-4;

    published.rootPitchClass = chord.recognised ? chord.rootPitchClass : -1;
    published.quality = chord.recognised ? chord.quality : core::ChordQuality::unknown;
    published.chordSymbol = chord.recognised ? chord.symbol : juce::String();
    published.chordDescription = chord.recognised ? chord.description : juce::String();
    published.keyName = key.recognised ? key.name : juce::String();
    published.confidence = chord.confidence;
    published.margin = chord.margin;
    published.windowSeconds = analyser.windowSeconds();
    published.deviceName = capture.deviceName();
    published.progression = recent.joinIntoString ("  ");
}

} // namespace keyla::app
