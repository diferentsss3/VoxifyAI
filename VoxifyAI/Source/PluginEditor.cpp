#include "PluginEditor.h"

using namespace vxui;

//==============================================================================
VoxifyLnF::VoxifyLnF()
{
    setColour (juce::ComboBox::backgroundColourId, panel());
    setColour (juce::ComboBox::textColourId, text());
    setColour (juce::ComboBox::outlineColourId, accent().withAlpha (0.5f));
    setColour (juce::ComboBox::arrowColourId, accent());
    setColour (juce::PopupMenu::backgroundColourId, panel());
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent());
    setColour (juce::TextButton::buttonColourId, panel());
    setColour (juce::TextButton::textColourOffId, text());
    setColour (juce::ToggleButton::textColourId, text());
    setColour (juce::ToggleButton::tickColourId, accent());
    setColour (juce::ToggleButton::tickDisabledColourId, dim());
    setColour (juce::Label::textColourId, text());
    setColour (juce::TextEditor::backgroundColourId, panel());
    setColour (juce::TextEditor::textColourId, text());
    setColour (juce::TextEditor::outlineColourId, accent().withAlpha (0.3f));
}

void VoxifyLnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                  float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float lw = std::max (3.0f, r * 0.13f);
    const float ar = r - lw * 0.5f;
    const float ang = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (c.x, c.y, ar, ar, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xFF2A2145));
    g.strokePath (track, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, ar, ar, 0.0f, startAngle, ang, true);
    g.setGradientFill (juce::ColourGradient (accent(), c.x - r, c.y, accent2(), c.x + r, c.y, false));
    g.strokePath (val, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ir = ar - lw;
    g.setColour (juce::Colour (0xFF0F0B1C));
    g.fillEllipse (c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f);

    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -ir, 3.0f, ir * 0.35f, 1.5f);
    g.setColour (text());
    g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (c.x, c.y));

    g.setColour (text());
    g.setFont (juce::FontOptions (juce::jlimit (11.0f, 30.0f, r * 0.38f), juce::Font::bold));
    g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> (c.x - ir, c.y - ir * 0.3f, ir * 2.0f, ir),
                juce::Justification::centred);
}

