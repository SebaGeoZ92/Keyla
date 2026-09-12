#include "ListeningSession.h"

#include <core/instrument/Instruments.h>
#include <core/listen/Accompaniment.h>
#include <core/listen/ChordTimeline.h>
#include <core/listen/Chromagram.h>
#include <core/listen/HarmonyFromAudio.h>
#include <core/music/Pitch.h>
#include <core/text/Utf8.h>

#include <algorithm>
#include <cmath>

namespace keyla::app
{

using keyla::operator""_u8;

namespace
{
    juce::String chordSymbol (int root, core::ChordQuality quality)
    {
        if (root < 0)
            return "-";

        return core::pitchClassName (root) + core::ChordRecognizer::qualitySymbol (quality);
    }

    juce::String clock (double seconds)
    {
        const auto whole = static_cast<int> (std::floor (seconds));
        const auto tenths = static_cast<int> (std::floor ((seconds - whole) * 10.0));

        return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2)
             + "." + juce::String (tenths);
    }

    juce::String percent (double fraction)
    {
        return juce::String (juce::roundToInt (fraction * 100.0)) + " %";
    }

    void writeWav (juce::MemoryOutputStream& out, const std::vector<std::int16_t>& samples, int rate)
    {
        const auto dataBytes = static_cast<std::uint32_t> (samples.size() * 2);

        out.write ("RIFF", 4);
        out.writeInt (static_cast<int> (36 + dataBytes));
        out.write ("WAVE", 4);

        out.write ("fmt ", 4);
        out.writeInt (16);
        out.writeShort (1);                     // PCM
        out.writeShort (1);                     // mono
        out.writeInt (rate);
        out.writeInt (rate * 2);                // bytes por segundo
        out.writeShort (2);                     // bytes por bloque
        out.writeShort (16);                    // bits

        out.write ("data", 4);
        out.writeInt (static_cast<int> (dataBytes));
        out.write (samples.data(), samples.size() * 2);
    }

    bool readWav (const juce::File& file, std::vector<std::int16_t>& samples, double& rate)
    {
        juce::MemoryBlock block;

        if (! file.loadFileAsData (block) || block.getSize() < 44)
            return false;

        const auto* bytes = static_cast<const char*> (block.getData());
        const auto size = block.getSize();

        if (std::memcmp (bytes, "RIFF", 4) != 0 || std::memcmp (bytes + 8, "WAVE", 4) != 0)
            return false;

        int channels = 0, bits = 0;
        std::size_t position = 12;

        // Se recorren los trozos en vez de suponer la cabecera de 44 bytes: así
        // también se lee un WAV que haya pasado por otro programa.
        while (position + 8 <= size)
        {
            const auto chunkSize = static_cast<std::size_t> (
                juce::ByteOrder::littleEndianInt (bytes + position + 4));
            const auto* body = bytes + position + 8;

            if (std::memcmp (bytes + position, "fmt ", 4) == 0 && chunkSize >= 16)
            {
                channels = juce::ByteOrder::littleEndianShort (body + 2);
                rate = juce::ByteOrder::littleEndianInt (body + 4);
                bits = juce::ByteOrder::littleEndianShort (body + 14);
            }
            else if (std::memcmp (bytes + position, "data", 4) == 0)
            {
                if (bits != 16 || channels < 1)
                    return false;

                const auto frames = std::min (chunkSize, size - (position + 8)) / (2u * channels);
                samples.resize (frames);

                for (std::size_t i = 0; i < frames; ++i)
                {
                    int sum = 0;

                    for (int c = 0; c < channels; ++c)
                        sum += static_cast<std::int16_t> (
                            juce::ByteOrder::littleEndianShort (body + (i * channels + c) * 2));

                    samples[i] = static_cast<std::int16_t> (sum / channels);
                }

                return rate > 0.0;
            }

            position += 8 + chunkSize + (chunkSize & 1);
        }

        return false;
    }
}

juce::File listeningSessionsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Keyla")
               .getChildFile ("sesiones");
}

