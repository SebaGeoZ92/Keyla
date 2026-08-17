#include "Instruments.h"
#include "StruckStringSynth.h"

#include "../text/Utf8.h"

namespace keyla::core
{

using keyla::operator""_u8;

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
    }

    return 0.15f;
}

} // namespace keyla::core
