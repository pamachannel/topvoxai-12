#include "PluginEditor.h"

using namespace topvox;

namespace
{
const juce::Colour kBlack  (0xff0b0b0d);
const juce::Colour kPanel  (0xff17171b);
const juce::Colour kGold   (0xffd4a437);
const juce::Colour kOrange (0xffff8a1f);
const juce::Colour kGrey   (0xff8a8a93);

juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
}

// ------------------------------------------------------------- LookAndFeel
TopVoxLookAndFeel::TopVoxLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,               kGrey);
    setColour (juce::TextButton::buttonColourId,        juce::Colour (0xff25252b));
    setColour (juce::TextButton::textColourOffId,       kGold);
    setColour (juce::TextButton::textColourOnId,        kOrange);
    setColour (juce::ComboBox::backgroundColourId,      juce::Colour (0xff25252b));
    setColour (juce::ComboBox::textColourId,            juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId,         kGold.withAlpha (0.6f));
    setColour (juce::ComboBox::arrowColourId,           kGold);
    setColour (juce::PopupMenu::backgroundColourId,     juce::Colour (0xff1d1d22));
    setColour (juce::PopupMenu::textColourId,           juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kOrange.withAlpha (0.8f));
}

void TopVoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float track = radius - 3.0f;

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, track, track, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff2b2b31));
    g.strokePath (bg, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (centre.x, centre.y, track, track, 0.0f, startAngle, angle, true);
    g.setColour (s.isEnabled() ? kOrange : kGrey);
    g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float body = radius * 0.66f;
    g.setColour (juce::Colour (0xff202026));
    g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
    g.setColour (kGold.withAlpha (0.5f));
    g.drawEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRectangle (-1.5f, -body * 0.92f, 3.0f, body * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (kGold);
    g.fillPath (pointer);
}

// ------------------------------------------------------------------ Éditeur
TopVoxEditor::TopVoxEditor (TopVoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    setSize (1060, 680);
    setResizable (false, false);

    for (int i = 0; i < kNumParams; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
        k.slider.setDoubleClickReturnValue (true, spec (i).def);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, spec (i).id, k.slider);
        k.label.setText (U (spec (i).name), juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::Font (13.0f));
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
    }

    sections = {
        { U ("ENTRÉE"),       { pInGain, pHpf, pGate },                              {} },
        { U ("DE-ESSER"),     { pDeess, pDeessFreq },                                {} },
        { U ("ÉGALISEUR"),    { pEqLow, pEqMud, pEqPres, pEqAir },                   {} },
        { U ("COMPRESSEUR"),  { pCompThr, pCompRatio, pCompAtt, pCompRel, pCompMk }, {} },
        { U ("SATURATION"),   { pSat },                                              {} },
        { U ("SORTIE"),       { pWidth, pCeil, pOutGain },                           {} },
        { U ("REVERB"),       { pRevMix, pRevSize, pRevPre },                        {} },
        { U ("DELAY"),        { pDelMix, pDelTime, pDelFb },                         {} },
    };

    for (int i = 0; i < 5; ++i)
        styleBox.addItem (U (styleName (i)), i + 1);
    styleBox.setSelectedId ((int) proc.apvts.state.getProperty ("style", 1) + 1, juce::dontSendNotification);
    styleBox.onChange = [this] { proc.apvts.state.setProperty ("style", styleBox.getSelectedId() - 1, nullptr); };
    addAndMakeVisible (styleBox);

    presetButton.setButtonText (U ("CHARGER LE STYLE"));
    presetButton.onClick = [this]
    {
        proc.applyStyle (styleBox.getSelectedId() - 1);
        statusMessage = U ("Style « ") + styleBox.getText() + U (" » chargé.");
        repaint();
    };
    addAndMakeVisible (presetButton);

    autoButton.setButtonText ("AUTO MIX");
    autoButton.onClick = [this]
    {
        if (proc.runAutoMix (styleBox.getSelectedId() - 1))
            statusMessage = U ("Auto Mix appliqué d'après ta voix (style « ") + styleBox.getText() + U (" »).");
        else
            statusMessage = U ("Pas assez de voix analysée : lance la lecture et chante ou parle pendant au moins 5 secondes, puis reclique.");
        repaint();
    };
    addAndMakeVisible (autoButton);

    resetButton.setButtonText (U ("NOUVELLE ANALYSE"));
    resetButton.onClick = [this]
    {
        proc.resetAnalysis();
        statusMessage = U ("Analyse remise à zéro. Lance la lecture de la voix.");
        repaint();
    };
    addAndMakeVisible (resetButton);

    statusMessage = U ("Lance la lecture de ta voix, puis clique sur AUTO MIX.");
    startTimerHz (30);
}