juce::File writeListeningSession (const ListeningSessionData& data, juce::String& error)
{
    if (data.audio.empty() || data.sampleRate <= 0.0)
    {
        error = "No se grabo nada: no llego audio mientras se grababa."_u8;
        return {};
    }

    const auto folder = listeningSessionsFolder().getChildFile (
        "escucha-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"));

    if (! folder.createDirectory())
    {
        error = "No se pudo crear la carpeta de la sesion."_u8;
        return {};
    }

    juce::MemoryOutputStream wav;
    writeWav (wav, data.audio, static_cast<int> (data.sampleRate));

    if (! folder.getChildFile ("audio.wav").replaceWithData (wav.getData(), wav.getDataSize()))
    {
        error = "No se pudo escribir el audio."_u8;
        return {};
    }

    juce::StringArray csv;
    csv.add ("segundos,nota,pulsada");

    for (const auto& note : data.notes)
        csv.add (juce::String (note.seconds, 4) + "," + juce::String (note.pitch)
                 + "," + (note.isOn ? "1" : "0"));

    folder.getChildFile ("notas.csv").replaceWithText (csv.joinIntoString ("\n") + "\n");

    juce::StringArray info;
    info.add ("dispositivo=" + data.deviceName);
    info.add ("frecuencia=" + juce::String (data.sampleRate, 0));
    info.add ("duracion=" + juce::String (data.durationSeconds(), 2));
    folder.getChildFile ("sesion.txt").replaceWithText (info.joinIntoString ("\n") + "\n");

    return folder;
}

bool readListeningSession (const juce::File& folder, ListeningSessionData& data, juce::String& error)
{
    if (! readWav (folder.getChildFile ("audio.wav"), data.audio, data.sampleRate))
    {
        error = "No se pudo leer audio.wav en "_u8 + folder.getFullPathName();
        return false;
    }

    juce::StringArray lines;
    lines.addLines (folder.getChildFile ("notas.csv").loadFileAsString());

    for (int i = 1; i < lines.size(); ++i)
    {
        const auto fields = juce::StringArray::fromTokens (lines[i], ",", "");

        if (fields.size() < 3)
            continue;

        data.notes.push_back ({ fields[0].getDoubleValue(), fields[1].getIntValue(),
                                fields[2].getIntValue() != 0 });
    }

    juce::StringArray info;
    info.addLines (folder.getChildFile ("sesion.txt").loadFileAsString());

    for (const auto& line : info)
        if (line.startsWith ("dispositivo="))
            data.deviceName = line.fromFirstOccurrenceOf ("=", false, false);

    return true;
}

