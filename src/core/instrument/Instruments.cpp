#include "Instruments.h"
#include "StruckStringSynth.h"

#include "../text/Utf8.h"

namespace keyla::core
{

using keyla::operator""_u8;

const std::vector<InstrumentId>& allInstrumentIds()
{
    // Orden de presentación, no de persistencia: ver el comentario del enum.
    static const std::vector<InstrumentId> catalogue {
        InstrumentId::piano,      InstrumentId::electricPiano, InstrumentId::harpsichord,
        InstrumentId::organ,      InstrumentId::accordion,     InstrumentId::guitar,
        InstrumentId::strings,    InstrumentId::choir,         InstrumentId::vibraphone,
        InstrumentId::marimba,    InstrumentId::flute
    };

    return catalogue;
}

bool isValidInstrumentId (int value)
{
    for (auto id : allInstrumentIds())
        if (static_cast<int> (id) == value)
            return true;

    return false;
}

juce::String instrumentName (InstrumentId id)
{
    switch (id)
    {
        case InstrumentId::piano:          return "Piano";
        case InstrumentId::electricPiano:  return "Piano eléctrico"_u8;
        case InstrumentId::organ:          return "Órgano"_u8;
        case InstrumentId::accordion:      return "Acordeón"_u8;
        case InstrumentId::strings:        return "Cuerdas";
        case InstrumentId::vibraphone:     return "Vibráfono"_u8;
        case InstrumentId::guitar:         return "Guitarra";
        case InstrumentId::harpsichord:    return "Clavecín"_u8;
        case InstrumentId::marimba:        return "Marimba";
        case InstrumentId::flute:          return "Flauta";
        case InstrumentId::choir:          return "Coro";
    }

    return "?";
}

std::unique_ptr<IInstrument> createInstrument (InstrumentId id)
{
    switch (id)
    {
        case InstrumentId::piano:          return std::make_unique<StruckStringSynth> (32);
        case InstrumentId::electricPiano:  return std::make_unique<ElectricPiano>();
        case InstrumentId::organ:          return std::make_unique<Organ>();
        case InstrumentId::accordion:      return std::make_unique<Accordion>();
        case InstrumentId::strings:        return std::make_unique<Strings>();
        case InstrumentId::vibraphone:     return std::make_unique<Vibraphone>();
        case InstrumentId::guitar:         return std::make_unique<Guitar>();
        case InstrumentId::harpsichord:    return std::make_unique<Harpsichord>();
        case InstrumentId::marimba:        return std::make_unique<Marimba>();
        case InstrumentId::flute:          return std::make_unique<Flute>();
        case InstrumentId::choir:          return std::make_unique<Choir>();
    }

    return std::make_unique<StruckStringSynth> (32);
}

float defaultReverbFor (InstrumentId id)
{
    switch (id)
    {
        case InstrumentId::piano:          return 0.12f;
        case InstrumentId::electricPiano:  return 0.20f;
        case InstrumentId::organ:          return 0.25f;
        case InstrumentId::accordion:      return 0.15f;

        // Una sección de cuerdas sin sala suena a juguete: la reverberación no
        // es un adorno aquí, es parte del instrumento.
        case InstrumentId::strings:        return 0.55f;
        case InstrumentId::vibraphone:     return 0.35f;

        case InstrumentId::guitar:         return 0.18f;
        case InstrumentId::harpsichord:    return 0.22f;
        case InstrumentId::marimba:        return 0.20f;

        // Una flauta seca suena a silbato de plástico, y un coro sin iglesia
        // detrás no es un coro. En los dos casos la sala es el instrumento.
        case InstrumentId::flute:          return 0.45f;
        case InstrumentId::choir:          return 0.60f;
    }

    return 0.15f;
}

} // namespace keyla::core
