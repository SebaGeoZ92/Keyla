#include "MidiFileImporter.h"

#include "../text/Utf8.h"

#include <algorithm>
#include <map>

namespace keyla::core
{

using keyla::operator""_u8;

juce::String handSeparationDescription (HandSeparation separation)
{
    switch (separation)
    {
        case HandSeparation::notAttempted:  return "sin separar";
        case HandSeparation::byTrack:       return "manos separadas por pista"_u8;
        case HandSeparation::byChannel:     return "manos separadas por canal";
        case HandSeparation::bySplitPoint:  return "manos deducidas por altura: revísalo"_u8;
        case HandSeparation::singleHand:    return "una sola mano";
    }

    return {};
}

namespace
{
    /** Una nota extraída del fichero, todavía sin agrupar. */
    struct ImportedNote
    {
        int pitch { 0 };
        int velocity { 0 };
        double startBeat { 0.0 };
        double endBeat { 0.0 };
        int track { 0 };
        int channel { 1 };
        Hand hand { Hand::unknown };
    };

    /** Media de altura de un conjunto, para decidir cuál es la mano derecha. */
    double meanPitch (const std::vector<ImportedNote>& notes,
                      const std::function<bool (const ImportedNote&)>& predicate)
    {
        double sum = 0.0;
        int count = 0;

        for (const auto& note : notes)
        {
            if (! predicate (note))
                continue;

            sum += note.pitch;
            ++count;
        }

        return count > 0 ? sum / count : 0.0;
    }