juce::String analyseListeningSession (const juce::File& folder, juce::String& error)
{
    ListeningSessionData data;

    if (! readListeningSession (folder, data, error))
        return {};

    const double duration = data.durationSeconds();

    // ── Lo que oyó Keyla, reanalizado ───────────────────────────────────────
    std::vector<float> mono (data.audio.size());

    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = data.audio[i] / 32768.0f;

    int lowestPitch = 0;
    const auto frames = core::computeChromaFrames (mono, data.sampleRate, {}, &lowestPitch);

    core::KeyEstimate key;
    const auto heard = core::chordsFromFrames (frames, lowestPitch, core::HarmonyListener::Options {}, &key);

    // ── Lo que tocaste ──────────────────────────────────────────────────────
    const auto played = core::chordsFromNotes (data.notes);
    const auto evaluation = core::evaluateListening (heard, played, duration);

    // ── El informe ──────────────────────────────────────────────────────────
    juce::StringArray report;

    report.add ("Keyla - sesion de escucha");
    report.add ("Carpeta: " + folder.getFullPathName());
    report.add ("Duracion: " + clock (duration) + "   salida: " + data.deviceName);

    if (key.recognised)
        report.add ("Tonalidad que oyo Keyla: " + key.name);

    report.add ({});

    if (played.empty())
    {
        report.add ("No tocaste acordes (tres o mas notas a la vez) mientras se grababa,");
        report.add ("asi que no hay con que comparar. Abajo va solo lo que oyo Keyla.");
    }
    else if (! evaluation.valid)
    {
        report.add ("Tocaste, pero no hubo tiempo en que Keyla y tu tuvierais acorde a la vez.");
    }
    else
    {
        report.add ("CUANTO COINCIDIO KEYLA CON LO QUE TOCASTE");
        report.add ("  Fundamental:            " + percent (evaluation.rootAgreement));
        report.add ("  Fundamental y tipo:     " + percent (evaluation.fullAgreement));
        report.add ("  Tiempo comparado:       " + clock (evaluation.comparedSeconds));

        const auto lag = evaluation.lagSeconds;

        report.add ("  Desfase:                " + juce::String (std::abs (lag), 2) + " s "
                    + (lag >= 0.0 ? "(tus manos van detras de lo que oye Keyla)"
                                  : "(tus manos van delante de lo que oye Keyla)"));

        if (evaluation.comparedSeconds < 20.0)
            report.add ("  Ojo: menos de 20 s comparados. Los porcentajes son orientativos.");

        report.add ({});
        report.add ("ACUERDO POR TRAMOS DE 30 s (donde baja mucho, o fallo Keyla o no seguias la cancion)");

        for (double from = 0.0; from < duration; from += 30.0)
        {
            const auto section = core::evaluateListening (heard, played, std::min (duration, from + 30.0),
                                                          0.0, 0.05, from);

            juce::String bar;

            if (section.valid)
                bar = juce::String::repeatedString ("#", juce::roundToInt (section.rootAgreement * 20.0))
                          .paddedRight ('.', 20)
                    + "  " + percent (section.rootAgreement);
            else
                bar = "(sin acordes tuyos)";

            report.add ("  " + clock (from).paddedRight (' ', 8) + bar);
        }

        report.add ({});
        report.add ("DONDE MAS DISCREPASTEIS");

        if (evaluation.confusions.empty())
        {
            report.add ("  En ningun sitio.");
        }
        else
        {
            for (std::size_t i = 0; i < std::min<std::size_t> (8, evaluation.confusions.size()); ++i)
            {
                const auto& c = evaluation.confusions[i];

                report.add ("  tocaste " + chordSymbol (c.playedRoot, c.playedQuality).paddedRight (' ', 6)
                            + " y Keyla oyo " + chordSymbol (c.heardRoot, c.heardQuality).paddedRight (' ', 6)
                            + "  durante " + juce::String (c.seconds, 1) + " s");
            }
        }
    }

    report.add ({});
    report.add ("LINEA DE TIEMPO (cada cambio que oyo Keyla)");
    report.add ("  tiempo   oyo      sugirio tocar                 tocaste");

    core::AccompanimentCoach coach;

    for (const auto& chord : heard)
    {
        const auto suggestion = coach.suggest (chord.rootPitchClass, chord.quality);
        const auto* mine = core::chordAt (played, chord.seconds + evaluation.lagSeconds);

        report.add ("  " + clock (chord.seconds).paddedRight (' ', 8)
                    + " " + chordSymbol (chord.rootPitchClass, chord.quality).paddedRight (' ', 8)
                    + " " + suggestion.description.paddedRight (' ', 30)
                    + " " + (mine != nullptr ? chordSymbol (mine->rootPitchClass, mine->quality)
                                             : juce::String ("-")));
    }

    const auto text = report.joinIntoString ("\n") + "\n";
    folder.getChildFile ("informe.txt").replaceWithText (text);

    return text;
}

