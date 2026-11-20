#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace vx;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VoxifyProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    int styleCount = 0;
    const auto* styles = stylePresets (styleCount);
    const auto& def = styles[0];

    for (int m = 0; m < NumMods; ++m)
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { modId (m), 1 }, modName (m),
                                                                 Range (0.0f, 100.0f, 0.1f), def.amt[m]));
    for (int m = 0; m < NumMods; ++m)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { onId (m), 1 }, juce::String (modName (m)) + " On", true));

    auto addF = [&] (const char* id, const char* name, float lo, float hi, float d, float step = 0.01f)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, Range (lo, hi, step), d));
    };
    addF ("aimix",     "AI Mix",        0.0f, 100.0f, 100.0f, 0.1f);
    addF ("tuneSpeed", "Tune Speed",    0.0f, 100.0f, def.tuneSpeed, 0.1f);
    addF ("ingain",    "Input Gain",    -24.0f, 24.0f, 0.0f);
    addF ("outgain",   "Output Gain",   -24.0f, 12.0f, 0.0f);
    addF ("mudDb",     "Mud Cut",       -8.0f, 0.0f, def.mudDb);
    addF ("presDb",    "Presence",      -2.0f, 8.0f, def.presDb);
    addF ("airDb",     "Air",           0.0f, 8.0f, def.airDb);
    addF ("deessHz",   "De-Ess Freq",   4000.0f, 9500.0f, def.deessHz, 1.0f);
    addF ("hpHz",      "Low Cut",       40.0f, 200.0f, def.hpHz, 1.0f);

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "key", 1 }, "Key", keyNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scale", 1 }, "Scale", scaleNames(), 1));
    layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "keyAuto", 1 }, "Key Auto-Detect", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "delayDiv", 1 }, "Delay Time", delayDivNames(), def.delayDiv));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "style", 1 }, "Style", styleNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "adlib", 1 }, "Adlib Mode", false));
    layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "smartGain", 1 }, "Smart Gain", false));
    layout.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "bypass", 1 }, "Before (Bypass)", false));
    return layout;
}

VoxifyProcessor::VoxifyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Beat (sidechain)", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "VOXIFY", createLayout())
{
    for (int m = 0; m < NumMods; ++m)
    {
        amtP[m] = apvts.getRawParameterValue (modId (m));
        onP[m]  = apvts.getRawParameterValue (onId (m));
    }
    aimixP = apvts.getRawParameterValue ("aimix");
    tuneSpeedP = apvts.getRawParameterValue ("tuneSpeed");
    inGainP = apvts.getRawParameterValue ("ingain");
    outGainP = apvts.getRawParameterValue ("outgain");
    mudP = apvts.getRawParameterValue ("mudDb");
    presP = apvts.getRawParameterValue ("presDb");
    airP = apvts.getRawParameterValue ("airDb");
    deessHzP = apvts.getRawParameterValue ("deessHz");
    hpP = apvts.getRawParameterValue ("hpHz");
    keyP = apvts.getRawParameterValue ("key");
    scaleP = apvts.getRawParameterValue ("scale");
    keyAutoP = apvts.getRawParameterValue ("keyAuto");
    delayDivP = apvts.getRawParameterValue ("delayDiv");
    adlibP = apvts.getRawParameterValue ("adlib");
    smartGainP = apvts.getRawParameterValue ("smartGain");
    bypassP = apvts.getRawParameterValue ("bypass");
}

bool VoxifyProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void VoxifyProcessor::prepareToPlay (double sr, int block)
{
    sampleRateHz = sr;
    const int maxBlock = juce::jmax (block, 4096);
    chain.prepare (sr, maxBlock);
    analyzer.prepare (sr);
    monoScratch.assign ((size_t) maxBlock, 0.0f);
    setLatencySamples (chain.getLatency());
}

