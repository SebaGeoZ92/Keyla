// Keyla escuchando. La pregunta que estos tests contestan es una sola: ¿puede
// sacar el cifrado de un audio, sin que nadie le diga qué notas hay?
//
// El material de prueba sale gratis: se le da a escuchar **lo que ella misma
// toca**. Es un banco de pruebas honesto porque los instrumentos de Keyla tienen
// armónicos de verdad —el piano llega a 24 parciales— y son justo los armónicos
// los que hacen difícil este problema. Un test con senoides puras diría que todo
// funciona y no probaría nada.

#include <core/instrument/Instruments.h>
#include <core/listen/Chromagram.h>
#include <core/listen/Accompaniment.h>
#include <core/listen/HarmonyFromAudio.h>
#include <core/listen/ListeningEvaluation.h>
#include <core/music/Pitch.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace keyla::core;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    RawMidiMessage noteOn (int pitch, int velocity)
    {
        RawMidiMessage m;
        m.bytes[0] = 0x90;
        m.bytes[1] = static_cast<std::uint8_t> (pitch);
        m.bytes[2] = static_cast<std::uint8_t> (velocity);
        m.size = 3;
        return m;
    }

    /** Toca las notas a la vez y devuelve el audio en mono. */
    std::vector<float> renderMono (IInstrument& instrument, const std::vector<int>& pitches,
                                   double seconds, int velocity = 100)
    {
        instrument.prepare (sampleRate, blockSize);

        std::vector<StampedMidiEvent> events;

        for (auto pitch : pitches)
        {
            StampedMidiEvent event;
            event.message = noteOn (pitch, velocity);
            event.renderOffset = 0;
            events.push_back (event);
        }

        juce::AudioBuffer<float> buffer (2, blockSize);
        std::vector<float> mono;

        const auto totalBlocks = static_cast<int> (seconds * sampleRate / blockSize);
        mono.reserve (static_cast<std::size_t> (totalBlocks * blockSize));

        for (int block = 0; block < totalBlocks; ++block)
        {
            buffer.clear();

            if (block == 0)
                instrument.process (buffer, MidiEventSpan { events.data(), events.size() });
            else
                instrument.process (buffer, MidiEventSpan { nullptr, 0 });

            const auto* data = buffer.getReadPointer (0);

            for (int i = 0; i < blockSize; ++i)
                mono.push_back (data[i]);
        }

        return mono;
    }

    /** Notas de un acorde en estado fundamental alrededor del Do central. */
    std::vector<int> chordNotes (int rootPitchClass, ChordQuality quality, int octaveBase = 48)
    {
        std::vector<int> notes;

        for (auto interval : ChordRecognizer::intervalsFor (quality))
            notes.push_back (octaveBase + rootPitchClass + interval);

        return notes;
    }

    /** Le da a escuchar el audio entero y devuelve el último fotograma. */
    struct Heard
    {
        Chroma chroma;
        std::vector<double> profile;
        bool valid { false };
    };

    Heard listen (ChromaAnalyser& analyser, const std::vector<float>& audio)
    {
        analyser.push (audio.data(), static_cast<int> (audio.size()));

        Heard heard;
        Chroma frame;

        while (analyser.popFrame (frame))
        {
            heard.chroma = frame;
            heard.profile = analyser.pitchProfile();
            heard.valid = true;
        }

        return heard;
    }
}

TEST_CASE ("Keyla saca un acorde de su propio piano", "[listen]")
{
    ChromaAnalyser analyser;
    analyser.prepare (sampleRate);

    auto piano = createInstrument (InstrumentId::piano);
    const auto audio = renderMono (*piano, { 48, 52, 55 }, 1.2);      // Do mayor

    const auto heard = listen (analyser, audio);
    REQUIRE (heard.valid);

    const auto estimate = HarmonyListener::estimate (heard.chroma, 0, HarmonyListener::Options {});

    INFO ("oido: " << estimate.symbol.toStdString()
          << "  confianza " << estimate.confidence
          << "  margen " << estimate.margin);

    CHECK (estimate.recognised);
    CHECK (estimate.rootPitchClass == 0);
    CHECK (estimate.quality == ChordQuality::major);
}