juce::String tuneListeningSession (const juce::File& folder,
                                   double fromSeconds, double toSeconds,
                                   juce::String& error)
{
    ListeningSessionData data;

    if (! readListeningSession (folder, data, error))
        return {};

    const double duration = data.durationSeconds();
    const double start = fromSeconds >= 0.0 ? fromSeconds : 0.0;
    const double end = toSeconds > start ? std::min (duration, toSeconds) : duration;

    std::vector<float> mono (data.audio.size());

    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = data.audio[i] / 32768.0f;

    int lowestPitch = 0;
    const auto frames = core::computeChromaFrames (mono, data.sampleRate, {}, &lowestPitch);
    const auto played = core::chordsFromNotes (data.notes);

    struct Trial
    {
        core::HarmonyListener::Options options;
        core::ListeningEvaluation result;
        int changes { 0 };
    };

    std::vector<Trial> trials;

    for (double sharpening : { 1.0, 1.5, 2.0 })
     for (double penalty : { 0.0, 0.10, 0.20, 0.30, 0.50 })
      for (double seventh : { 0.0, 0.05, 0.10 })
       for (double bass : { 0.03, 0.08 })
        for (int agree : { 2, 3, 4 })
        {
            core::HarmonyListener::Options options;
            options.sharpening = sharpening;
            options.complexQualityPenalty = penalty;
            options.seventhQualityPenalty = seventh;
            options.bassIsRootBonus = bass;
            options.framesToAgree = agree;

            const auto heard = core::chordsFromFrames (frames, lowestPitch, options);

            Trial trial;
            trial.options = options;
            trial.result = core::evaluateListening (heard, played, end, 1.0, 0.05, start);
            trial.changes = static_cast<int> (heard.size());
            trials.push_back (trial);
        }

    std::sort (trials.begin(), trials.end(), [] (const Trial& a, const Trial& b)
    {
        return a.result.rootAgreement + a.result.fullAgreement
             > b.result.rootAgreement + b.result.fullAgreement;
    });

    const auto describe = [] (const Trial& t)
    {
        return "agudizar " + juce::String (t.options.sharpening, 1)
             + "  raros " + juce::String (t.options.complexQualityPenalty, 2)
             + "  sept " + juce::String (t.options.seventhQualityPenalty, 2)
             + "  bajo " + juce::String (t.options.bassIsRootBonus, 2)
             + "  fotogr " + juce::String (t.options.framesToAgree)
             + "   ->  fund " + percent (t.result.rootAgreement)
             + "  tipo " + percent (t.result.fullAgreement)
             + "  cambios " + juce::String (t.changes);
    };

    juce::StringArray report;
    report.add ("Keyla - ajuste del reconocimiento sobre una grabacion");
    report.add ("Tramo comparado: " + clock (start) + " a " + clock (end)
                + "   ajustes probados: " + juce::String (static_cast<int> (trials.size())));
    report.add ({});

    // El actual, para comparar contra algo real y no contra la nada.
    core::HarmonyListener::Options current;
    const auto currentHeard = core::chordsFromFrames (frames, lowestPitch, current);
    Trial baseline;
    baseline.options = current;
    baseline.result = core::evaluateListening (currentHeard, played, end, 1.0, 0.05, start);
    baseline.changes = static_cast<int> (currentHeard.size());

    report.add ("AJUSTE ACTUAL");
    report.add ("  " + describe (baseline));
    report.add ({});
    report.add ("LOS 25 MEJORES");

    for (std::size_t i = 0; i < std::min<std::size_t> (25, trials.size()); ++i)
        report.add ("  " + describe (trials[i]));

    report.add ({});
    report.add ("LOS 5 PEORES");

    for (std::size_t i = trials.size() > 5 ? trials.size() - 5 : 0; i < trials.size(); ++i)
        report.add ("  " + describe (trials[i]));

    report.add ({});
    report.add ("TODOS");

    for (const auto& trial : trials)
        report.add ("  " + describe (trial));

    const auto text = report.joinIntoString ("\n") + "\n";
    folder.getChildFile ("ajuste.txt").replaceWithText (text);
    return text;
}

namespace
{
    /** Un acorde limpio tocado por el piano de Keyla, ya escuchado. */
    struct CleanChord
    {
        int root;
        core::ChordQuality quality;
        core::Chroma chroma;
    };

