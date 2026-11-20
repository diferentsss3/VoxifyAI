#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace vxui
{
inline juce::Colour bg()     { return juce::Colour (0xFF0B0814); }
inline juce::Colour panel()  { return juce::Colour (0xFF171126); }
inline juce::Colour accent() { return juce::Colour (0xFF9B5CFF); }
inline juce::Colour accent2(){ return juce::Colour (0xFF5CE1FF); }
inline juce::Colour text()   { return juce::Colour (0xFFE8E0FF); }
inline juce::Colour dim()    { return juce::Colour (0xFF8B7FB0); }
}

class VoxifyLnF : public juce::LookAndFeel_V4
{
public:
    VoxifyLnF();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle,
                           float endAngle, juce::Slider&) override;
};

class VoxifyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoxifyEditor (VoxifyProcessor&);
    ~VoxifyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadReference();
    void refreshCoach();

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
        juce::ToggleButton on { "ON" };
        std::unique_ptr<SA> sa;
        std::unique_ptr<BA> ba;
    };

    VoxifyProcessor& proc;
    VoxifyLnF lnf;

    std::array<Knob, vx::NumMods> knobs;

    juce::Slider aiMix  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider inGain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider outGain{ juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label aiMixLabel, inLabel, outLabel, infoLabel, titleLabel, coachTitle;

    juce::ComboBox styleBox, keyBox, scaleBox, divBox;
    juce::TextButton analyzeBtn { "ANALYZE VOCAL" }, autoMixBtn { "AUTO MIX" }, refBtn { "REFERENCE..." },
                     abBtn { "A" }, undoBtn { "UNDO" };
    juce::ToggleButton beforeBtn { "BEFORE (bypass)" }, adlibBtn { "ADLIB MODE" },
                       smartGainBtn { "SMART GAIN" }, keyAutoBtn { "KEY AUTO" };
    juce::TextEditor coach;

    std::unique_ptr<SA> aiMixA, inA, outA;
    std::unique_ptr<BA> beforeA, adlibA, smartA, keyAutoA;
    std::unique_ptr<CA> keyA, scaleA, divA;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> meterIn, meterOut;
    int coachTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxifyEditor)
};
