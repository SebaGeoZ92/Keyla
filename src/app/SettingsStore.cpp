#include "SettingsStore.h"

namespace keyla::app
{

juce::File Settings::file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Keyla")
               .getChildFile ("settings.json");
}

Settings Settings::load()
{
    Settings settings;

    const auto source = file();

    if (! source.existsAsFile())
        return settings;

    const auto parsed = juce::JSON::parse (source.loadFileAsString());

    if (! parsed.isObject())
        return settings;         // fichero corrupto: se ignora, no se rompe nada

    const auto get = [&parsed] (const char* key) { return parsed.getProperty (key, {}); };

    settings.audioOutputName = get ("audioOutput").toString();
    settings.midiInputName = get ("midiInput").toString();
    settings.listenDeviceName = get ("listenDevice").toString();

    if (const auto value = get ("bufferSize"); ! value.isVoid())
        settings.bufferSize = juce::jlimit (16, 8192, static_cast<int> (value));

    if (const auto value = get ("exclusive"); ! value.isVoid())
        settings.exclusive = static_cast<bool> (value);

    if (const auto value = get ("instrument"); ! value.isVoid())
    {
        // Se pregunta al catálogo en vez de comparar contra el último valor del
        // enum: escrito a mano, esto se queda obsoleto el día que se añade un
        // instrumento y nadie se entera hasta que el ajuste deja de cargarse.
        if (const auto id = static_cast<int> (value); core::isValidInstrumentId (id))
            settings.instrument = static_cast<core::InstrumentId> (id);
    }

    if (const auto value = get ("masterVolume"); ! value.isVoid())
        settings.masterVolume = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (value)));

    if (const auto value = get ("reverbMix"); ! value.isVoid())
        settings.reverbMix = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (value)));

    if (const auto value = get ("volumeController"); ! value.isVoid())
        settings.volumeController = juce::jlimit (0, 127, static_cast<int> (value));

    if (const auto value = get ("tremoloController"); ! value.isVoid())
        settings.tremoloController = juce::jlimit (0, 127, static_cast<int> (value));

    if (const auto value = get ("reverbController"); ! value.isVoid())
        settings.reverbController = juce::jlimit (0, 127, static_cast<int> (value));

    if (const auto value = get ("tremoloDepth"); ! value.isVoid())
        settings.tremoloDepth = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (value)));

    if (const auto value = get ("tempoBpm"); ! value.isVoid())
        settings.tempoBpm = juce::jlimit (20.0, 300.0, static_cast<double> (value));

    if (const auto value = get ("perceptualOffsetMs"); ! value.isVoid())
        settings.perceptualOffsetMs = juce::jlimit (-100.0, 100.0, static_cast<double> (value));

    return settings;
}

void Settings::save() const
{
    auto* root = new juce::DynamicObject();

    root->setProperty ("audioOutput", audioOutputName);
    root->setProperty ("bufferSize", bufferSize);
    root->setProperty ("exclusive", exclusive);
    root->setProperty ("midiInput", midiInputName);
    root->setProperty ("listenDevice", listenDeviceName);
    root->setProperty ("instrument", static_cast<int> (instrument));
    root->setProperty ("masterVolume", masterVolume);
    root->setProperty ("reverbMix", reverbMix);
    root->setProperty ("volumeController", volumeController);
    root->setProperty ("tremoloController", tremoloController);
    root->setProperty ("reverbController", reverbController);
    root->setProperty ("tremoloDepth", tremoloDepth);
    root->setProperty ("tempoBpm", tempoBpm);
    root->setProperty ("perceptualOffsetMs", perceptualOffsetMs);

    const auto target = file();
    target.getParentDirectory().createDirectory();
    target.replaceWithText (juce::JSON::toString (juce::var (root)));
}

} // namespace keyla::app
