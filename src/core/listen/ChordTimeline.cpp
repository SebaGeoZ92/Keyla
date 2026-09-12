#include "ChordTimeline.h"

#include <algorithm>

namespace keyla::core
{

std::vector<ChromaFrame> computeChromaFrames (const std::vector<float>& mono,
                                              double sampleRate,
                                              ChromaAnalyser::Options options,
                                              int* lowestPitchOut)
{
    ChromaAnalyser analyser;
    analyser.prepare (sampleRate, options);

    if (lowestPitchOut != nullptr)
        *lowestPitchOut = analyser.lowestPitch();

    std::vector<ChromaFrame> frames;

    constexpr std::size_t block = 4096;
    std::size_t pushed = 0;

    while (pushed < mono.size())
    {
        const auto count = std::min (block, mono.size() - pushed);
        analyser.push (mono.data() + pushed, static_cast<int> (count));
        pushed += count;

        ChromaFrame frame;

        while (analyser.popFrame (frame.chroma))
        {
            // El instante es el del final de lo empujado: es lo mismo que ve la
            // escucha en directo, donde el acorde se anuncia al llegar el audio.
            frame.seconds = static_cast<double> (pushed) / sampleRate;
            frame.profile = analyser.pitchProfile();
            frames.push_back (frame);
        }
    }

    return frames;
}

std::vector<TimedChord> chordsFromFrames (const std::vector<ChromaFrame>& frames,
                                          int lowestPitch,
                                          const HarmonyListener::Options& options,
                                          KeyEstimate* keyOut)
{
    HarmonyListener listener { options };
    std::vector<TimedChord> chords;

    for (const auto& frame : frames)
    {
        const auto current = listener.observe (frame.chroma, frame.profile, lowestPitch);

        if (! current.recognised)
            continue;

        if (chords.empty()
            || chords.back().rootPitchClass != current.rootPitchClass
            || chords.back().quality != current.quality)
            chords.push_back ({ frame.seconds, current.rootPitchClass, current.quality });
    }

    if (keyOut != nullptr)
        *keyOut = listener.key();

    return chords;
}

} // namespace keyla::core