TEST_CASE ("El silencio no produce acordes", "[listen]")
{
    // Un cromagrama de silencio, normalizado, se parece a cualquier cosa. Es la
    // forma más fácil que hay de que Keyla se invente acordes en los huecos de
    // una cancion, y por eso la energia se mira antes de normalizar.
    ChromaAnalyser analyser;
    analyser.prepare (sampleRate);

    const std::vector<float> silence (static_cast<std::size_t> (sampleRate), 0.0f);
    const auto heard = listen (analyser, silence);

    REQUIRE (heard.valid);
    CHECK (heard.chroma.isSilent());

    const auto estimate = HarmonyListener::estimate (heard.chroma, -1, HarmonyListener::Options {});
    CHECK_FALSE (estimate.recognised);
}

TEST_CASE ("Acierto sobre las doce tonicas y varias calidades", "[listen][accuracy]")
{
    // La cifra que de verdad importa. Se imprime porque es una medida, no un
    // aprobado: si algún día baja, conviene ver cuánto.
    ChromaAnalyser analyser;
    analyser.prepare (sampleRate);

    const std::vector<ChordQuality> qualities {
        ChordQuality::major, ChordQuality::minor,
        ChordQuality::dominant7, ChordQuality::minor7
    };

    int total = 0;
    int rootHits = 0;
    int fullHits = 0;

    for (auto quality : qualities)
    {
        for (int root = 0; root < 12; ++root)
        {
            auto piano = createInstrument (InstrumentId::piano);
            const auto audio = renderMono (*piano, chordNotes (root, quality), 1.0);

            const auto heard = listen (analyser, audio);
            REQUIRE (heard.valid);

            const auto estimate = HarmonyListener::estimate (heard.chroma,
                                                             heard.chroma.isSilent() ? -1 : root,
                                                             HarmonyListener::Options {});

            ++total;

            if (estimate.rootPitchClass == root)
            {
                ++rootHits;

                if (estimate.quality == quality)
                    ++fullHits;
            }

            if (estimate.rootPitchClass != root || estimate.quality != quality)
                std::cout << "  [oido] " << pitchClassName (root).toStdString()
                          << ChordRecognizer::qualitySymbol (quality).toStdString()
                          << " -> " << estimate.symbol.toStdString()
                          << "  (conf " << estimate.confidence
                          << " margen " << estimate.margin << ")\n";
        }
    }

    std::cout << "  [escucha] fundamental " << (100.0 * rootHits / total) << " %"
              << "   fundamental+calidad " << (100.0 * fullHits / total) << " %"
              << "   sobre " << total << " acordes\n";

    CHECK (rootHits * 100 >= total * 95);
    CHECK (fullHits * 100 >= total * 90);
}

TEST_CASE ("Funciona con instrumentos de timbre muy distinto", "[listen]")
{
    // El piano tiene 24 parciales; la flauta tiene cuatro y el órgano mete una
    // quinta grave en el registro. Si el reconocimiento sólo funcionara con el
    // piano, no serviría para escuchar una cancion de verdad.
    for (auto id : { InstrumentId::organ, InstrumentId::strings,
                     InstrumentId::guitar, InstrumentId::flute })
    {
        ChromaAnalyser analyser;
        analyser.prepare (sampleRate);

        auto instrument = createInstrument (id);
        const auto audio = renderMono (*instrument, { 53, 57, 60, 65 }, 1.2);   // Fa mayor

        const auto heard = listen (analyser, audio);
        REQUIRE (heard.valid);

        const auto estimate = HarmonyListener::estimate (heard.chroma, 5, HarmonyListener::Options {});

        INFO (instrumentName (id).toStdString() << " -> " << estimate.symbol.toStdString()
              << " (conf " << estimate.confidence << ")");

        CHECK (estimate.rootPitchClass == 5);
    }
}

