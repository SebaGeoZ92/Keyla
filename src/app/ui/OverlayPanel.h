#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace keyla::app
{

/** Un panel que se abre encima de la ventana, con el fondo oscurecido.

    Existe para los ajustes: salida de audio, buffer, puerto MIDI, arranque con
    Windows. Se tocan una vez y nunca más, y estaban siempre a la vista
    ocupando las tres primeras filas de la ventana, justo donde van los ojos
    mientras se toca.

    No sabe nada de qué contiene. Quien lo usa le mete los controles como hijos
    y le dice dónde van con `onLayout`; lo que no son controles —títulos de
    sección, textos— lo pinta con `onPaintCard`.
*/
class OverlayPanel final : public juce::Component
{
public:
    OverlayPanel()
    {
        setInterceptsMouseClicks (true, true);
    }

    std::function<void (juce::Rectangle<int> card)> onLayout;
    std::function<void (juce::Graphics&, juce::Rectangle<int> card)> onPaintCard;

    /** Al pulsar fuera de la tarjeta. */
    std::function<void()> onDismiss;

    int cardWidth { 760 };
    int cardHeight { 480 };

    juce::Rectangle<int> cardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (cardWidth, getWidth() - 40),
                                                       juce::jmin (cardHeight, getHeight() - 40));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));

        const auto card = cardBounds();
        const auto shape = card.toFloat();

        g.setColour (theme::surface);
        g.fillRoundedRectangle (shape, static_cast<float> (theme::cardRadius));

        g.setColour (theme::border);
        g.drawRoundedRectangle (shape.reduced (0.5f), static_cast<float> (theme::cardRadius), 1.0f);

        if (onPaintCard != nullptr)
            onPaintCard (g, card);
    }

    void resized() override
    {
        if (onLayout != nullptr)
            onLayout (cardBounds());
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! cardBounds().contains (event.getPosition()) && onDismiss != nullptr)
            onDismiss();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverlayPanel)
};

} // namespace keyla::app