TopVoxEditor::~TopVoxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TopVoxEditor::timerCallback()
{
    auto toNorm = [] (float lin) { return juce::jlimit (0.f, 1.f, (linToDb (lin) + 60.f) / 60.f); };

    const float in  = toNorm (proc.chain.inPeak.exchange (0.f));
    const float out = toNorm (proc.chain.outPeak.exchange (0.f));
    const float gr  = juce::jlimit (0.f, 1.f, juce::jmax (proc.chain.compGr.exchange (0.f),
                                                          proc.chain.limGr.exchange (0.f)) / 20.f);
    proc.chain.deessGr.store (0.f);

    meterIn  = juce::jmax (in,  meterIn  * 0.86f);
    meterOut = juce::jmax (out, meterOut * 0.86f);
    meterGr  = juce::jmax (gr,  meterGr  * 0.90f);
    report   = proc.getReport();
    repaint();
}

void TopVoxEditor::drawMeter (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                              float v, bool fromTop)
{
    g.setColour (kGrey);
    g.setFont (juce::Font (12.0f));
    g.drawText (title, area.removeFromBottom (18), juce::Justification::centred);

    auto bar = area.reduced (10, 0).toFloat();
    g.setColour (juce::Colour (0xff0f0f12));
    g.fillRoundedRectangle (bar, 4.0f);

    const float h = bar.getHeight() * juce::jlimit (0.f, 1.f, v);
    auto fill = fromTop ? bar.withHeight (h) : bar.withTop (bar.getBottom() - h);
    juce::ColourGradient grad (kOrange, bar.getX(), bar.getY(),
                               juce::Colour (0xff3ecf6e), bar.getX(), bar.getBottom(), false);
    grad.addColour (0.12, juce::Colour (0xffe8d03a));
    g.setGradientFill (fromTop ? juce::ColourGradient (juce::Colour (0xffff8a1f), bar.getX(), bar.getY(),
                                                       juce::Colour (0xffd4a437), bar.getX(), bar.getBottom(), false)
                               : grad);
    g.fillRoundedRectangle (fill, 4.0f);
    g.setColour (kGold.withAlpha (0.35f));
    g.drawRoundedRectangle (bar, 4.0f, 1.0f);
}

void TopVoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBlack);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1710), 0.f, 0.f, kBlack, 0.f, 120.f, false));
    g.fillRect (0, 0, getWidth(), 120);

    g.setColour (kGold);
    g.setFont (juce::Font (34.0f, juce::Font::bold));
    g.drawText ("TOPVOX", 24, 14, 170, 40, juce::Justification::centredLeft);
    g.setColour (kOrange);
    g.drawText ("VOICE", 168, 14, 140, 40, juce::Justification::centredLeft);
    g.setColour (kGrey);
    g.setFont (juce::Font (13.0f));
    g.drawText (U ("Mixage vocal intelligent : analyse locale, sans Internet"), 26, 52, 460, 20, juce::Justification::centredLeft);

    for (const auto& s : sections)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (s.bounds.toFloat(), 8.0f);
        g.setColour (kGold.withAlpha (0.35f));
        g.drawRoundedRectangle (s.bounds.toFloat(), 8.0f, 1.0f);
        g.setColour (kGold);
        g.setFont (juce::Font (12.5f, juce::Font::bold));
        g.drawText (s.title, s.bounds.withHeight (24).reduced (12, 0), juce::Justification::centredLeft);
    }

    // zone d'analyse
    g.setColour (kPanel);
    g.fillRoundedRectangle (analysisArea.toFloat(), 8.0f);
    g.setColour (kOrange.withAlpha (0.5f));
    g.drawRoundedRectangle (analysisArea.toFloat(), 8.0f, 1.0f);
    g.setColour (kGold);
    g.setFont (juce::Font (12.5f, juce::Font::bold));
    g.drawText (U ("ANALYSE DE LA VOIX"), analysisArea.withHeight (24).reduced (12, 0), juce::Justification::centredLeft);

    auto txt = analysisArea.reduced (12, 28);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (13.0f));
    if (report.activeSec > 0.f)
    {
        juce::String t;
        t << U ("Voix analysée : ") << juce::String (report.activeSec, 1) << " s"
          << "   |   RMS " << juce::String (report.rmsDb, 1) << " dB"
          << "   |   " << U ("crête ") << juce::String (report.peakDb, 1) << " dB\n"
          << U ("Grave ")  << juce::String (report.low  * 100.f, 0) << " %   "
          << "Medium "     << juce::String (report.mid  * 100.f, 0) << " %   "
          << U ("Présence ") << juce::String (report.pres * 100.f, 0) << " %   "
          << "Sibilance "  << juce::String (report.sib  * 100.f, 0) << " %";
        g.drawFittedText (t, txt.removeFromTop (40), juce::Justification::topLeft, 3);
    }
    else
        g.drawFittedText (U ("Aucune voix analysée pour l'instant."), txt.removeFromTop (40), juce::Justification::topLeft, 2);

    g.setColour (kOrange);
    g.drawFittedText (statusMessage, txt, juce::Justification::topLeft, 4);

    // VU-mètres
    g.setColour (kPanel);
    g.fillRoundedRectangle (metersArea.toFloat(), 8.0f);
    g.setColour (kGold.withAlpha (0.35f));
    g.drawRoundedRectangle (metersArea.toFloat(), 8.0f, 1.0f);

    auto m = metersArea.reduced (6, 12);
    const int w = m.getWidth() / 3;
    drawMeter (g, m.removeFromLeft (w), "IN", meterIn, false);
    drawMeter (g, m.removeFromLeft (w), "OUT", meterOut, false);
    drawMeter (g, m, "GR", meterGr, true);
}

void TopVoxEditor::resized()
{
    const int W = getWidth(), H = getHeight();
    const int margin = 20, gap = 10;

    // en-tête : boutons à droite
    {
        auto r = juce::Rectangle<int> (W - margin - 640, 26, 640, 32);
        resetButton.setBounds  (r.removeFromRight (150)); r.removeFromRight (gap);
        autoButton.setBounds   (r.removeFromRight (110)); r.removeFromRight (gap);
        presetButton.setBounds (r.removeFromRight (150)); r.removeFromRight (gap);
        styleBox.setBounds     (r.removeFromRight (160));
    }

    const int top = 96;
    metersArea = { W - margin - 140, top, 140, H - top - margin };

    const int contentW = W - margin * 2 - 140 - gap;
    const int rows = 3;
    const int rowH = (H - top - margin - gap * (rows - 1)) / rows;

    // lignes : indices de sections, -1 = zone d'analyse
    const std::vector<std::vector<int>> layoutRows = { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, -1 } };

    for (int row = 0; row < rows; ++row)
    {
        const auto& ids = layoutRows[(size_t) row];
        float units = 0.f;
        for (int id : ids) units += (id < 0) ? 3.2f : (float) sections[(size_t) id].ids.size();

        const int usable = contentW - gap * ((int) ids.size() - 1);
        int x = margin;
        const int y = top + row * (rowH + gap);

        for (int id : ids)
        {
            const float u = (id < 0) ? 3.2f : (float) sections[(size_t) id].ids.size();
            const int w = (int) std::round (usable * u / units);
            const juce::Rectangle<int> b (x, y, w, rowH);
            x += w + gap;

            if (id < 0) { analysisArea = b; continue; }

            auto& s = sections[(size_t) id];
            s.bounds = b;

            auto inner = b.reduced (8, 0).withTrimmedTop (26).withTrimmedBottom (6);
            const int cw = inner.getWidth() / (int) s.ids.size();
            for (int pid : s.ids)
            {
                auto cell = inner.removeFromLeft (cw);
                auto& k = knobs[(size_t) pid];
                k.label.setBounds (cell.removeFromTop (18));
                k.slider.setBounds (cell);
            }
        }
    }
}