    /** Los mismos 48 acordes que usa el test de acierto: doce fundamentales en
        mayor, menor, séptima y menor séptima. Se renderizan y se escuchan una
        vez; cada ajuste sólo repite la decisión, que es lo barato. */
    std::vector<CleanChord> renderCleanChords()
    {
        constexpr double rate = 48000.0;
        constexpr int block = 512;

        std::vector<CleanChord> chords;

        for (auto quality : { core::ChordQuality::major, core::ChordQuality::minor,
                              core::ChordQuality::dominant7, core::ChordQuality::minor7 })
        {
            for (int root = 0; root < 12; ++root)
            {
                auto piano = core::createInstrument (core::InstrumentId::piano);
                piano->prepare (rate, block);

                std::vector<core::StampedMidiEvent> events;

                for (auto interval : core::ChordRecognizer::intervalsFor (quality))
                {
                    core::StampedMidiEvent event;
                    event.message.bytes[0] = 0x90;
                    event.message.bytes[1] = static_cast<std::uint8_t> (48 + root + interval);
                    event.message.bytes[2] = 100;
                    event.message.size = 3;
                    event.renderOffset = 0;
                    events.push_back (event);
                }

                juce::AudioBuffer<float> buffer (2, block);
                std::vector<float> mono;

                for (int b = 0; b < static_cast<int> (rate / block); ++b)
                {
                    buffer.clear();
                    piano->process (buffer, b == 0 ? core::MidiEventSpan { events.data(), events.size() }
                                                   : core::MidiEventSpan { nullptr, 0 });

                    const auto* data = buffer.getReadPointer (0);
                    mono.insert (mono.end(), data, data + block);
                }

                const auto frames = core::computeChromaFrames (mono, rate);

                if (! frames.empty())
                    chords.push_back ({ root, quality, frames.back().chroma });
            }
        }

        return chords;
    }

    double cleanTypeAccuracy (const std::vector<CleanChord>& chords,
                              const core::HarmonyListener::Options& options)
    {
        if (chords.empty())
            return 0.0;

        int hits = 0;

        for (const auto& chord : chords)
        {
            const auto estimate = core::HarmonyListener::estimate (chord.chroma, chord.root, options);

            if (estimate.rootPitchClass == chord.root && estimate.quality == chord.quality)
                ++hits;
        }

        return static_cast<double> (hits) / static_cast<double> (chords.size());
    }
}