//==============================================================================
VoxifyEditor::VoxifyEditor (VoxifyProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (1100, 720);

    auto styleLabel = [] (juce::Label& l, const juce::String& t, float size, bool bold, juce::Justification j)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        l.setJustificationType (j);
        l.setColour (juce::Label::textColourId, text());
    };

    styleLabel (titleLabel, "VOXIFY AI", 30.0f, true, juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    // ---- ручки модулей ----
    for (int m = 0; m < vx::NumMods; ++m)
    {
        auto& k = knobs[(size_t) m];
        styleLabel (k.label, vx::modName (m), 12.0f, true, juce::Justification::centred);
        k.slider.setRange (0.0, 100.0, 0.1);
        k.slider.setNumDecimalPlacesToDisplay (0);
        k.slider.setDoubleClickReturnValue (true, 50.0);
        k.sa = std::make_unique<SA> (proc.apvts, vx::modId (m), k.slider);
        k.ba = std::make_unique<BA> (proc.apvts, vx::onId (m), k.on);
        addAndMakeVisible (k.slider); addAndMakeVisible (k.label); addAndMakeVisible (k.on);
    }

    // ---- большая ручка AI MIX, Input/Output ----
    aiMix.setNumDecimalPlacesToDisplay (0);
    aiMixA = std::make_unique<SA> (proc.apvts, "aimix", aiMix);
    inGain.setNumDecimalPlacesToDisplay (1);   inGain.setTextValueSuffix (" dB");
    outGain.setNumDecimalPlacesToDisplay (1);  outGain.setTextValueSuffix (" dB");
    inA  = std::make_unique<SA> (proc.apvts, "ingain", inGain);
    outA = std::make_unique<SA> (proc.apvts, "outgain", outGain);
    styleLabel (aiMixLabel, "AI MIX %", 14.0f, true, juce::Justification::centred);
    styleLabel (inLabel, "INPUT", 12.0f, true, juce::Justification::centred);
    styleLabel (outLabel, "OUTPUT", 12.0f, true, juce::Justification::centred);
    for (auto* c : { (juce::Component*) &aiMix, (juce::Component*) &inGain, (juce::Component*) &outGain,
                     (juce::Component*) &aiMixLabel, (juce::Component*) &inLabel, (juce::Component*) &outLabel })
        addAndMakeVisible (c);

    // ---- комбобоксы ----
    styleBox.addItemList (vx::styleNames(), 1);
    styleBox.setSelectedItemIndex ((int) proc.apvts.getRawParameterValue ("style")->load(), juce::dontSendNotification);
    styleBox.onChange = [this] { proc.applyStyle (styleBox.getSelectedItemIndex()); };
    keyBox.addItemList (vx::keyNames(), 1);
    scaleBox.addItemList (vx::scaleNames(), 1);
    divBox.addItemList (vx::delayDivNames(), 1);
    keyA   = std::make_unique<CA> (proc.apvts, "key", keyBox);
    scaleA = std::make_unique<CA> (proc.apvts, "scale", scaleBox);
    divA   = std::make_unique<CA> (proc.apvts, "delayDiv", divBox);
    for (auto* c : { &styleBox, &keyBox, &scaleBox, &divBox }) addAndMakeVisible (c);

    // ---- кнопки ----
    analyzeBtn.onClick = [this] { proc.restartAnalysis(); coach.setText (vx::U ("Анализ перезапущен: проиграйте вокал 5-10 секунд..."), false); };
    autoMixBtn.onClick = [this] { proc.runAutoMix(); refreshCoach(); };
    refBtn.onClick = [this]
    {
        if (proc.hasReference()) { proc.clearReference(); refBtn.setButtonText ("REFERENCE..."); }
        else loadReference();
    };
    abBtn.onClick = [this] { proc.toggleAB(); abBtn.setButtonText (proc.getActiveSlot() == 0 ? "A" : "B"); };
    undoBtn.onClick = [this] { proc.undoLastAutoAction(); };
    autoMixBtn.setColour (juce::TextButton::buttonColourId, accent().darker (0.3f));

    beforeA  = std::make_unique<BA> (proc.apvts, "bypass", beforeBtn);
    adlibA   = std::make_unique<BA> (proc.apvts, "adlib", adlibBtn);
    smartA   = std::make_unique<BA> (proc.apvts, "smartGain", smartGainBtn);
    keyAutoA = std::make_unique<BA> (proc.apvts, "keyAuto", keyAutoBtn);
    for (auto* c : { (juce::Component*) &analyzeBtn, (juce::Component*) &autoMixBtn, (juce::Component*) &refBtn,
                     (juce::Component*) &abBtn, (juce::Component*) &undoBtn, (juce::Component*) &beforeBtn,
                     (juce::Component*) &adlibBtn, (juce::Component*) &smartGainBtn, (juce::Component*) &keyAutoBtn })
        addAndMakeVisible (c);

    styleLabel (infoLabel, "", 13.0f, false, juce::Justification::centredLeft);
    styleLabel (coachTitle, vx::U ("AI VOCAL COACH"), 13.0f, true, juce::Justification::centredLeft);
    addAndMakeVisible (infoLabel); addAndMakeVisible (coachTitle);

    coach.setMultiLine (true);
    coach.setReadOnly (true);
    coach.setScrollbarsShown (true);
    coach.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (coach);
    refreshCoach();

    startTimerHz (20);
}

