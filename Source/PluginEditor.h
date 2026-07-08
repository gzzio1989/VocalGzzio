#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "PluginProcessor.h"

//==============================================================================
// Light palette matched to the white/navy-line mascot illustration
namespace Palette
{
    const juce::Colour bgTop    { 0xfffdfcf7 };   // warm paper white
    const juce::Colour bgBot    { 0xfff1eee4 };
    const juce::Colour panel    { 0xffffffff };
    const juce::Colour panelLn  { 0xffd8d2c2 };
    const juce::Colour ink      { 0xff2c4a6e };   // outline navy from the artwork
    const juce::Colour inkSoft  { 0xff5f7896 };
    const juce::Colour yellow   { 0xfff5e663 };   // eyes / noodles
    const juce::Colour yellowDk { 0xffd9c53a };
    const juce::Colour green    { 0xff4e8f5f };   // nori shaker
    const juce::Colour salmon   { 0xffef9a80 };
    const juce::Colour track    { 0xffe6e1d3 };
}

//==============================================================================
class GzzioLnF : public juce::LookAndFeel_V4
{
public:
    GzzioLnF()
    {
        setColour (juce::Label::textColourId, Palette::ink);
        setColour (juce::Slider::textBoxTextColourId, Palette::ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, Palette::ink);
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::outlineColourId, Palette::ink.darker (0.2f));
        setColour (juce::TextButton::buttonColourId, Palette::panel);
        setColour (juce::TextButton::textColourOffId, Palette::ink);
        setColour (juce::TextButton::textColourOnId, Palette::ink);
        setColour (juce::ComboBox::backgroundColourId, Palette::panel);
        setColour (juce::ComboBox::textColourId, Palette::ink);
        setColour (juce::ComboBox::outlineColourId, Palette::panelLn);
        setColour (juce::ComboBox::arrowColourId, Palette::inkSoft);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
        setColour (juce::PopupMenu::textColourId, Palette::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::yellow);
        setColour (juce::PopupMenu::highlightedTextColourId, Palette::ink);
    }

    void setFontScale (float s) { fontScale = juce::jmax (0.5f, s); }
    float getFontScale() const  { return fontScale; }

    juce::Font getLabelFont (juce::Label& l) override
    {
        const float base = (float) l.getProperties().getWithDefault ("fontH", 14.0);
        const bool  bold = (bool)  l.getProperties().getWithDefault ("bold", false);
        return juce::Font (juce::FontOptions().withHeight (base * fontScale)
                                              .withStyle (bold ? "Bold" : "Regular"));
    }

    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        if (l.isBeingEdited()) return;
        const auto f = getLabelFont (l);
        g.setColour (l.findColour (juce::Label::textColourId)
                         .withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
        g.setFont (f);
        const auto area = l.getBorderSize().subtractedFrom (l.getLocalBounds());
        g.drawFittedText (l.getText(), area, l.getJustificationType(),
                          juce::jmax (1, (int) ((float) area.getHeight() / f.getHeight())),
                          l.getMinimumHorizontalScale());
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions()
                               .withHeight (juce::jmin ((float) h * 0.62f, 15.0f) * fontScale)
                               .withStyle ("Bold"));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions().withHeight (14.0f * fontScale));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (juce::FontOptions().withHeight (15.0f * fontScale));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  c      = bounds.getCentre();
        const float angle  = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::track);
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (Palette::yellowDk);
        g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // knob: white bowl with navy outline (like the mascot's bowl head)
        const float kr = radius - 9.0f;
        juce::ColourGradient grad (juce::Colours::white, c.x, c.y - kr,
                                   juce::Colour (0xffeceadf), c.x, c.y + kr, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
        g.setColour (Palette::ink);
        g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 2.0f);

        juce::Path pointer;
        const float pOut = kr * 0.78f, pIn = kr * 0.25f;
        pointer.startNewSubPath (c.x + pIn  * std::sin (angle), c.y - pIn  * std::cos (angle));
        pointer.lineTo          (c.x + pOut * std::sin (angle), c.y - pOut * std::cos (angle));
        g.setColour (Palette::ink);
        g.strokePath (pointer, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId)
                                      : bg;
        if (down) col = col.darker (0.06f);
        else if (over) col = col.darker (0.03f);
        g.setColour (col);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (b.getToggleState() ? Palette::ink : Palette::panelLn);
        g.drawRoundedRectangle (r, 7.0f, b.getToggleState() ? 2.0f : 1.2f);
    }

