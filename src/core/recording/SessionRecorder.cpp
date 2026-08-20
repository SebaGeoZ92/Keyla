#include "SessionRecorder.h"

#include <algorithm>

namespace keyla::core
{

double PlayedEvent::onsetSample() const noexcept
{
    if (notes.empty())
        return 0.0;

    double earliest = notes.front().onsetSample;

    for (const auto& note : notes)
        earliest = std::min (earliest, note.onsetSample);

    return earliest;
}

double PlayedEvent::meanVelocity() const noexcept
{
    if (notes.empty())
        return 0.0;

    double sum = 0.0;

    for (const auto& note : notes)
        sum += note.velocity;

    return sum / static_cast<double> (notes.size());
}

bool PlayedEvent::contains (int pitch) const noexcept
{
    return std::any_of (notes.begin(), notes.end(),
                        [pitch] (const RecordedNote& note) { return note.pitch == pitch; });
}

// ── Grabación ───────────────────────────────────────────────────────────────

void SessionRecorder::start (double sampleRateToUse)
{
    recorded.clear();
    rate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;
    startedAt = 0.0;
    stoppedAt = 0.0;
    recording = true;
    sustainDown = false;
}

void SessionRecorder::stop (double endSample)
{
    recording = false;
    stoppedAt = endSample;

    // Las notas que seguían pulsadas al parar se cierran aquí. Dejarlas
    // abiertas haría que su duración fuese cero y el informe diría que se
    // soltaron al instante, que es justo lo contrario de lo que pasó.
    for (auto& note : recorded)
        if (! note.isFinished())
            note.offsetSample = endSample;
}

void SessionRecorder::clear()
{
    recorded.clear();
    recording = false;
    startedAt = 0.0;
    stoppedAt = 0.0;
    sustainDown = false;
}

void SessionRecorder::noteOn (int pitch, int velocity, double exactSample, int channel)
{
    if (! recording)
        return;

    RecordedNote note;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    note.onsetSample = exactSample;
    note.offsetSample = -1.0;
    note.sustainedByPedal = false;

    recorded.push_back (note);
}

void SessionRecorder::noteOff (int pitch, double exactSample)
{
    if (! recording)
        return;

    // Se cierra la más reciente que siga abierta con esa altura: repetir una
    // nota antes de soltar la anterior es normal tocando, y cerrar la más
    // antigua descuadraría las duraciones.
    for (auto it = recorded.rbegin(); it != recorded.rend(); ++it)
    {
        if (it->pitch != pitch || it->isFinished())
            continue;

        it->offsetSample = exactSample;
        it->sustainedByPedal = sustainDown;
        return;
    }
}

void SessionRecorder::setSustainPedal (int value, double)
{
    sustainDown = value >= sustainPedalThreshold;
}

double SessionRecorder::lengthSamples() const noexcept
{
    if (recorded.empty())
        return 0.0;

    double last = stoppedAt;

    for (const auto& note : recorded)
        last = std::max (last, note.isFinished() ? note.offsetSample : note.onsetSample);

    return std::max (0.0, last - startedAt);
}

std::vector<PlayedEvent> SessionRecorder::groupIntoEvents (double toleranceSeconds) const
{
    std::vector<PlayedEvent> events;

    if (recorded.empty())
        return events;

    auto sorted = recorded;
    std::sort (sorted.begin(), sorted.end(),
               [] (const RecordedNote& a, const RecordedNote& b)
               { return a.onsetSample < b.onsetSample; });

    const double tolerance = toleranceSeconds * rate;

    PlayedEvent group;
    group.notes.push_back (sorted.front());
    double groupStart = sorted.front().onsetSample;

    for (std::size_t i = 1; i < sorted.size(); ++i)
    {
        // Se compara contra el **inicio** del grupo, no contra la nota anterior.
        // Encadenando contra la anterior, una escala rápida acabaría siendo un
        // solo acorde gigante de treinta notas.
        if (sorted[i].onsetSample - groupStart <= tolerance)
        {
            group.notes.push_back (sorted[i]);
            continue;
        }

        events.push_back (std::move (group));
        group = PlayedEvent {};
        group.notes.push_back (sorted[i]);
        groupStart = sorted[i].onsetSample;
    }

    events.push_back (std::move (group));

    return events;
}

} // namespace keyla::core