VoxifyEditor::~VoxifyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void VoxifyEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg());
    g.setGradientFill (juce::ColourGradient (accent().withAlpha (0.18f), 550.0f, 200.0f, bg(), 0.0f, 0.0f, true));
    g.fillRect (getLocalBounds());

    auto section = [&] (juce::Rectangle<int> r)
    {
        g.setColour (panel());
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (accent().withAlpha (0.25f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    };
    section ({ 16, 70, 1068, 250 });
    section ({ 16, 332, 1068, 190 });
    section ({ 16, 534, 1068, 170 });

    auto bar = [&] (juce::Rectangle<int> r, float peak)
    {
        g.setColour (juce::Colour (0xFF0F0B1C));
        g.fillRect (r);
        const float db = juce::jlimit (-60.0f, 0.0f, vx::linToDb (peak));
        const float h = (db + 60.0f) / 60.0f * (float) r.getHeight();
        g.setColour (db > -3.0f ? juce::Colours::red : db > -12.0f ? juce::Colours::yellow : juce::Colour (0xFF3DDC84));
        g.fillRect (r.getX(), r.getBottom() - (int) h, r.getWidth(), (int) h);
    };
    bar (meterIn,  proc.inPeak.load());
    bar (meterOut, proc.outPeak.load());

    g.setColour (dim());
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("RMS " + juce::String (proc.outRmsDb.load(), 1) + " dB   GR " + juce::String (proc.compGrDb.load(), 1) + " dB",
                getWidth() - 330, 8, 310, 20, juce::Justification::centredRight);
}

void VoxifyEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    // верхняя панель
    auto top = area.removeFromTop (54);
    titleLabel.setBounds (top.removeFromLeft (220));
    abBtn.setBounds (top.removeFromRight (40).reduced (0, 10));
    top.removeFromRight (6);
    undoBtn.setBounds (top.removeFromRight (64).reduced (0, 10));
    top.removeFromRight (6);
    beforeBtn.setBounds (top.removeFromRight (150).reduced (0, 10));
    top.removeFromRight (10);
    styleBox.setBounds (top.removeFromRight (230).reduced (0, 10));

    // центр: Key / AI MIX / кнопки
    auto mid = juce::Rectangle<int> (16, 70, 1068, 250).reduced (14);
    auto left = mid.removeFromLeft (300);
    auto right = mid.removeFromRight (300);
    auto centre = mid;

    keyBox.setBounds (left.getX(), 84, 90, 28);
    scaleBox.setBounds (left.getX() + 96, 84, 120, 28);
    keyAutoBtn.setBounds (left.getX(), 118, 140, 24);
    divBox.setBounds (left.getX() + 150, 118, 120, 26);
    adlibBtn.setBounds (left.getX(), 148, 140, 24);
    smartGainBtn.setBounds (left.getX() + 150, 148, 140, 24);
    infoLabel.setBounds (left.getX(), 180, 290, 100);

    inGain.setBounds (juce::Rectangle<int> (centre.getX() - 80, 120, 80, 80));
    outGain.setBounds (juce::Rectangle<int> (centre.getRight(), 120, 80, 80));
    inLabel.setBounds (centre.getX() - 80, 200, 80, 20);
    outLabel.setBounds (centre.getRight(), 200, 80, 20);
    aiMix.setBounds (centre.withSizeKeepingCentre (200, 200).translated (0, -8));
    aiMixLabel.setBounds (centre.getCentreX() - 60, centre.getBottom() - 22, 120, 20);

    analyzeBtn.setBounds (right.removeFromTop (40));
    right.removeFromTop (10);
    autoMixBtn.setBounds (right.removeFromTop (48));
    right.removeFromTop (10);
    refBtn.setBounds (right.removeFromTop (36));

    // ручки
    auto kr = juce::Rectangle<int> (16, 332, 1068, 190).reduced (8);
    const int w = kr.getWidth() / vx::NumMods;
    for (int m = 0; m < vx::NumMods; ++m)
    {
        auto cell = kr.removeFromLeft (w);
        auto& k = knobs[(size_t) m];
        k.label.setBounds (cell.removeFromTop (22));
        k.on.setBounds (cell.removeFromBottom (26).withSizeKeepingCentre (60, 24));
        k.slider.setBounds (cell);
    }

    // коуч + метры
    auto bottom = juce::Rectangle<int> (16, 534, 1068, 170).reduced (12);
    meterOut = bottom.removeFromRight (14);
    bottom.removeFromRight (6);
    meterIn = bottom.removeFromRight (14);
    bottom.removeFromRight (10);
    coachTitle.setBounds (bottom.removeFromTop (22));
    coach.setBounds (bottom);
}

