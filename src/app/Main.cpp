#include "ui/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace keyla::app
{

class KeylaApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Keyla"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : juce::DocumentWindow ("Keyla",
                                    juce::Colour { 0xff1d1f24 },
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (720, 340, 4000, 1400);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace keyla::app

START_JUCE_APPLICATION (keyla::app::KeylaApplication)
