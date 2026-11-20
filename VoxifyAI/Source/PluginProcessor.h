#pragma once
#include "Common.h"
#include "Params.h"
#include "Presets.h"
#include "Analysis.h"
#include "Dsp.h"

class VoxifyProcessor : public juce::AudioProcessor
{
public:
    VoxifyProcessor();
    ~VoxifyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Voxify AI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- действия из GUI (message thread) ----
    void applyStyle (int index);          // загрузить пресет стиля
    void runAutoMix();                    // AI Auto Mix: анализ + подбор параметров
    void restartAnalysis();               // ANALYZE VOCAL: сбросить накопленную статистику
    void undoLastAutoAction();            // вернуть состояние до Auto Mix / смены стиля
    void toggleAB();                      // переключить слот A/B
    int  getActiveSlot() const { return activeSlot; }
    void setReference (const std::array<float, 6>& rel) { refRel = rel; hasRef = true; }
    void clearReference() { hasRef = false; }
    bool hasReference() const { return hasRef; }

    float getBeatMinusVocalValid() const { return beatPresent.load() ? 1.0f : 0.0f; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    vx::VoiceAnalyzer analyzer;

    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, outRmsDb { -100.0f }, compGrDb { 0.0f },
                       vocalRmsDb { -100.0f }, beatRmsDb { -100.0f }, hostBpm { 0.0f };
    std::atomic<bool>  beatPresent { false };
    std::atomic<float> effTonic { 0.0f }, effScale { 0.0f };

private:
    void setParam (const juce::String& id, float value);
    float getParam (const juce::String& id) const;

    vx::VocalChain chain;
    std::vector<float> monoScratch;

    std::atomic<float>* amtP[vx::NumMods] {};
    std::atomic<float>* onP[vx::NumMods] {};
    std::atomic<float> *aimixP, *tuneSpeedP, *inGainP, *outGainP, *mudP, *presP, *airP, *deessHzP, *hpP,
                       *keyP, *scaleP, *keyAutoP, *delayDivP, *adlibP, *smartGainP, *bypassP;

    juce::ValueTree slots[2];
    int activeSlot = 0;
    juce::ValueTree undoState;

    std::array<float, 6> refRel {};
    bool hasRef = false;
    double sampleRateHz = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxifyProcessor)
};