//==============================================================================
void VoxifyEditor::timerCallback()
{
    // синхронизация комбобокса стиля (после undo/загрузки состояния)
    const int st = (int) proc.apvts.getRawParameterValue ("style")->load();
    if (styleBox.getSelectedItemIndex() != st)
        styleBox.setSelectedItemIndex (st, juce::dontSendNotification);

    const auto names = vx::keyNames();
    const int tonic = (int) proc.effTonic.load();
    const int scale = (int) proc.effScale.load();
    juce::String keyTxt = names[juce::jlimit (0, 11, tonic)] + " " + vx::scaleNames()[juce::jlimit (0, 2, scale)];

    juce::String pitchTxt = "-";
    const float hz = proc.analyzer.pitchHz.load();
    if (hz > 0.0f)
    {
        const int midi = (int) std::lround (69.0f + 12.0f * std::log2 (hz / 440.0f));
        pitchTxt = names[((midi % 12) + 12) % 12] + juce::String (midi / 12 - 1) + " (" + juce::String ((int) hz) + " Hz)";
    }

    infoLabel.setText ("KEY: " + keyTxt + " (" + juce::String (proc.analyzer.keyStrength.load(), 2) + ")\n"
                       + "PITCH: " + pitchTxt + "\n"
                       + "HOST BPM: " + juce::String (proc.hostBpm.load(), 1) + "\n"
                       + "VOICED: " + juce::String (proc.analyzer.voicedSeconds.load(), 1) + " s   LAT: "
                       + juce::String (proc.getLatencySamples()) + " smp",
                       juce::dontSendNotification);

    if (++coachTick >= 20) { coachTick = 0; refreshCoach(); }
    repaint();
}

void VoxifyEditor::refreshCoach()
{
    const bool beat = proc.beatPresent.load();
    const float diff = proc.vocalRmsDb.load() - proc.beatRmsDb.load();
    auto lines = vx::makeCoachReport (proc.analyzer, vx::defaultTargetTilt(), beat, diff);
    juce::String t;
    for (auto& s : lines) t << juce::String::fromUTF8 ("\xE2\x80\xA2 ") << s << "\n";
    if (coach.getText() != t) coach.setText (t, false);
}

void VoxifyEditor::loadReference()
{
    chooser = std::make_unique<juce::FileChooser> (vx::U ("Референс (лучше - акапелла)"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<VoxifyEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) return;

        const juce::int64 total = juce::jmin ((juce::int64) rd->lengthInSamples, (juce::int64) (rd->sampleRate * 60.0));
        vx::VoiceAnalyzer an;
        an.prepare (rd->sampleRate);

        juce::AudioBuffer<float> buf (2, 4096);
        std::vector<float> mono (4096);
        for (juce::int64 pos = 0; pos < total; pos += 4096)
        {
            const int n = (int) juce::jmin ((juce::int64) 4096, total - pos);
            buf.clear();
            rd->read (&buf, 0, n, pos, true, true);
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
            an.process (mono.data(), n);
        }

        if (an.voicedSeconds.load() < 2.0f) return; // слишком мало голоса в файле
        safe->proc.setReference (an.relBands());
        safe->refBtn.setButtonText ("CLEAR REFERENCE");
    });
}
