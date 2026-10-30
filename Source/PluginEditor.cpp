#include "PluginEditor.h"

using namespace juce;

namespace ui
{
// =====================================================================================
// Look and feel
// =====================================================================================
MurmurLookAndFeel::MurmurLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Label::textColourId, colours::text);
    setColour (TextButton::buttonColourId, colours::panel);
    setColour (TextButton::buttonOnColourId, colours::centre);
    setColour (TextButton::textColourOffId, colours::dim);
    setColour (TextButton::textColourOnId, colours::bg);
    setColour (ComboBox::backgroundColourId, colours::panel);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, colours::line);
    setColour (ComboBox::arrowColourId, colours::dim);
    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::line);
    setColour (ToggleButton::textColourId, colours::text);
    setColour (ToggleButton::tickColourId, colours::centre);
}

void MurmurLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto accent = s.findColour (Slider::rotarySliderFillColourId);
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);

    // track
    Path track;
    track.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, start, end, true);
    g.setColour (colours::line);
    g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc
    Path val;
    val.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, start, angle, true);
    g.setColour (accent);
    g.strokePath (val, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // body
    const float br = r * 0.68f;
    g.setColour (colours::panel.brighter (0.08f));
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (colours::line);
    g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.0f);

    // pointer
    const Point<float> tip (c.x + std::sin (angle) * br * 0.82f, c.y - std::cos (angle) * br * 0.82f);
    const Point<float> base (c.x + std::sin (angle) * br * 0.3f, c.y - std::cos (angle) * br * 0.3f);
    g.setColour (colours::text);
    g.drawLine ({ base, tip }, 2.5f);
}

void MurmurLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (34.0f).withSizeKeepingCentre (30.0f, 16.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::centre : colours::line);
    g.fillRoundedRectangle (box, 8.0f);
    const float kx = on ? box.getRight() - 14.0f : box.getX() + 2.0f;
    g.setColour (on ? colours::bg : colours::dim);
    g.fillEllipse (kx, box.getY() + 2.0f, 12.0f, 12.0f);
    g.setColour (highlighted ? colours::text : colours::text.withAlpha (0.9f));
    g.setFont (FontOptions (14.0f, Font::bold));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6.0f), Justification::centredLeft);
}

void MurmurLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto accent = b.findColour (TextButton::buttonOnColourId);
    g.setColour (on ? accent : (highlighted || down ? colours::line : colours::panel));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? accent : colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void MurmurLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 2, ax + 5, ay - 2, ax, ay + 4);
    g.setColour (colours::dim);
    g.fillPath (arrow);
}

// =====================================================================================
// Knob
// =====================================================================================
Knob::Knob (AudioProcessorValueTreeState& s, const String& paramID, const String& caption, Colour accent)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 90, 18);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxTextColourId, colours::text);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);

    label.setText (caption, dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setFont (FontOptions (13.0f, Font::bold));
    label.setColour (Label::textColourId, colours::dim);
    addAndMakeVisible (label);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramID, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (20));
    const bool hasBox = slider.getTextBoxPosition() != Slider::NoTextBox;
    const int box = hasBox ? 22 : 0;
    const int size = jmin (r.getWidth(), r.getHeight() - box, 150);
    slider.setBounds (r.withSizeKeepingCentre (r.getWidth(), size + box));
}

// =====================================================================================
// Segmented
// =====================================================================================
Segmented::Segmented (AudioProcessorValueTreeState& s, const String& paramID, Colour accent)
{
    auto* param = s.getParameter (paramID);
    auto* choice = dynamic_cast<AudioParameterChoice*> (param);
    jassert (choice != nullptr);

    for (int i = 0; i < choice->choices.size(); ++i)
    {
        auto* b = buttons.add (new TextButton (choice->choices[i]));
        b->setClickingTogglesState (false);
        b->setColour (TextButton::buttonOnColourId, accent);
        b->onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }

    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v)
    {
        const int idx = (int) std::lround (v);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, dontSendNotification);
    });
    attachment->sendInitialUpdate();
}