//==============================================================================
void VoxifyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, true, 0);
    const int n = juce::jmin (main.getNumSamples(), (int) monoScratch.size());
    if (n <= 0 || main.getNumChannels() < 2) return;

    float* l = main.getWritePointer (0);
    float* r = main.getWritePointer (1);

    // ---- уровень входа, моно-копия для анализа ----
    const float inG = dbToLin (inGainP->load());
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        monoScratch[(size_t) i] = 0.5f * (l[i] + r[i]) * inG;
        pk = std::max (pk, std::max (std::abs (l[i]), std::abs (r[i])));
    }
    inPeak = pk;

    // ---- тональность: авто или ручная ----
    const bool keyAuto = keyAutoP->load() > 0.5f;
    int tonic = (int) keyP->load();
    int scale = (int) scaleP->load();
    if (keyAuto && analyzer.voicedSeconds.load() > 3.0f && analyzer.keyStrength.load() > 0.4f)
    {
        tonic = analyzer.keyTonic.load();
        scale = analyzer.keyMinor.load() ? 1 : 0;
    }
    const uint16_t mask = makeScaleMask (tonic, scale);
    effTonic = (float) tonic; effScale = (float) scale;

    analyzer.setScaleMask (mask);
    analyzer.process (monoScratch.data(), n);

    // ---- Vocal vs Beat (sidechain) ----
    bool beat = false;
    if (getBusCount (true) > 1)
        if (auto* sb = getBus (true, 1); sb != nullptr && sb->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            double sum = 0.0;
            for (int c = 0; c < sc.getNumChannels(); ++c)
            {
                const float* d = sc.getReadPointer (c);
                for (int i = 0; i < n; ++i) sum += (double) d[i] * d[i];
            }
            const float rmsDb = 10.0f * std::log10 ((float) (sum / (n * juce::jmax (1, sc.getNumChannels()))) + 1.0e-12f);
            if (rmsDb > -60.0f)
            {
                const float cur = beatRmsDb.load();
                beatRmsDb = cur < -90.0f ? rmsDb : cur + 0.01f * (rmsDb - cur);
            }
            beat = true;
        }
    beatPresent = beat;
    if (analyzer.levelDb.load() > -90.0f) vocalRmsDb = analyzer.levelDb.load();

    // ---- параметры цепочки ----
    ChainParams cp;
    const float master = aimixP->load() * 0.01f;
    for (int m = 0; m < NumMods; ++m)
    {
        cp.on[(size_t) m]  = onP[m]->load() > 0.5f;
        float a = amtP[m]->load() * 0.01f;
        if (m != Noise) a *= master;
        cp.amt[(size_t) m] = a;
    }
    cp.tuneSpeed = tuneSpeedP->load() * 0.01f;
    cp.mudDb = mudP->load();  cp.presDb = presP->load();  cp.airDb = airP->load();
    cp.deessHz = deessHzP->load();  cp.hpHz = hpP->load();
    cp.scaleMask = mask;
    cp.smartGain = smartGainP->load() > 0.5f;
    cp.adlib = adlibP->load() > 0.5f;
    cp.bypass = bypassP->load() > 0.5f;
    cp.inGainDb = inGainP->load();
    cp.outGainDb = outGainP->load();
    cp.delayDiv = (int) delayDivP->load();
    cp.pitchHz = analyzer.pitchHz.load();

    // BPM хоста (Beat Detection = темп из FL Studio, а не анализ аудио)
    cp.bpm = 120.0f;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                cp.bpm = (float) *bpm;
    hostBpm = cp.bpm;

    chain.process (l, r, n, cp);
    compGrDb = chain.getCompGrDb();

    // ---- метры выхода ----
    float op = 0.0f;
    double s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        op = std::max (op, std::max (std::abs (l[i]), std::abs (r[i])));
        s2 += 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
    }
    outPeak = op;
    const float db = 10.0f * std::log10 ((float) (s2 / n) + 1.0e-12f);
    outRmsDb = outRmsDb.load() + 0.1f * (db - outRmsDb.load());
}

//==============================================================================
void VoxifyProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float VoxifyProcessor::getParam (const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue (id)) return p->load();
    return 0.0f;
}

