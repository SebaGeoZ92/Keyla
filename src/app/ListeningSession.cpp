#include "ListeningSession.h"

#include <core/listen/Accompaniment.h>
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
    core::ChromaAnalyser analyser;
    analyser.prepare (data.sampleRate);

    core::HarmonyListener listener;
    std::vector<core::TimedChord> heard;

    constexpr int block = 4096;
    std::vector<float> scratch (block);
    std::size_t pushed = 0;

    while (pushed < data.audio.size())
    {
        const auto count = std::min<std::size_t> (block, data.audio.size() - pushed);

        for (std::size_t i = 0; i < count; ++i)
            scratch[i] = data.audio[pushed + i] / 32768.0f;

        analyser.push (scratch.data(), static_cast<int> (count));
        pushed += count;

        core::Chroma frame;

        while (analyser.popFrame (frame))
        {
            const auto current = listener.observe (frame, analyser.pitchProfile(), analyser.lowestPitch());

            if (current.recognised
                && (heard.empty()
                    || heard.back().rootPitchClass != current.rootPitchClass
                    || heard.back().quality != current.quality))
                heard.push_back ({ pushed / data.sampleRate, current.rootPitchClass, current.quality });
        }
    }

    // ── Lo que tocaste ──────────────────────────────────────────────────────
    const auto played = core::chordsFromNotes (data.notes);
    const auto evaluation = core::evaluateListening (heard, played, duration);

    // ── El informe ──────────────────────────────────────────────────────────
    juce::StringArray report;

    report.add ("Keyla - sesion de escucha");
    report.add ("Carpeta: " + folder.getFullPathName());
    report.add ("Duracion: " + clock (duration) + "   salida: " + data.deviceName);

    const auto key = listener.key();

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

} // namespace keyla::app