void Segmented::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / jmax (1, buttons.size());
    for (auto* b : buttons)
        b->setBounds (r.removeFromLeft (w).reduced (2, 0));
}

// =====================================================================================
// Footswitch
// =====================================================================================
Footswitch::Footswitch (AudioProcessorValueTreeState& s, const String& paramID, const String& t, Colour a, Style st)
    : state (s), text (t), accent (a), style (st)
{
    param = s.getParameter (paramID);
    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void Footswitch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (pressed ? colours::line : colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (on ? accent : colours::line);
    g.drawRoundedRectangle (r, 10.0f, on ? 2.0f : 1.0f);

    // LED
    const float led = 12.0f;
    const auto lc = Point<float> (r.getX() + 22.0f, r.getCentreY());
    if (on)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillEllipse (lc.x - led, lc.y - led, led * 2, led * 2);
    }
    g.setColour (on ? accent : colours::line.brighter (0.2f));
    g.fillEllipse (lc.x - led * 0.5f, lc.y - led * 0.5f, led, led);

    g.setColour (on ? colours::text : colours::dim);
    g.setFont (FontOptions (15.0f, Font::bold));
    g.drawText (text, r.withTrimmedLeft (36.0f), Justification::centredLeft);

    if (style == Style::Hold)
    {
        const bool latch = state.getRawParameterValue (ParamIDs::latch)->load() > 0.5f;
        g.setColour (colours::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText (latch ? "tap" : "hold", r.withTrimmedRight (10.0f), Justification::centredRight);
    }
}

void Footswitch::mouseDown (const MouseEvent&)
{
    pressed = true;
    const bool latch = state.getRawParameterValue (ParamIDs::latch)->load() > 0.5f;
    if (style == Style::Toggle || latch)
    {
        attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
    }
    else
    {
        holding = true;
        attachment->beginGesture();
        attachment->setValueAsPartOfGesture (1.0f);
    }
    repaint();
}

void Footswitch::mouseUp (const MouseEvent&)
{
    pressed = false;
    if (holding)
    {
        attachment->setValueAsPartOfGesture (0.0f);
        attachment->endGesture();
        holding = false;
    }
    repaint();
}

// =====================================================================================
// Buffer view
// =====================================================================================
void BufferView::refresh()
{
    const auto& loop = proc.getEngine().getLoop();
    if (! loop.isReady())
        return; // the host hasn't prepared the engine yet
    playing = loop.isPlaying();
    dubbing = playing && proc.apvts.getRawParameterValue (ParamIDs::overdub)->load() > 0.5f;
    const int len = std::max (1, loop.length());
    const int origin = loop.origin();
    const int filled = loop.filledSamples();
    fill = playing ? 1.0f : (float) filled / (float) len;
    playFrac = loop.positionFraction();

    const float* l = loop.data (0);
    const float* r = loop.data (1);
    const int cols = (int) peaks.size();
    const int span = std::max (1, len / cols);
    const int stride = std::max (1, span / 48);
    const int shownLen = playing ? len : std::max (1, filled);
    const int shownStart = playing ? origin : (filled < len ? 0 : origin);

    for (int c = 0; c < cols; ++c)
    {
        float pk = 0.0f;
        const int s0 = (int) ((int64_t) c * shownLen / cols);
        const int s1 = (int) ((int64_t) (c + 1) * shownLen / cols);
        for (int s = s0; s < s1; s += stride)
        {
            const int k = (shownStart + s) % len;
            pk = std::max (pk, std::max (std::fabs (l[k]), std::fabs (r[k])));
        }
        peaks[(size_t) c] = peaks[(size_t) c] * 0.5f + std::min (1.0f, pk) * 0.5f;
    }
    repaint();
}

void BufferView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    auto inner = r.reduced (14.0f, 12.0f);
    auto header = inner.removeFromTop (18.0f);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.setColour (colours::dim);
    g.drawText ("MICRO-LOOPER BUFFER", header, Justification::centredLeft);

    const String stateText = dubbing ? "DUBBING" : (playing ? "PLAYING" : "LISTENING");
    const Colour stateCol = dubbing ? colours::wet : (playing ? colours::loop : colours::dim);
    g.setColour (stateCol);
    g.drawText (stateText, header, Justification::centredRight);

    inner.removeFromTop (6.0f);
    const float mid = inner.getCentreY();
    const float half = inner.getHeight() * 0.5f;
    const int cols = (int) peaks.size();
    const float cw = inner.getWidth() / (float) cols;
    const float shownW = playing ? inner.getWidth() : inner.getWidth() * jlimit (0.0f, 1.0f, fill);

    for (int c = 0; c < cols; ++c)
    {
        const float x = inner.getX() + (float) c * cw * (shownW / inner.getWidth());
        const float hgt = std::max (1.0f, peaks[(size_t) c] * half * 1.6f);
        g.setColour ((playing ? colours::loop : colours::dim).withAlpha (0.75f));
        g.fillRect (x, mid - std::min (half, hgt), std::max (1.0f, cw * (shownW / inner.getWidth()) - 1.0f), std::min (half, hgt) * 2.0f);
    }

    if (playing)
    {
        const float px = inner.getX() + jlimit (0.0f, 1.0f, playFrac) * inner.getWidth();
        g.setColour (colours::text);
        g.fillRect (px - 1.0f, inner.getY(), 2.0f, inner.getHeight());
    }
}