void VoxifyProcessor::applyStyle (int index)
{
    int count = 0;
    const auto* styles = stylePresets (count);
    index = juce::jlimit (0, count - 1, index);
    const auto& s = styles[index];

    undoState = apvts.copyState();
    setParam ("style", (float) index);
    for (int m = 0; m < NumMods; ++m) setParam (modId (m), s.amt[m]);
    setParam ("tuneSpeed", s.tuneSpeed);
    setParam ("mudDb", s.mudDb);
    setParam ("presDb", s.presDb);
    setParam ("airDb", s.airDb);
    setParam ("deessHz", s.deessHz);
    setParam ("hpHz", s.hpHz);
    setParam ("delayDiv", (float) s.delayDiv);
}

void VoxifyProcessor::restartAnalysis()
{
    analyzer.reset();
}

// AI Auto Mix: берёт базу из выбранного стиля и корректирует её по измеренному спектру/динамике.
// Все пороги эвристические и не откалиброваны - см. README.
void VoxifyProcessor::runAutoMix()
{
    int count = 0;
    const auto* styles = stylePresets (count);
    const int styleIdx = juce::jlimit (0, count - 1, (int) getParam ("style"));
    const auto& s = styles[styleIdx];

    undoState = apvts.copyState();

    // нет данных - просто база стиля
    if (analyzer.voicedSeconds.load() < 2.0f)
    {
        applyStyle (styleIdx);
        return;
    }

    const auto rel = analyzer.relBands();
    const auto def = defaultTargetTilt();
    const std::array<float, 6>& target = hasRef ? refRel : def;
    std::array<float, 6> dev {};
    for (size_t i = 0; i < 6; ++i) dev[i] = rel[i] - target[i];

    // Chain Builder: включаем только то, что нужно
    const float nf = analyzer.noiseFloorDb.load();
    const float crest = analyzer.peakDb.load() - analyzer.levelDb.load();

    for (int m = 0; m < NumMods; ++m) { setParam (onId (m), 1.0f); setParam (modId (m), s.amt[m]); }

    // шумоподавление по уровню шума
    const float noiseAmt = juce::jlimit (0.0f, 90.0f, juce::jmap (nf, -80.0f, -45.0f, 10.0f, 90.0f));
    setParam (modId (Noise), noiseAmt);
    if (nf < -75.0f) setParam (onId (Noise), 0.0f);

    // де-эссер
    const float deessAmt = juce::jlimit (20.0f, 95.0f, s.amt[DeEss] + dev[4] * 5.0f);
    setParam (modId (DeEss), deessAmt);

    // компрессия по динамике (разница пик/RMS)
    float compAmt = s.amt[Comp];
    if (crest > 14.0f) compAmt += 15.0f; else if (crest < 9.0f) compAmt -= 10.0f;
    setParam (modId (Comp), juce::jlimit (20.0f, 95.0f, compAmt));

    // Smart EQ: поправки относительно целевого наклона (или референса)
    setParam ("mudDb",  juce::jlimit (-8.0f, 0.0f, s.mudDb - std::max (0.0f, dev[1]) * 0.7f));
    setParam ("presDb", juce::jlimit (-2.0f, 8.0f, s.presDb - dev[3] * 0.5f));
    setParam ("airDb",  juce::jlimit (0.0f, 8.0f, s.airDb - dev[5] * 0.4f));
    setParam ("deessHz", s.deessHz);
    setParam ("hpHz", s.hpHz);
    setParam ("tuneSpeed", s.tuneSpeed);
    setParam ("delayDiv", (float) s.delayDiv);

    // тональность
    if (analyzer.keyStrength.load() > 0.4f)
    {
        setParam ("keyAuto", 1.0f);
        setParam ("key", (float) analyzer.keyTonic.load());
        setParam ("scale", analyzer.keyMinor.load() ? 1.0f : 0.0f);
    }
    setParam ("smartGain", 1.0f);
}

void VoxifyProcessor::undoLastAutoAction()
{
    if (undoState.isValid()) apvts.replaceState (undoState);
}

void VoxifyProcessor::toggleAB()
{
    slots[activeSlot] = apvts.copyState();
    activeSlot ^= 1;
    if (slots[activeSlot].isValid()) apvts.replaceState (slots[activeSlot]);
    else slots[activeSlot] = apvts.copyState();
}

//==============================================================================
void VoxifyProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void VoxifyProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VoxifyProcessor::createEditor() { return new VoxifyEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxifyProcessor(); }
