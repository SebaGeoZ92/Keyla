#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace keyla::app::theme
{

/** El aspecto de Keyla en un solo sitio.

    Hasta ahora cada componente escribía sus colores a mano, y la ventana usaba
    el gris azulado por defecto de JUCE para desplegables y botones. Con los
    colores repartidos por diez ficheros no había forma de que la ventana se
    viera como una sola cosa.

    Una idea manda sobre el resto: **el verde es de Keyla y significa "esto es
    música"** —el acorde que suena, las teclas que tocar, el botón de empezar—.
    No se usa para decorar, porque en cuanto decora deja de señalar.
*/

inline const juce::Colour background    { 0xff1d1f24 };
inline const juce::Colour surface       { 0xff262930 };
inline const juce::Colour surfaceRaised { 0xff2e323a };
inline const juce::Colour border        { 0xff363a43 };

inline const juce::Colour text          { 0xffe8eaed };
inline const juce::Colour textSecondary { 0xff9aa2ad };
inline const juce::Colour textDim       { 0xff5f6670 };

inline const juce::Colour accent        { 0xff7fb069 };
inline const juce::Colour accentDeep    { 0xff4f7a3e };
inline const juce::Colour progress      { 0xff4f9dd9 };
inline const juce::Colour warning       { 0xffe0b062 };
inline const juce::Colour error         { 0xffe4785e };

inline constexpr int cardHeaderHeight = 22;
inline constexpr int cardRadius = 10;

/** Pinta una tarjeta con su título y devuelve dónde va el contenido. */
inline juce::Rectangle<int> paintCard (juce::Graphics& g, juce::Rectangle<int> bounds,
                                       const juce::String& title)
{
    const auto shape = bounds.toFloat().reduced (0.5f);

    g.setColour (surface);
    g.fillRoundedRectangle (shape, static_cast<float> (cardRadius));

    g.setColour (border);
    g.drawRoundedRectangle (shape, static_cast<float> (cardRadius), 1.0f);

    auto content = bounds.reduced (16, 10);
    auto header = content.removeFromTop (cardHeaderHeight);

    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (title, header, juce::Justification::centredLeft, false);

    content.removeFromTop (2);
    return content;
}

/** Colores de los controles de JUCE. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, background);

        setColour (juce::ComboBox::backgroundColourId, surfaceRaised);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textSecondary);
        setColour (juce::ComboBox::focusedOutlineColourId, accent);

        setColour (juce::PopupMenu::backgroundColourId, surfaceRaised);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDeep);
        setColour (juce::PopupMenu::highlightedTextColourId, text);

        setColour (juce::TextButton::buttonColourId, surfaceRaised);
        setColour (juce::TextButton::buttonOnColourId, accentDeep);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, text);

        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);

        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::trackColourId, accentDeep);
        setColour (juce::Slider::backgroundColourId, border);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, surfaceRaised);
        setColour (juce::Slider::textBoxOutlineColourId, border);

        setColour (juce::Label::textColourId, textSecondary);
    }

    /** Botones con esquinas algo más redondas que las de JUCE, a juego con las
        tarjetas. */
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        auto shape = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = backgroundColour;

        if (! button.isEnabled())
            colour = colour.withMultipliedAlpha (0.5f);
        else if (down)
            colour = colour.brighter (0.15f);
        else if (highlighted)
            colour = colour.brighter (0.08f);

        g.setColour (colour);
        g.fillRoundedRectangle (shape, 6.0f);

        g.setColour (border);
        g.drawRoundedRectangle (shape, 6.0f, 1.0f);
    }
};

/** El botón que hace lo principal de su zona: en verde. Uno por zona como
    mucho, o deja de señalar. */
inline void makePrimary (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, accentDeep);
    button.setColour (juce::TextButton::textColourOffId, text);
}

} // namespace keyla::app::theme