TEST_CASE ("Sigue una progresion y deduce la tonalidad", "[listen]")
{
    // Do - Sol - La menor - Fa: la progresión de media radio. Se le da a oír
    // seguida, como llegaría de una canción, y se le pregunta por dónde ha
    // pasado y en qué tonalidad está.
    ChromaAnalyser analyser;
    analyser.prepare (sampleRate);

    HarmonyListener listener;

    const std::vector<std::pair<int, ChordQuality>> progression {
        { 0, ChordQuality::major },     // Do
        { 7, ChordQuality::major },     // Sol
        { 9, ChordQuality::minor },     // La menor
        { 5, ChordQuality::major }      // Fa
    };

    for (const auto& [root, quality] : progression)
    {
        auto piano = createInstrument (InstrumentId::piano);
        const auto audio = renderMono (*piano, chordNotes (root, quality), 1.4);

        analyser.push (audio.data(), static_cast<int> (audio.size()));

        Chroma frame;

        while (analyser.popFrame (frame))
            listener.observe (frame, analyser.pitchProfile(), analyser.lowestPitch());
    }

    const auto heard = listener.progression();

    std::cout << "  [progresion] ";

    for (const auto& chord : heard)
        std::cout << chord.symbol.toStdString() << ' ';

    const auto key = listener.key();
    std::cout << "  tonalidad: " << key.name.toStdString()
              << " (" << key.confidence << ")\n";

    // No se exige la secuencia exacta —un acorde puede colarse en la transición
    // entre dos— sino que los cuatro de verdad estén y en orden.
    std::vector<int> roots;

    for (const auto& chord : heard)
        roots.push_back (chord.rootPitchClass);

    std::size_t matched = 0;

    for (auto root : roots)
        if (matched < progression.size() && root == progression[matched].first)
            ++matched;

    CHECK (matched == progression.size());

    CHECK (key.recognised);
    CHECK (key.tonicPitchClass == 0);
    CHECK_FALSE (key.minor);
}

TEST_CASE ("La ventana de analisis se declara", "[listen]")
{
    // Un acorde detectado no es "ahora": es "hace una ventana". Si esa cifra no
    // se puede consultar, la interfaz acabará enseñando el acorde como si fuera
    // del instante y no lo es.
    ChromaAnalyser analyser;
    analyser.prepare (sampleRate);

    CHECK (analyser.windowSeconds() > 0.3);
    CHECK (analyser.windowSeconds() < 1.2);
}

TEST_CASE ("El acompanante toca el acorde que se le pide", "[listen][accompaniment]")
{
    // Colocar no es reharmonizar: salga donde salga en el teclado, las clases
    // de altura tienen que ser exactamente las del acorde pedido.
    AccompanimentCoach coach;

    for (int root = 0; root < 12; ++root)
    {
        for (auto quality : { ChordQuality::major, ChordQuality::minor, ChordQuality::dominant7 })
        {
            const auto suggestion = coach.suggest (root, quality);

            INFO ("acorde " << pitchClassName (root).toStdString()
                  << ChordRecognizer::qualitySymbol (quality).toStdString());

            REQUIRE (suggestion.valid);

            std::vector<int> wanted;

            for (auto interval : ChordRecognizer::intervalsFor (quality))
                wanted.push_back ((root + interval) % 12);

            std::sort (wanted.begin(), wanted.end());

            std::vector<int> got;

            for (auto pitch : suggestion.rightHand)
                got.push_back (pitchClassOf (pitch));

            std::sort (got.begin(), got.end());
            got.erase (std::unique (got.begin(), got.end()), got.end());

            CHECK (got == wanted);
        }
    }
}