juce::String tuneAcrossSessions (const juce::Array<juce::File>& folders,
                                 const juce::File& outputFile,
                                 juce::String& error)
{
    struct Song
    {
        juce::String name;
        double duration { 0.0 };
        int lowestPitch { 0 };
        std::vector<core::ChromaFrame> frames;
        std::vector<core::TimedChord> played;
    };

    std::vector<Song> songs;

    for (const auto& folder : folders)
    {
        ListeningSessionData data;

        if (! readListeningSession (folder, data, error))
            return {};

        Song song;
        song.name = folder.getFileName();
        song.duration = data.durationSeconds();

        std::vector<float> mono (data.audio.size());

        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] = data.audio[i] / 32768.0f;

        song.frames = core::computeChromaFrames (mono, data.sampleRate, {}, &song.lowestPitch);
        song.played = core::chordsFromNotes (data.notes);
        songs.push_back (std::move (song));
    }

    if (songs.empty())
    {
        error = "No hay sesiones que comparar."_u8;
        return {};
    }

    const auto clean = renderCleanChords();

    struct Trial
    {
        core::HarmonyListener::Options options;
        std::vector<core::ListeningEvaluation> perSong;
        double meanRoot { 0.0 };
        double meanType { 0.0 };
        double worstType { 1.0 };
        double cleanType { 0.0 };

        double score() const { return 0.5 * (meanRoot + meanType); }
    };

    const auto evaluate = [&songs, &clean] (const core::HarmonyListener::Options& options)
    {
        Trial trial;
        trial.options = options;

        for (const auto& song : songs)
        {
            const auto heard = core::chordsFromFrames (song.frames, song.lowestPitch, options);
            const auto result = core::evaluateListening (heard, song.played, song.duration, 1.0, 0.05);

            trial.perSong.push_back (result);
            trial.meanRoot += result.rootAgreement / songs.size();
            trial.meanType += result.fullAgreement / songs.size();
            trial.worstType = std::min (trial.worstType, result.fullAgreement);
        }

        trial.cleanType = cleanTypeAccuracy (clean, options);
        return trial;
    };

    std::vector<Trial> trials;

    for (double sharpening : { 1.0, 1.5, 2.0 })
     for (double rare : { 0.10, 0.30, 0.50 })
      for (double seventh : { 0.0, 0.02, 0.05, 0.08, 0.10 })
       for (double bass : { 0.03, 0.08, 0.15 })
        for (int agree : { 3, 4 })
        {
            core::HarmonyListener::Options options;
            options.sharpening = sharpening;
            options.complexQualityPenalty = rare;
            options.seventhQualityPenalty = seventh;
            options.bassIsRootBonus = bass;
            options.framesToAgree = agree;
            trials.push_back (evaluate (options));
        }

    std::sort (trials.begin(), trials.end(),
               [] (const Trial& a, const Trial& b) { return a.score() > b.score(); });

    const auto describe = [] (const Trial& t)
    {
        juce::String line;
        line << "agudizar " << juce::String (t.options.sharpening, 1)
             << "  raros " << juce::String (t.options.complexQualityPenalty, 2)
             << "  sept " << juce::String (t.options.seventhQualityPenalty, 2)
             << "  bajo " << juce::String (t.options.bassIsRootBonus, 2)
             << "  fotogr " << t.options.framesToAgree
             << "   ->  media " << percent (t.meanRoot) << " / " << percent (t.meanType)
             << "   limpios " << percent (t.cleanType) << "   [";

        for (std::size_t i = 0; i < t.perSong.size(); ++i)
            line << (i > 0 ? "  " : "") << percent (t.perSong[i].rootAgreement)
                 << "/" << percent (t.perSong[i].fullAgreement);

        return line + "]";
    };

    juce::StringArray report;
    report.add ("Keyla - ajuste sobre varias canciones");
    report.add ("Ajustes probados: " + juce::String (static_cast<int> (trials.size())));
    report.add ("Cada fila: acorde / tipo, de media y por cancion. 'limpios' = acierto de tipo");
    report.add ("en los 48 acordes del piano de Keyla. Por debajo del 90 % el ajuste queda descalificado.");
    report.add ({});
    report.add ("CANCIONES");

    for (std::size_t i = 0; i < songs.size(); ++i)
        report.add ("  " + juce::String (static_cast<int> (i + 1)) + ". " + songs[i].name
                    + "   " + clock (songs[i].duration));

    report.add ({});
    report.add ("AJUSTE ACTUAL");
    report.add ("  " + describe (evaluate (core::HarmonyListener::Options {})));

    report.add ({});
    report.add ("LOS 20 MEJORES QUE NO ROMPEN LOS ACORDES LIMPIOS");

    int shown = 0;

    for (const auto& trial : trials)
    {
        if (trial.cleanType < 0.90)
            continue;

        report.add ("  " + describe (trial));

        if (++shown == 20)
            break;
    }

    report.add ({});
    report.add ("LOS 5 MEJORES SIN MIRAR LOS LIMPIOS (lo que costaria no protegerlos)");

    for (std::size_t i = 0; i < std::min<std::size_t> (5, trials.size()); ++i)
        report.add ("  " + describe (trials[i]));

    report.add ({});
    report.add ("TODOS");

    for (const auto& trial : trials)
        report.add ("  " + describe (trial));

    const auto text = report.joinIntoString ("\n") + "\n";
    outputFile.replaceWithText (text);
    return text;
}

} // namespace keyla::app
