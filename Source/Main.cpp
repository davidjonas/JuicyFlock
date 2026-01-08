#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
class JuicyFlockApplication : public juce::JUCEApplication
{
public:
    //==============================================================================
    JuicyFlockApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    //==============================================================================
    void initialise(const juce::String& commandLine) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
    }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(juce::ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
           #else
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());

            // Start maximised on Windows. JUCE's native "fullscreen" on Windows maps to SW_SHOWMAXIMIZED.
           #if JUCE_WINDOWS
            setFullScreen (true);
           #else
            // Fallback: size to the primary display work area.
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                setBounds (display->userArea.reduced (8));
           #endif
           #endif

            setVisible(true);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
            {
                JUCEApplication::getInstance()->systemRequestedQuit();
                return true;
            }

            return DocumentWindow::keyPressed (key);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION(JuicyFlockApplication)