TEST_CASE ("El acompanante mueve poco la mano entre acordes", "[listen][accompaniment]")
{
    // La propiedad que justifica que exista. Si la mano saltara como en estado
    // fundamental, esto seria una lista de acordes y no un acompanamiento.
    const std::vector<std::pair<int, ChordQuality>> song {
        { 0, ChordQuality::major },     // Do
        { 7, ChordQuality::major },     // Sol
        { 9, ChordQuality::minor },     // Lam
        { 5, ChordQuality::major },     // Fa
        { 0, ChordQuality::major }
    };

    AccompanimentCoach::Options options;
    options.withLeftHandBass = false;

    AccompanimentCoach coach { options };

    int linkedTravel = 0;
    int rootTravel = 0;
    std::vector<int> previous;

    for (const auto& [root, quality] : song)
    {
        const auto suggestion = coach.suggest (root, quality);
        REQUIRE (suggestion.valid);

        if (! previous.empty())
            for (std::size_t i = 0; i < previous.size() && i < suggestion.rightHand.size(); ++i)
                linkedTravel += std::abs (suggestion.rightHand[i] - previous[i]);

        previous = suggestion.rightHand;
    }

    // Lo mismo en estado fundamental, para comparar contra algo real y no
    // contra un numero inventado.
    std::vector<int> previousRoot;

    for (const auto& [root, quality] : song)
    {
        std::vector<int> voicing;

        for (auto interval : ChordRecognizer::intervalsFor (quality))
            voicing.push_back (60 + root + interval);

        if (! previousRoot.empty())
            for (std::size_t i = 0; i < previousRoot.size() && i < voicing.size(); ++i)
                rootTravel += std::abs (voicing[i] - previousRoot[i]);

        previousRoot = voicing;
    }

    std::cout << "  [acompanar] recorrido de la mano: enlazado " << linkedTravel
              << " semitonos, fundamental " << rootTravel << '\n';

    CHECK (linkedTravel * 2 < rootTravel);
}

TEST_CASE ("El bajo no se amontona con la derecha", "[listen][accompaniment]")
{
    // Dos manos en el mismo sitio del grave es exactamente como se consigue que
    // un acorde suene a barro. Es el mismo error que ya costo el bug de los
    // graves del piano, y aqui se evita por construccion.
    AccompanimentCoach coach;

    for (int root = 0; root < 12; ++root)
    {
        const auto suggestion = coach.suggest (root, ChordQuality::major);
        REQUIRE (suggestion.valid);
        REQUIRE (suggestion.bassNote >= 0);

        const int lowestRight = *std::min_element (suggestion.rightHand.begin(),
                                                   suggestion.rightHand.end());

        INFO ("bajo " << suggestion.bassNote << " derecha desde " << lowestRight);

        CHECK (suggestion.bassNote <= lowestRight - 7);      // al menos una quinta
        CHECK (pitchClassOf (suggestion.bassNote) == root);  // y es la fundamental
        CHECK (suggestion.bassNote >= 21);
    }
}

TEST_CASE ("Olvidar la mano corta el enlace entre canciones", "[listen][accompaniment]")
{
    AccompanimentCoach coach;

    const auto first = coach.suggest (11, ChordQuality::major);   // Si mayor, arriba
    REQUIRE (first.valid);

    coach.reset();

    const auto afterReset = coach.suggest (0, ChordQuality::major);
    REQUIRE (afterReset.valid);

    // Sin memoria, un Do mayor se coloca centrado, no pegado a donde quedo el
    // Si de la cancion anterior.
    const int centre = 60;
    const int distance = std::abs (afterReset.rightHand.front() - centre);

    INFO ("primera nota tras olvidar: " << afterReset.rightHand.front());
    CHECK (distance <= 12);
}

namespace
{
    /** Do-Sol-Lam-Fa, un acorde cada dos segundos, a partir de `offset`. */
    std::vector<TimedChord> popLoop (double offset, int repeats = 3)
    {
        const std::vector<std::pair<int, ChordQuality>> loop {
            { 0, ChordQuality::major }, { 7, ChordQuality::major },
            { 9, ChordQuality::minor }, { 5, ChordQuality::major }
        };

        std::vector<TimedChord> timeline;
        double t = offset;

        for (int r = 0; r < repeats; ++r)
            for (const auto& [root, quality] : loop)
            {
                timeline.push_back ({ t, root, quality });
                t += 2.0;
            }

        return timeline;
    }
}