    /** Decide la mano de cada nota y devuelve cómo lo decidió.

        El orden importa: la pista y el canal son información que *puso alguien*,
        mientras que partir por altura es una conjetura nuestra. Se prefiere
        siempre el dato sobre la conjetura. */
    HandSeparation assignHands (std::vector<ImportedNote>& notes, int splitPoint)
    {
        if (notes.empty())
            return HandSeparation::notAttempted;

        std::set<int> tracks, channels;

        for (const auto& note : notes)
        {
            tracks.insert (note.track);
            channels.insert (note.channel);
        }

        auto assignByKey = [&notes] (const std::function<int (const ImportedNote&)>& keyOf,
                                     int keyA, int keyB)
        {
            const double meanA = meanPitch (notes, [&] (const ImportedNote& n) { return keyOf (n) == keyA; });
            const double meanB = meanPitch (notes, [&] (const ImportedNote& n) { return keyOf (n) == keyB; });

            const int rightKey = meanA >= meanB ? keyA : keyB;

            for (auto& note : notes)
                note.hand = keyOf (note) == rightKey ? Hand::right : Hand::left;
        };

        if (tracks.size() == 2)
        {
            const auto it = tracks.begin();
            assignByKey ([] (const ImportedNote& n) { return n.track; }, *it, *std::next (it));
            return HandSeparation::byTrack;
        }

        if (channels.size() == 2)
        {
            const auto it = channels.begin();
            assignByKey ([] (const ImportedNote& n) { return n.channel; }, *it, *std::next (it));
            return HandSeparation::byChannel;
        }

        // Todo junto: sólo queda la altura, que es una conjetura y se dice.
        int lowest = 127, highest = 0;

        for (const auto& note : notes)
        {
            lowest = std::min (lowest, note.pitch);
            highest = std::max (highest, note.pitch);
        }

        if (highest - lowest <= 16)
        {
            // Menos de una octava y media: cabe en una mano, y partirla por la
            // mitad sería inventarse una separación que no existe.
            for (auto& note : notes)
                note.hand = Hand::right;

            return HandSeparation::singleHand;
        }

        for (auto& note : notes)
            note.hand = note.pitch < splitPoint ? Hand::left : Hand::right;

        return HandSeparation::bySplitPoint;
    }
}

MidiImportResult importMidiFile (juce::InputStream& stream, const MidiImportOptions& options)
{
    MidiImportResult result;

    juce::MidiFile midiFile;

    if (! midiFile.readFrom (stream))
    {
        result.message = "No se pudo leer el fichero: no parece un MIDI válido."_u8;
        return result;
    }

    const auto timeFormat = midiFile.getTimeFormat();

    if (timeFormat <= 0)
    {
        // Formato SMPTE. Se podría convertir, pero un fichero de práctica de
        // piano no viene así y soportarlo sin poder probarlo sería fingir.
        result.message = "Este MIDI usa marcas de tiempo SMPTE, que todavía no se admiten."_u8;
        return result;
    }

    const auto ticksPerBeat = static_cast<double> (timeFormat);

    // ── Tempo y compás ──────────────────────────────────────────────────────
    juce::MidiMessageSequence tempoEvents;
    midiFile.findAllTempoEvents (tempoEvents);
    midiFile.findAllTimeSigEvents (tempoEvents);

    for (int i = 0; i < tempoEvents.getNumEvents(); ++i)
    {
        const auto& message = tempoEvents.getEventPointer (i)->message;

        if (message.isTempoMetaEvent())
        {
            const double secondsPerQuarter = message.getTempoSecondsPerQuarterNote();

            if (secondsPerQuarter > 0.0)
                result.tempoBpm = juce::jlimit (20.0, 300.0, 60.0 / secondsPerQuarter);
        }
        else if (message.isTimeSignatureMetaEvent())
        {
            int numerator = 4, denominator = 4;
            message.getTimeSignatureInfo (numerator, denominator);

            const TimeSignature meter { numerator, denominator };

            if (meter.isValid())
                result.meter = meter;
        }
    }

    // ── Notas ───────────────────────────────────────────────────────────────
    std::vector<ImportedNote> notes;

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex)
    {
        auto track = *midiFile.getTrack (trackIndex);
        track.updateMatchedPairs();

        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            auto* event = track.getEventPointer (i);

            if (! event->message.isNoteOn())
                continue;

            ImportedNote note;
            note.pitch = event->message.getNoteNumber();
            note.velocity = event->message.getVelocity();
            note.channel = event->message.getChannel();
            note.track = trackIndex;
            note.startBeat = event->message.getTimeStamp() / ticksPerBeat;

            note.endBeat = event->noteOffObject != nullptr
                         ? event->noteOffObject->message.getTimeStamp() / ticksPerBeat
                         : note.startBeat + 1.0;

            notes.push_back (note);
        }
    }

    if (notes.empty())
    {
        result.message = "El fichero no contiene ninguna nota."_u8;
        return result;
    }

    result.handSeparation = assignHands (notes, options.splitPoint);

    // ── Filtro de manos ─────────────────────────────────────────────────────
    if (options.hands == Hand::left || options.hands == Hand::right)
    {
        const auto wanted = options.hands;

        notes.erase (std::remove_if (notes.begin(), notes.end(),
                                     [wanted] (const ImportedNote& n) { return n.hand != wanted; }),
                     notes.end());

        if (notes.empty())
        {
            result.message = "No hay notas para esa mano en este fichero."_u8;
            return result;
        }
    }

    std::stable_sort (notes.begin(), notes.end(),
                      [] (const ImportedNote& a, const ImportedNote& b)
                      { return a.startBeat < b.startBeat; });

    // El origen se lleva al primer ataque: un fichero con dos compases de
    // silencio al principio no debe obligar a esperarlos.
    const double origin = notes.front().startBeat;

    // ── Agrupación en acordes ───────────────────────────────────────────────
    for (const auto& note : notes)
    {
        const double onset = note.startBeat - origin;

        const bool joinsPrevious = ! result.exercise.events.empty()
                                && onset - result.exercise.events.back().onsetBeat
                                       <= options.chordToleranceBeats;

        if (joinsPrevious)
        {
            auto& event = result.exercise.events.back();

            // Una altura repetida dentro del mismo acorde es un error del
            // fichero, no un acorde con la nota doblada.
            if (! event.contains (note.pitch))
                event.pitches.push_back (static_cast<std::uint8_t> (note.pitch));

            if (event.hand != note.hand)
                event.hand = Hand::both;

            continue;
        }

        if (static_cast<int> (result.exercise.events.size()) >= options.maxEvents)
        {
            result.truncated = true;
            break;
        }

        ExpectedEvent event;
        event.pitches = { static_cast<std::uint8_t> (note.pitch) };
        event.onsetBeat = onset;
        event.durationBeats = std::max (0.05, note.endBeat - note.startBeat);
        event.hand = note.hand;

        result.exercise.events.push_back (std::move (event));
    }

    // Las alturas de cada acorde, ordenadas de grave a aguda: el resto del
    // sistema asume que la primera es el bajo.
    for (auto& event : result.exercise.events)
        std::sort (event.pitches.begin(), event.pitches.end());

    result.ok = ! result.exercise.isEmpty();
    result.exercise.hint = "Importado de MIDI: sin digitación. "_u8
                         + handSeparationDescription (result.handSeparation) + ".";

    if (! result.ok)
        result.message = "No quedó ninguna nota tras importar."_u8;
    else if (result.truncated)
        result.message = "Importado, pero recortado a "_u8 + juce::String (options.maxEvents)
                       + " eventos: el fichero es muy largo para practicarlo entero."_u8;

    return result;
}

MidiImportResult importMidiFile (const juce::File& file, const MidiImportOptions& options)
{
    MidiImportResult result;

    if (! file.existsAsFile())
    {
        result.message = "No existe el fichero "_u8 + file.getFullPathName() + ".";
        return result;
    }

    juce::FileInputStream stream (file);

    if (! stream.openedOk())
    {
        result.message = "No se pudo abrir "_u8 + file.getFileName() + ".";
        return result;
    }

    auto imported = importMidiFile (stream, options);

    if (imported.ok)
    {
        imported.exercise.id = "midi." + file.getFileNameWithoutExtension().toLowerCase();
        imported.exercise.name = file.getFileNameWithoutExtension();
    }

    return imported;
}

} // namespace keyla::core