// =====================================================================================
// Switch / Picker
// =====================================================================================
Switch::Switch (AudioProcessorValueTreeState& s, const String& paramID, const String& name, const String& h)
{
    button.setButtonText (name);
    addAndMakeVisible (button);
    help.setText (h, dontSendNotification);
    help.setFont (FontOptions (12.0f));
    help.setColour (Label::textColourId, colours::dim);
    addAndMakeVisible (help);
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, paramID, button);
}

void Switch::resized()
{
    auto r = getLocalBounds();
    button.setBounds (r.removeFromTop (24));
    help.setBounds (r.withTrimmedLeft (36));
}

Picker::Picker (AudioProcessorValueTreeState& s, const String& paramID, const String& name)
{
    label.setText (name, dontSendNotification);
    label.setFont (FontOptions (13.0f, Font::bold));
    label.setColour (Label::textColourId, colours::dim);
    addAndMakeVisible (label);
    if (auto* c = dynamic_cast<AudioParameterChoice*> (s.getParameter (paramID)))
        box.addItemList (c->choices, 1);
    addAndMakeVisible (box);
    attachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, paramID, box);
}

void Picker::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (20));
    box.setBounds (r.removeFromTop (30));
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;

MurmurEditor::MurmurEditor (MurmurProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      wetMode (p.apvts, ParamIDs::wetMode, colours::wet),
      loopMode (p.apvts, ParamIDs::loopMode, colours::loop),
      routing (p.apvts, ParamIDs::routing, colours::centre),
      time (p.apvts, ParamIDs::time, "TIME", colours::wet),
      wetModify (p.apvts, ParamIDs::wetModify, "MODIFY", colours::wet),
      mix (p.apvts, ParamIDs::mix, "MIX", colours::centre),
      clock (p.apvts, ParamIDs::clock, "CLOCK", colours::centre),
      length (p.apvts, ParamIDs::length, "LENGTH", colours::loop),
      loopModify (p.apvts, ParamIDs::loopModify, "MODIFY", colours::loop),
      wetSwitch (p.apvts, ParamIDs::wetOn, "WET", colours::wet, Footswitch::Style::Toggle),
      freezeSwitch (p.apvts, ParamIDs::freeze, "FREEZE", colours::wet, Footswitch::Style::Hold),
      loopSwitch (p.apvts, ParamIDs::loopOn, "LOOP", colours::loop, Footswitch::Style::Toggle),
      dubSwitch (p.apvts, ParamIDs::overdub, "DUB", colours::loop, Footswitch::Style::Hold),
      bufferView (p),
      width (p.apvts, ParamIDs::width, "STEREO WIDTH", colours::centre),
      tone (p.apvts, ParamIDs::tone, "TONE", colours::wet),
      fade (p.apvts, ParamIDs::fade, "FADE", colours::loop),
      direct (p.apvts, ParamIDs::direct, "DIRECT LOOP", colours::loop),
      balance (p.apvts, ParamIDs::balance, "LEVEL BALANCE", colours::centre),
      inGain (p.apvts, ParamIDs::inGain, "INPUT", colours::centre),
      outGain (p.apvts, ParamIDs::outGain, "OUTPUT", colours::centre),
      bufferSeconds (p.apvts, ParamIDs::bufferSeconds, "MAX BUFFER", colours::loop),
      sync (p.apvts, ParamIDs::sync, "CHANNEL SYNC"),
      spreadSolo (p.apvts, ParamIDs::spreadSolo, "SPREAD SOLO"),
      stepSet (p.apvts, ParamIDs::stepSet, "CLOCK STEPS")
{
    setLookAndFeel (&lnf);

    for (auto* t : { &panelTab, &deepTab })
    {
        t->setColour (TextButton::buttonOnColourId, colours::centre);
        addAndMakeVisible (t);
    }
    panelTab.onClick = [this] { showPage (0); };
    deepTab.onClick = [this] { showPage (1); };

    for (Component* c : std::initializer_list<Component*> { &wetMode, &loopMode, &routing, &time, &wetModify, &mix, &clock,
                                                             &length, &loopModify, &wetSwitch, &freezeSwitch, &loopSwitch,
                                                             &dubSwitch, &bufferView, &clockReadout })
        addChildComponent (c);

    clockReadout.setJustificationType (Justification::centred);
    clock.slider.setColour (Slider::textBoxTextColourId, Colours::transparentBlack); // the readout replaces it
    clockReadout.setFont (FontOptions (12.5f));
    clockReadout.setColour (Label::textColourId, colours::text);

    for (Component* c : std::initializer_list<Component*> { &width, &tone, &fade, &direct, &balance, &inGain, &outGain,
                                                             &bufferSeconds, &sync, &spreadSolo, &stepSet })
        addChildComponent (c);

    struct S { const char* id; const char* name; const char* help; };
    const S defs[] = {
        { ParamIDs::spread, "Spread", "Each mode makes its own stereo image" },
        { ParamIDs::miso, "MISO", "Mono in, stereo out" },
        { ParamIDs::trails, "Trails", "Wet tails ring out after bypass" },
        { ParamIDs::dryKill, "Dry Kill", "Remove the dry signal" },
        { ParamIDs::latch, "Latch", "Freeze and Dub tap on and off" },
        { ParamIDs::noDub, "No Dub (Refresh)", "Hold Dub for a live, delayed looper" },
        { ParamIDs::smoothClock, "Smooth Clock", "Clock glides instead of stepping" },
        { ParamIDs::halfLength, "Half Length", "Halve the loop, like the original" },
        { ParamIDs::classic, "Classic", "Bring back the MKI noise and flaws" },
    };
    for (const auto& d : defs)
        addChildComponent (switches.add (new Switch (p.apvts, d.id, d.name, d.help)));

    setSize (960, 640);
    showPage (0);
    startTimerHz (30);
}

MurmurEditor::~MurmurEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MurmurEditor::showPage (int newPage)
{
    page = newPage;
    panelTab.setToggleState (page == 0, dontSendNotification);
    deepTab.setToggleState (page == 1, dontSendNotification);

    for (Component* c : std::initializer_list<Component*> { &wetMode, &loopMode, &routing, &time, &wetModify, &mix, &clock,
                                                             &length, &loopModify, &wetSwitch, &freezeSwitch, &loopSwitch,
                                                             &dubSwitch, &clockReadout })
        c->setVisible (page == 0);
    bufferView.setVisible (true);

    for (Component* c : std::initializer_list<Component*> { &width, &tone, &fade, &direct, &balance, &inGain, &outGain,
                                                             &bufferSeconds, &sync, &spreadSolo, &stepSet })
        c->setVisible (page == 1);
    for (auto* s : switches)
        s->setVisible (page == 1);

    resized();
    repaint();
}

void MurmurEditor::paint (Graphics& g)
{
    g.fillAll (colours::bg);

    // header
    g.setColour (colours::text);
    g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText ("MURMUR", 24, 14, 200, 32, Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (FontOptions (13.0f));
    g.drawText ("micro-loops and moody spaces", 160, 18, 260, 26, Justification::centredLeft);

    auto section = [&] (Rectangle<int> r, const String& title, Colour accent)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 12.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 12.0f, 1.0f);
        g.setColour (accent);
        g.fillRoundedRectangle ((float) r.getX() + 16, (float) r.getY() + 18, 4.0f, 14.0f, 2.0f);
        g.setColour (colours::text);
        g.setFont (FontOptions (14.0f, Font::bold));
        g.drawText (title, r.getX() + 28, r.getY() + 12, 220, 26, Justification::centredLeft);
    };

    if (page == 0)
    {
        section (wetArea, "WET CHANNEL", colours::wet);
        section (centreArea, "SHARED", colours::centre);
        section (loopArea, "MICRO-LOOPER", colours::loop);
    }
    else
    {
        section (deepArea, "HIDDEN OPTIONS AND SWITCHES", colours::centre);
    }
}

void MurmurEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    auto header = r.removeFromTop (40);
    deepTab.setBounds (header.removeFromRight (90).reduced (2, 4));
    panelTab.setBounds (header.removeFromRight (90).reduced (2, 4));
    r.removeFromTop (12);

    auto bottom = r.removeFromBottom (130);
    bufferView.setBounds (bottom);
    r.removeFromBottom (14);

    if (page == 0)
    {
        const int colW = (r.getWidth() - 28) / 3;
        wetArea = r.removeFromLeft (colW);
        r.removeFromLeft (14);
        centreArea = r.removeFromLeft (colW);
        r.removeFromLeft (14);
        loopArea = r;

        auto layoutChannel = [] (Rectangle<int> area, Component& modeSel, Component& k1, Component& k2, Component& sw1, Component& sw2)
        {
            auto a = area.reduced (16);
            a.removeFromTop (34);
            modeSel.setBounds (a.removeFromTop (30));
            a.removeFromTop (12);
            auto switches = a.removeFromBottom (52);
            a.removeFromBottom (10);
            const int kw = a.getWidth() / 2;
            k1.setBounds (a.removeFromLeft (kw).reduced (4, 0));
            k2.setBounds (a.reduced (4, 0));
            const int sw = (switches.getWidth() - 10) / 2;
            sw1.setBounds (switches.removeFromLeft (sw));
            switches.removeFromLeft (10);
            sw2.setBounds (switches);
        };
        layoutChannel (wetArea, wetMode, time, wetModify, wetSwitch, freezeSwitch);
        layoutChannel (loopArea, loopMode, length, loopModify, loopSwitch, dubSwitch);

        auto c = centreArea.reduced (16);
        c.removeFromTop (34);
        routing.setBounds (c.removeFromBottom (30));
        c.removeFromBottom (8);
        auto routingLabelSpace = c.removeFromBottom (0);
        juce::ignoreUnused (routingLabelSpace);
        const int kw = c.getWidth() / 2;
        mix.setBounds (c.removeFromLeft (kw).reduced (4, 0));
        clock.setBounds (c.reduced (4, 0));
        // the readout sits where the Clock's value box would be
        const auto sb = clock.slider.getBounds() + clock.getPosition();
        clockReadout.setBounds (clock.getX() - 20, sb.getBottom() - 22, clock.getWidth() + 30, 22);
    }
    else
    {
        deepArea = r;
        auto a = r.reduced (16);
        a.removeFromTop (34);

        auto knobs = a.removeFromTop (120);
        Component* ks[] = { &width, &tone, &fade, &direct, &balance, &inGain, &outGain, &bufferSeconds };
        const int kw = knobs.getWidth() / 8;
        for (auto* k : ks)
            k->setBounds (knobs.removeFromLeft (kw).reduced (4, 0));

        a.removeFromTop (12);
        auto pickers = a.removeFromTop (54);
        const int pw = (pickers.getWidth() - 40) / 3;
        sync.setBounds (pickers.removeFromLeft (pw));
        pickers.removeFromLeft (20);
        spreadSolo.setBounds (pickers.removeFromLeft (pw));
        pickers.removeFromLeft (20);
        stepSet.setBounds (pickers);

        a.removeFromTop (14);
        const int cols = 3;
        const int sw = a.getWidth() / cols;
        const int sh = 46;
        for (int i = 0; i < switches.size(); ++i)
            switches[i]->setBounds (a.getX() + (i % cols) * sw, a.getY() + (i / cols) * sh, sw - 12, sh);
    }
}