TEST_CASE ("Comparar lo oido con lo tocado: acuerdo perfecto", "[listen][evaluation]")
{
    const auto song = popLoop (0.0);
    const auto result = evaluateListening (song, song, 24.0);

    REQUIRE (result.valid);
    CHECK (result.rootAgreement > 0.99);
    CHECK (result.fullAgreement > 0.99);
    CHECK (std::abs (result.lagSeconds) < 0.01);
    CHECK (result.confusions.empty());
}

TEST_CASE ("Encuentra el retraso de las manos", "[listen][evaluation]")
{
    // Tocas lo mismo pero medio segundo tarde. Si no se corrigiera el desfase,
    // cada cambio de acorde contaria como error durante medio segundo aunque
    // los dos hubierais acertado, y el informe culparia al reconocedor.
    const auto heard = popLoop (0.0);
    const auto played = popLoop (0.5);

    const auto result = evaluateListening (heard, played, 25.0);

    REQUIRE (result.valid);
    CHECK (std::abs (result.lagSeconds - 0.5) < 0.06);
    CHECK (result.rootAgreement > 0.97);
}

TEST_CASE ("Cuenta las confusiones por tiempo", "[listen][evaluation]")
{
    // Keyla oye Do donde tocaste Lam — la confusion mas natural del mundo,
    // porque comparten dos de sus tres notas.
    auto heard = popLoop (0.0);
    const auto played = popLoop (0.0);

    for (auto& chord : heard)
        if (chord.rootPitchClass == 9)
        {
            chord.rootPitchClass = 0;
            chord.quality = ChordQuality::major;
        }

    const auto result = evaluateListening (heard, played, 24.0, 0.0);

    REQUIRE (result.valid);

    // Un acorde de cada cuatro mal: 75 %.
    CHECK (std::abs (result.rootAgreement - 0.75) < 0.03);

    REQUIRE (! result.confusions.empty());

    const auto& worst = result.confusions.front();
    CHECK (worst.playedRoot == 9);
    CHECK (worst.playedQuality == ChordQuality::minor);
    CHECK (worst.heardRoot == 0);
    CHECK (worst.heardQuality == ChordQuality::major);
    CHECK (std::abs (worst.seconds - 6.0) < 0.2);
}

TEST_CASE ("Los acordes tocados salen de las teclas", "[listen][evaluation]")
{
    std::vector<TimedNote> notes;

    const auto press = [&notes] (double t, std::vector<int> pitches, double length)
    {
        for (auto p : pitches)
            notes.push_back ({ t, p, true });

        for (auto p : pitches)
            notes.push_back ({ t + length, p, false });
    };

    press (0.0, { 60, 64, 67 }, 1.0);      // Do
    press (1.0, { 55, 59, 62 }, 1.0);      // Sol

    const auto chords = chordsFromNotes (notes);

    REQUIRE (chords.size() == 2);
    CHECK (chords[0].rootPitchClass == 0);
    CHECK (chords[1].rootPitchClass == 7);
    CHECK (std::abs (chords[1].seconds - 1.0) < 0.01);
}

TEST_CASE ("El legato no fabrica acordes que no tocaste", "[listen][evaluation]")
{
    // Pasas de Do a Lam sin levantar del todo la mano: durante unos
    // milisegundos tienes Do-Mi-Sol-La bajo los dedos, que es un Do6 —o un
    // Lam7, segun el bajo—. Ese acorde no lo tocaste y no debe aparecer.
    std::vector<TimedNote> notes {
        { 0.00, 60, true }, { 0.00, 64, true }, { 0.00, 67, true },
        { 1.00, 69, true },                                          // entra el La...
        { 1.02, 67, false },                                         // ...y sale el Sol
        { 2.00, 60, false }, { 2.00, 64, false }, { 2.00, 69, false }
    };

    const auto chords = chordsFromNotes (notes);

    REQUIRE (chords.size() == 2);
    CHECK (chords[0].rootPitchClass == 0);
    CHECK (chords[0].quality == ChordQuality::major);
    CHECK (chords[1].rootPitchClass == 9);
    CHECK (chords[1].quality == ChordQuality::minor);
}
