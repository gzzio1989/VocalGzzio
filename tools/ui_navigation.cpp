#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestPaths.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

// Drive queued button clicks through the real editor and message loop. Content-only
// tests miss preferences, asynchronous callouts and the first paint after switching.
class NavigationCheck final : private juce::Timer
{
public:
    NavigationCheck()
    {
        auto settings = gz::dataDirectory().getChildFile ("VocalGzzio.settings");
        settings.getParentDirectory().createDirectory();
        settings.replaceWithText ("<PROPERTIES><VALUE name=\"ui_layout_revision\" val=\"2\"/>"
                                  "<VALUE name=\"ui_overview\" val=\"1\"/>"
                                  "<VALUE name=\"ui_theme\" val=\"0\"/></PROPERTIES>");
        processor = std::make_unique<VocalGzzioProcessor>();
        processor->setPlayConfigDetails (2, 2, 48000.0, 256);
        processor->prepareToPlay (48000.0, 256);
        editor.reset (processor->createEditor());
        editor->setTopLeftPosition (-20000, -20000);
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        editor->setVisible (true);
        for (auto* c : editor->getChildren())
            if (auto* v = dynamic_cast<VocalGzzioContent*> (c)) view = v;
        if (view) { view->getTuner().setTuningMode (3); view->getTuner().setGuitarString (1); }
        image = juce::Image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true,
                             juce::SoftwareImageType());
        worker = std::thread ([this]
        {
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            double phase = 0.0;
            while (! finished.load())
            {
                for (int n = 0; n < 256; ++n)
                {
                    const float sample = 0.1f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 82.4069 / 48000.0;
                    buffer.setSample (0, n, sample); buffer.setSample (1, n, sample);
                }
                processor->processBlock (buffer, midi);
                ++audioBlocks;
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        });
        startTimer (150);
    }

    ~NavigationCheck() override
    {
        stopTimer(); finished.store (true); worker.join();
        editor.reset(); processor->releaseResources();
    }
    int failures = 0;

private:
    void check (bool ok, const char* reason)
    {
        if (! ok) { ++failures; std::printf ("FAIL: %s\n", reason); std::fflush (stdout); }
    }
    void click (const char* id)
    {
        std::printf ("Click %s (round %d)\n", id, round); std::fflush (stdout);
        auto* b = dynamic_cast<juce::Button*> (view->findChildWithID (id));
        check (b && b->isShowing() && b->isEnabled(), "navigation is reachable");
        if (b) b->triggerClick();
        clicked = std::chrono::steady_clock::now();
    }
    void paintAndCheck (bool overview)
    {
        check (view->isOverview() == overview, "requested screen opened");
        juce::Graphics g (image);
        editor->paintEntireComponent (g, false);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - clicked).count();
        std::printf ("Ready and painted in %.1f ms\n", ms); std::fflush (stdout);
        check (ms < 1500.0, "navigation and first paint remain responsive");
        check (audioBlocks.load() > previousAudioBlocks, "audio continues during navigation");
        previousAudioBlocks = audioBlocks.load();
    }
    void timerCallback() override
    {
        if (! view) { check (false, "editor content exists"); finish(); return; }
        switch (step++)
        {
            case 0: view->setThemeMode (round == 1 ? 3 : 0); click ("openDetails"); break;
            case 1: paintAndCheck (false); click ("openOverview"); break;
            case 2: paintAndCheck (true); click ("openPitch"); break;
            case 3: paintAndCheck (false); click ("openOverview"); break;
            case 4: paintAndCheck (true); click ("openTuner"); break;
            case 5:
            {
                paintAndCheck (true);
                int count = 0;
                for (auto* c : view->getChildren())
                    if (auto* box = dynamic_cast<juce::CallOutBox*> (c))
                    { ++count; popup = box; }
                check (count == 1, "one large tuner opened");
                if (popup != nullptr)
                    for (auto* c : popup->getChildren())
                        if (auto* tuner = dynamic_cast<VocalTuner*> (c))
                        {
                            check (tuner->getTuningMode() == (round == 0 ? 3 : 2)
                                   && tuner->getTuningString() == (round == 0 ? 1 : 3),
                                   "large tuner preserves selected bass and string when reopened");
                            tuner->setTuningMode (2); tuner->setGuitarString (3);
                        }
                // Theme/source/advanced-mode changes can rebuild the overview
                // while a temporary child is open. Its lifetime is independent.
                view->setThemeMode (round == 1 ? 0 : 3);
                view->setOverview (true);
                check (popup != nullptr && popup->isShowing(), "overview refresh preserves its open popup");
                // A background CI window is legitimately dismissed by JUCE's
                // foreground-process check after 200 ms. Close it ourselves
                // after the regression assertion; do not test OS focus policy.
                if (popup != nullptr) popup->dismiss();
                break;
            }
            case 6:
                check (view->getTuner().getTuningMode() == 2 && view->getTuner().getTuningString() == 3,
                       "overview follows the large tuner's choice");
                break;
            case 7:
                check (juce::Component::getNumCurrentlyModalComponents() == 0, "tuner closes without trapping input");
                check (popup == nullptr, "closed popup was released");
                // A cached raw pointer to the destroyed popup used to be touched
                // by this next navigation action.
                view->setOverview (false);
                view->setOverview (true);
                if (++round < 3) step = 0; else finish();
                break;
        }
    }
    void finish() { stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop(); }
    std::unique_ptr<VocalGzzioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    VocalGzzioContent* view = nullptr;
    juce::Component::SafePointer<juce::CallOutBox> popup;
    juce::Image image;
    std::thread worker;
    std::atomic<bool> finished { false };
    std::atomic<int> audioBlocks { 0 };
    int previousAudioBlocks = 0, step = 0, round = 0;
    std::chrono::steady_clock::time_point clicked;
};

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    NavigationCheck test;
    juce::MessageManager::getInstance()->runDispatchLoop();
    std::printf ("Navigation failures: %d\n", test.failures);
    return test.failures == 0 ? 0 : 1;
}