void MurmurEditor::timerCallback()
{
    bufferView.refresh();

    if (page != 0)
        return;

    // Captions follow the selected modes, like the pedal's printed legends
    const int wm = (int) proc.apvts.getRawParameterValue (ParamIDs::wetMode)->load();
    const int lm = (int) proc.apvts.getRawParameterValue (ParamIDs::loopMode)->load();
    static const char* wetTime[] = { "TIME  (decay)", "TIME  (delay)", "TIME  (size)" };
    static const char* wetMod[] = { "MODIFY  (smear)", "MODIFY  (feedback)", "MODIFY  (speed)" };
    static const char* loopLen[] = { "LENGTH  (slice)", "LENGTH  (loop)", "LENGTH  (slice)" };
    static const char* loopMod[] = { "MODIFY  (sensitivity)", "MODIFY  (speed)", "MODIFY  (stretch)" };
    time.setCaption (wetTime[jlimit (0, 2, wm)]);
    wetModify.setCaption (wetMod[jlimit (0, 2, wm)]);
    length.setCaption (loopLen[jlimit (0, 2, lm)]);
    loopModify.setCaption (loopMod[jlimit (0, 2, lm)]);

    // Clock readout as rate plus the musical interval below 64 kHz
    const float hz = proc.getEngine().uiClockHz.load();
    const float semis = 12.0f * std::log2 (hz / 64000.0f);
    String interval;
    const int rounded = (int) std::lround (semis);
    if (std::abs (semis - (float) rounded) > 0.15f)
        interval = String (semis, 1) + " st";
    else if (rounded == 0)
        interval = "unison";
    else
    {
        const int down = -rounded;
        const int oct = down / 12, rem = down % 12;
        static const char* names[] = { "", "a semitone", "a tone", "a minor 3rd", "a major 3rd", "a 4th", "a tritone",
                                       "a 5th", "a minor 6th", "a major 6th", "a minor 7th", "a major 7th" };
        if (rem == 0)
            interval = "down " + String (oct) + (oct == 1 ? " octave" : " octaves");
        else
            interval = "down " + (oct > 0 ? String (oct) + " oct + " : String()) + names[rem];
    }
    clockReadout.setText (String (hz / 1000.0f, 1) + " kHz  /  " + interval, dontSendNotification);

    wetSwitch.repaint();
    freezeSwitch.repaint();
    dubSwitch.repaint();
}