private:
    float fontScale { 1.0f };
};

//==============================================================================
// Tuner + meter strip. Analysis only; smoothed, quiet-signal friendly.
class VocalTuner : public juce::Component, private juce::Timer
{
public:
    explicit VocalTuner (VocalGzzioProcessor& p) : proc (p)
    {
        buffer.resize ((size_t) VocalGzzioProcessor::tunerSize);
        for (int hz = 415; hz <= 445; ++hz)
            refPitchBox.addItem (juce::String (hz) + " Hz", hz);
        refPitchBox.setSelectedId (440, juce::dontSendNotification);
        refPitchBox.setJustificationType (juce::Justification::centred);
        refPitchBox.onChange = [this]
        {
            if (auto* prm = proc.apvts.getParameter ("refpitch"))
                prm->setValueNotifyingHost (
                    proc.apvts.getParameterRange ("refpitch")
                        .convertTo0to1 ((float) refPitchBox.getSelectedId()));
        };
        addAndMakeVisible (refPitchBox);
        startTimerHz (30);
    }

    void paint   (juce::Graphics&) override;
    void resized ()                override;

private:
    void timerCallback() override;
    void analyse();

    VocalGzzioProcessor& proc;
    std::vector<float>   buffer;
    juce::ComboBox       refPitchBox;

    juce::String noteName { "--" };
    float cents { 0 }, freq { 0 };
    bool  hasPitch { false };
    float dispCents { 0 }, dispFreq { 0 };
    int   hold { 0 };
    float dispIn { 0 }, dispOut { 0 }, dispGR { 0 }, dispDS { 0 }, dispDN { 0 };
};

//==============================================================================
class VocalGzzioContent : public juce::Component, private juce::Timer
{
public:
    explicit VocalGzzioContent (VocalGzzioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setFontScale (float s);
    std::function<void (bool)> onZoomToggle;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void timerCallback() override;   // learn-button state refresh
    void addKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                  const juce::String& tooltip, const juce::String& suffix);
    void placeRow (juce::Rectangle<int> area, std::initializer_list<Knob*> ks);
    void styleButton (juce::TextButton& b);
    juce::Font cfont (float h, bool bold = false) const;
    void applyVoicePreset (int id);
    void applyMicPreset (int id);
    void applyScene();
    void switchSlot (int target);
    void updateABButtons();
    void savePreset();
    void loadPreset();

    VocalGzzioProcessor& processor;
    juce::Image          mascot;
    juce::TooltipWindow  tooltipWindow { this, 380 };
    VocalTuner           tuner;

    Knob gate, lowCut, mudK, harshK, denoiseK,
         comp1K, comp2K, attackK, releaseK, deessK,
         presenceK, airK, warmthK, sustainK,
         makeupK, mixK, widthK, doublerK, delayK, revSizeK, revMixK;

    // Preset system: voice type x mic model x scene, all combinable
    juce::ComboBox voiceBox, micBox;
    juce::TextButton sceneSolo, sceneTalk, sceneBand { "Band" };
    int currentScene { 1 };   // 0 Solo, 1 Talk, 2 Band

    juce::TextButton learnButton { "LEARN" };
    juce::TextButton resetButton { "RESET" };
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "A>B" }, saveButton { "SAVE" }, loadButton { "LOAD" };
    juce::TextButton zoomButton;

    juce::ValueTree stateA, stateB;
    int currentSlot { 0 };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> cleanArea, dynArea, toneArea, spaceArea;
    float fontScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioContent)
};

//==============================================================================
class VocalGzzioEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalGzzioEditor (VocalGzzioProcessor&);
    ~VocalGzzioEditor() override;

    void resized() override;

private:
    static constexpr int baseW = 760;
    static constexpr int baseH = 924;

    VocalGzzioProcessor& processor;
    GzzioLnF             lnf;
    VocalGzzioContent    content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioEditor)
};
