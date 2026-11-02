#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette helpers
// =====================================================================================
namespace ff
{
Font font (float height, float kerning)
{
    static Typeface::Ptr face = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                   BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (face).withHeight (height).withKerningFactor (kerning));
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (text, r, j, false);
}
} // namespace ff

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }

// =====================================================================================
// FlipCard
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f)
    {
        progress = jmin (1.0f, progress + (float) (dt / 0.26));
        repaint();
    }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY();
    const float halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;

    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));

    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawText (text, card, Justification::centred, false);

    if (shade)
    {
        g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale)));
        g.fillRoundedRectangle (card, 7.0f);
    }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (outer.getHeight() > 70.0f ? 6.0f : 5.0f);

    if (progress >= 1.0f)
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, current, false, 1.0f, false);
    }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }

    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills
// =====================================================================================
Pills::Pills (StringArray l) : labels (std::move (l))
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l) : Pills (std::move (l))
{
    auto* p = s.getParameter (paramId);
    jassert (p != nullptr);
    // Choice parameters map index to value. Bool parameters list "on" first (e.g. HALF / FULL).
    const bool isBool = dynamic_cast<AudioParameterBool*> (p) != nullptr;
    attachment = std::make_unique<ParameterAttachment> (*p, [this, isBool] (float v)
    {
        selected = isBool ? (v > 0.5f ? 0 : 1) : (int) std::lround (v);
        repaint();
    });
    onPick = [this, isBool] (int i) { attachment->setValueAsCompleteGesture (isBool ? (i == 0 ? 1.0f : 0.0f) : (float) i); };
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const float gap = 8.0f;
    const float w = ((float) getWidth() - gap * (float) (labels.size() - 1)) / (float) labels.size();
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto b = cell (i).reduced (1.0f);
        const float r = b.getHeight() * 0.5f;
        const bool on = i == selected;
        if (on)
        {
            g.setColour (ff::ink);
            g.fillRoundedRectangle (b, r);
        }
        else
        {
            g.setColour (ff::line);
            g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f);
        }
        g.setColour (on ? ff::bg : ff::ink);
        g.setFont (ff::font (14.0f, 0.06f));
        g.drawText (labels[i], b, Justification::centred, false);
    }
}

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position))
        {
            if (onPick) onPick (i);
            else setSelected (i);
            return;
        }
}

// =====================================================================================
// CardToggle
// =====================================================================================
CardToggle::CardToggle (AudioProcessorValueTreeState& s, const String& paramId, const String& n, Style st)
    : name (n), style (st)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void CardToggle::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto card = style == Style::NameInside ? r : r.removeFromLeft (46.0f);
    g.setColour (on ? ff::ink : ff::card);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (Colours::black.withAlpha (on ? 0.3f : 0.85f));
    if (style == Style::NameInside)
    {
        g.fillRect (card.getX(), card.getCentreY() - 0.75f, 8.0f, 1.5f);
        g.fillRect (card.getRight() - 8.0f, card.getCentreY() - 0.75f, 8.0f, 1.5f);
    }
    else
    {
        g.fillRect (card.getX(), card.getCentreY() - 1.0f, card.getWidth(), 2.0f);
    }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (13.0f, 0.04f));
    g.drawText (style == Style::NameInside ? name : String (on ? "ON" : "OFF"), card, Justification::centred, false);

    if (style == Style::OnOffBeside)
    {
        g.setColour (ff::ink);
        g.setFont (ff::font (15.0f, 0.04f));
        g.drawText (name, r.withTrimmedLeft (10.0f), Justification::centredLeft, false);
    }
}

void CardToggle::mouseDown (const MouseEvent&)
{
    attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

// =====================================================================================
// Look and feel
// =====================================================================================
void FlipLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float left = (float) x + 8.0f, right = (float) (x + w) - 8.0f;
    g.setColour (ff::line);
    g.fillRoundedRectangle (left, cy - 2.0f, right - left, 4.0f, 2.0f);
    g.setColour (ff::ink.withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.85f));
    g.fillRoundedRectangle (left, cy - 2.0f, jmax (0.0f, pos - left), 4.0f, 2.0f);
    g.setColour (ff::ink);
    g.fillEllipse (pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    g.setColour (ff::bg);
    g.fillEllipse (pos - 2.5f, cy - 2.5f, 5.0f, 5.0f);
}

void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;

    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const Point<float> p1 (c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f);
        const Point<float> p2 (c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f);
        g.drawLine ({ p1, p2 }, 2.0f);
    }

    const float a = start + pos * (end - start);
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.68f + r * 0.15f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (a).translated (c));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

void FlipLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool isTap = b.getProperties()["tap"];
    const float rad = isTap ? 10.0f : r.getHeight() * 0.5f;
    g.setColour (down ? ff::card : (isTap ? ff::housing : ff::bg));
    g.fillRoundedRectangle (r, rad);
    g.setColour (highlighted || down ? ff::ink : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), rad, 2.0f);
    if (isTap)
    {
        g.setColour (ff::red);
        const float tw = ff::font (15.0f, 0.1f).getStringWidthFloat (b.getButtonText());
        g.fillEllipse (r.getCentreX() - tw * 0.5f - 18.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
    }
}

void FlipLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const bool isTap = b.getProperties()["tap"];
    g.setColour (ff::ink);
    g.setFont (ff::font (isTap ? 15.0f : (b.getButtonText().length() == 1 ? 18.0f : 13.0f), isTap ? 0.1f : 0.08f));
    auto r = b.getLocalBounds();
    if (isTap) r.removeFromLeft (14);
    g.drawText (b.getButtonText(), r, Justification::centred, false);
}

// =====================================================================================
// Dial
// =====================================================================================
Dial::Dial (AudioProcessorValueTreeState& s, const String& paramId, const String& c) : caption (c)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.onValueChange = [this] { repaint(); };
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (24).withSizeKeepingCentre (80, 80));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), r.removeFromBottom (20), Justification::centred, ff::ink, 15.0f);

    // While ramping or expression moves this knob, a red dot shows where it really is
    if (modActive)
    {
        const auto sb = slider.getBounds().toFloat();
        const auto c = sb.getCentre();
        const float rad = sb.getWidth() * 0.5f + 5.0f;
        const float a = MathConstants<float>::pi * (1.25f + 1.5f * jlimit (0.0f, 1.0f, modPos)) - MathConstants<float>::twoPi;
        g.setColour (ff::red);
        g.fillEllipse (c.x + std::sin (a) * rad - 3.5f, c.y - std::cos (a) * rad - 3.5f, 7.0f, 7.0f);
    }
}

// =====================================================================================
// LabelledSlider
// =====================================================================================
LabelledSlider::LabelledSlider (AudioProcessorValueTreeState& s, const String& paramId, const String& c) : caption (c)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.onValueChange = [this] { repaint(); };
}

void LabelledSlider::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (18));
}

void LabelledSlider::paint (Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (16);
    ff::label (g, caption.toUpperCase(), top);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), top, Justification::centredRight, ff::ink);
}

// =====================================================================================
// ClockFace
// =====================================================================================
// The 11 pedal stops sit evenly round the dial, so the needle is placed by
// interpolating between them: 2, 3, 4, 6, 8, 12, 16, 24, 32, 48 and 64 kHz.
static float dialPosForLog2 (float lg)
{
    static const float L[11] = { -5.0f, -4.415f, -4.0f, -3.415f, -3.0f, -2.415f, -2.0f, -1.415f, -1.0f, -0.415f, 0.0f };
    if (lg <= L[0]) return 0.0f;
    if (lg >= L[10]) return 1.0f;
    for (int i = 0; i < 10; ++i)
        if (lg <= L[i + 1])
            return ((float) i + (lg - L[i]) / (L[i + 1] - L[i])) / 10.0f;
    return 1.0f;
}

ClockFace::ClockFace (AudioProcessorValueTreeState& s, MurmurProcessor& p) : proc (p)
{
    param = s.getParameter (ParamIDs::clock);
    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { value = v; repaint(); });
    attachment->sendInitialUpdate();
    setMouseCursor (MouseCursor::UpDownResizeCursor);
}

void ClockFace::paint (Graphics& g)
{
    const float s = (float) jmin (getWidth(), getHeight()) / 220.0f;
    const Point<float> c (getWidth() * 0.5f, getHeight() * 0.5f);
    auto at = [&] (float deg, float r) { const float a = degreesToRadians (deg); return Point<float> (c.x + std::sin (a) * r * s, c.y - std::cos (a) * r * s); };

    g.setColour (ff::ring);
    g.fillEllipse (c.x - 106 * s, c.y - 106 * s, 212 * s, 212 * s);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 100 * s, c.y - 100 * s, 200 * s, 200 * s);

    const float hz = proc.getEngine().uiClockHz.load();
    const float needlePos = dialPosForLog2 (std::log2 (hz / 64000.0f));
    const int nearest = roundToInt (needlePos * 10.0f);

    static const char* names[11] = { "2", "3", "4", "6", "8", "12", "16", "24", "32", "48", "64" };
    for (int i = 0; i <= 10; ++i)
    {
        const float deg = -135.0f + 27.0f * (float) i;
        g.setColour (ff::ink);
        g.drawLine (Line<float> (at (deg, 86), at (deg, 98)), 4.0f * s);
        const auto lp = at (deg, 70);
        g.setColour (i == nearest ? ff::ink : ff::dim);
        g.setFont (ff::font (15.0f * s, 0.0f));
        g.drawText (names[i], Rectangle<float> (lp.x - 16 * s, lp.y - 9 * s, 32 * s, 18 * s), Justification::centred, false);
    }

    // where the knob is set (a small ink dot on the rim)
    const auto kp = at (-135.0f + 270.0f * value, 103);
    g.setColour (ff::ink);
    g.fillEllipse (kp.x - 3 * s, kp.y - 3 * s, 6 * s, 6 * s);

    // the red second hand shows the real rate, including ramping and Synth Mode
    Path needle;
    needle.addRectangle (-1.4f * s, -90.0f * s, 2.8f * s, 108.0f * s);
    g.setColour (ff::red);
    g.fillPath (needle, AffineTransform::rotation (degreesToRadians (-135.0f + 270.0f * needlePos)).translated (c));
    g.fillEllipse (c.x - 7 * s, c.y - 7 * s, 14 * s, 14 * s);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 2.6f * s, c.y - 2.6f * s, 5.2f * s, 5.2f * s);
}

void ClockFace::mouseDown (const MouseEvent&) { dragStart = value; attachment->beginGesture(); }
void ClockFace::mouseUp (const MouseEvent&) { attachment->endGesture(); }

void ClockFace::mouseDrag (const MouseEvent& e)
{
    const float v = jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 220.0f);
    attachment->setValueAsPartOfGesture (v);
}

void ClockFace::mouseDoubleClick (const MouseEvent&)
{
    attachment->setValueAsCompleteGesture (1.0f);
}

void ClockFace::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    attachment->setValueAsCompleteGesture (jlimit (0.0f, 1.0f, value + (w.deltaY > 0 ? 0.1f : -0.1f)));
}

// =====================================================================================
// Footswitch
// =====================================================================================
Footswitch::Footswitch (AudioProcessorValueTreeState& s, const String& mainId, const String& holdId,
                        const String& n, const String& tapT, const String& holdT, const String& holdingT)
    : state (s), name (n), tapText (tapT), holdText (holdT), holdingText (holdingT)
{
    mainParam = s.getParameter (mainId);
    holdParam = s.getParameter (holdId);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void Footswitch::set (RangedAudioParameter* p, bool v)
{
    p->beginChangeGesture();
    p->setValueNotifyingHost (v ? 1.0f : 0.0f);
    p->endChangeGesture();
}

void Footswitch::mouseDown (const MouseEvent&)
{
    pressed = true;
    held = false;
    downAt = nowSeconds();
    startTimer (20);
    repaint();
}

void Footswitch::timerCallback()
{
    if (pressed && ! held && nowSeconds() - downAt > 0.35)
    {
        held = true;
        const bool latch = state.getRawParameterValue (ParamIDs::latch)->load() > 0.5f;
        const bool holdOn = holdParam->getValue() > 0.5f;
        set (holdParam, latch ? ! holdOn : true);
        if (mainParam->getValue() < 0.5f) set (mainParam, true);
        repaint();
    }
}

void Footswitch::mouseUp (const MouseEvent&)
{
    stopTimer();
    const bool latch = state.getRawParameterValue (ParamIDs::latch)->load() > 0.5f;
    if (! held)
    {
        const bool turnOn = mainParam->getValue() < 0.5f;
        set (mainParam, turnOn);
        if (! turnOn && holdParam->getValue() > 0.5f) set (holdParam, false);
    }
    else if (! latch)
    {
        set (holdParam, false);
    }
    pressed = held = false;
    repaint();
}

void Footswitch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = mainParam->getValue() > 0.5f;
    const bool holding = holdParam->getValue() > 0.5f;

    g.setColour (pressed ? ff::card : ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (on ? ff::ink : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    const Point<float> led (r.getX() + 25.0f, r.getCentreY());
    if (on)
    {
        const float pulse = holding ? 0.5f + 0.5f * (float) std::sin (nowSeconds() * 8.0) : 1.0f;
        g.setColour (ff::red.withAlpha (0.22f * pulse));
        g.fillEllipse (led.x - 12.0f, led.y - 12.0f, 24.0f, 24.0f);
        g.setColour (ff::red);
    }
    else
    {
        g.setColour (ff::pin);
    }
    g.fillEllipse (led.x - 7.0f, led.y - 7.0f, 14.0f, 14.0f);

    g.setColour (on ? ff::ink : ff::dim);
    g.setFont (ff::font (20.0f, 0.08f));
    g.drawText (name, r.withTrimmedLeft (46.0f), Justification::centredLeft, false);

    auto cap = r.withTrimmedRight (16.0f);
    if (holding)
    {
        ff::label (g, holdingText, cap.toNearestInt(), Justification::centredRight, ff::ink, 14.0f);
    }
    else
    {
        const bool latch = state.getRawParameterValue (ParamIDs::latch)->load() > 0.5f;
        g.setColour (ff::dim);
        g.setFont (ff::font (12.0f, 0.06f));
        g.drawText (tapText, cap.withTrimmedBottom (cap.getHeight() * 0.5f).withTrimmedTop (8.0f), Justification::centredRight, false);
        g.drawText (latch ? holdText.replace ("HOLD", "HOLD TO LATCH") : holdText,
                    cap.withTrimmedTop (cap.getHeight() * 0.5f).withTrimmedBottom (8.0f), Justification::centredRight, false);
    }
}

// =====================================================================================
// PresetToggle
// =====================================================================================
PresetToggle::PresetToggle (MurmurProcessor& p) : proc (p)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

Rectangle<float> PresetToggle::cell (int i) const
{
    const float w[3] = { 40.0f, 52.0f, 40.0f };
    float x = 0;
    for (int k = 0; k < i; ++k) x += w[k] + 4.0f;
    return { x, 0.0f, w[i], (float) getHeight() - 8.0f };
}

void PresetToggle::paint (Graphics& g)
{
    const int sel = (int) std::lround (proc.apvts.getRawParameterValue (ParamIDs::presetSlot)->load());
    static const char* names[3] = { "A", "LIVE", "B" };
    for (int i = 0; i < 3; ++i)
    {
        auto b = cell (i);
        const bool on = i == sel;
        const bool flash = i == savedCell && nowSeconds() < savedFlashUntil;
        g.setColour (flash ? ff::red : (on ? ff::ink : ff::card));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (Colours::black.withAlpha (on ? 0.3f : 0.85f));
        g.fillRect (b.getX(), b.getCentreY() - 0.75f, 7.0f, 1.5f);
        g.fillRect (b.getRight() - 7.0f, b.getCentreY() - 0.75f, 7.0f, 1.5f);
        g.setColour (on && ! flash ? ff::bg : ff::ink);
        g.setFont (ff::font (i == 1 ? 13.0f : 15.0f, 0.06f));
        g.drawText (flash ? "SAVED" : names[i], b, Justification::centred, false);

        if (i != 1 && proc.hasPresetSlot (i)) // a dot under a slot that holds a preset
        {
            g.setColour (ff::dim);
            g.fillEllipse (b.getCentreX() - 2.0f, b.getBottom() + 3.0f, 4.0f, 4.0f);
        }
    }
}

void PresetToggle::mouseDown (const MouseEvent& e)
{
    pressedCell = -1;
    for (int i = 0; i < 3; ++i)
        if (cell (i).contains (e.position)) pressedCell = i;
    downAt = nowSeconds();
    saved = false;
    startTimer (40);
}

void PresetToggle::timerCallback()
{
    if (pressedCell >= 0 && pressedCell != 1 && ! saved && nowSeconds() - downAt > 0.8)
    {
        proc.savePresetSlot (pressedCell);
        saved = true;
        savedCell = pressedCell;
        savedFlashUntil = nowSeconds() + 0.9;
    }
    if (pressedCell < 0 && nowSeconds() > savedFlashUntil)
        stopTimer();
    repaint();
}

void PresetToggle::mouseUp (const MouseEvent&)
{
    if (pressedCell >= 0 && ! saved)
        if (auto* p = proc.apvts.getParameter (ParamIDs::presetSlot))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) pressedCell));
            p->endChangeGesture();
        }
    pressedCell = -1;
    repaint();
}

// =====================================================================================
// WaveCards
// =====================================================================================
WaveCards::WaveCards (AudioProcessorValueTreeState& s)
{
    param = s.getParameter (ParamIDs::rampWave);
    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { value = v; repaint(); });
    attachment->sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void WaveCards::paint (Graphics& g)
{
    const float gap = 6.0f, w = ((float) getWidth() - gap * 4.0f) / 5.0f, h = (float) getHeight();
    const int sel = jlimit (0, 4, roundToInt (value * 4.0f));
    for (int i = 0; i < 5; ++i)
    {
        Rectangle<float> b ((float) i * (w + gap), 0, w, h);
        const bool on = i == sel;
        g.setColour (on ? ff::ink : ff::card);
        g.fillRoundedRectangle (b, 6.0f);

        auto a = b.withSizeKeepingCentre (56.0f, 28.0f);
        auto P = [&] (float x, float y) { return Point<float> (a.getX() + x, a.getY() + y); };
        Path p;
        switch (i)
        {
            case 0: p.startNewSubPath (P (2, 24)); p.lineTo (P (15, 4)); p.lineTo (P (28, 24)); p.lineTo (P (41, 4)); p.lineTo (P (54, 24)); break;
            case 1: p.startNewSubPath (P (2, 24)); p.lineTo (P (2, 4)); p.lineTo (P (15, 4)); p.lineTo (P (15, 24)); p.lineTo (P (28, 24));
                    p.lineTo (P (28, 4)); p.lineTo (P (41, 4)); p.lineTo (P (41, 24)); p.lineTo (P (54, 24)); break;
            case 2: p.startNewSubPath (P (2, 14));
                    for (int k = 1; k <= 52; ++k) p.lineTo (P (2.0f + (float) k, 14.0f - 10.0f * std::sin ((float) k / 26.0f * MathConstants<float>::twoPi)));
                    break;
            case 3: p.startNewSubPath (P (2, 18)); p.lineTo (P (11, 18)); p.lineTo (P (11, 6)); p.lineTo (P (20, 6)); p.lineTo (P (20, 22));
                    p.lineTo (P (29, 22)); p.lineTo (P (29, 10)); p.lineTo (P (38, 10)); p.lineTo (P (38, 24)); p.lineTo (P (47, 24)); p.lineTo (P (47, 4)); p.lineTo (P (54, 4)); break;
            default: p.startNewSubPath (P (2, 18)); p.cubicTo (P (8, 18), P (9, 6), P (15, 6)); p.cubicTo (P (21, 6), P (22, 22), P (28, 22));
                     p.cubicTo (P (34, 22), P (34, 10), P (40, 10)); p.cubicTo (P (47, 10), P (47, 24), P (54, 6)); break;
        }
        g.setColour (on ? ff::bg : ff::ink);
        g.strokePath (p, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void WaveCards::mouseDown (const MouseEvent& e)
{
    const int i = jlimit (0, 4, (int) (e.position.x / ((float) getWidth() / 5.0f)));
    attachment->setValueAsCompleteGesture ((float) i / 4.0f);
}

// =====================================================================================
// BufferStrip and Pebble
// =====================================================================================
void BufferStrip::update (double t, double dt)
{
    now = t;
    const auto& eng = proc.getEngine();
    const auto& loop = eng.getLoop();
    auto& s = proc.apvts;
    const bool wetOn = s.getRawParameterValue (ParamIDs::wetOn)->load() > 0.5f;
    const bool synth = eng.isSynthActive();

    level = eng.uiInputLevel.load();
    if (level > 0.004f) lastSound = t;

    if (! loop.isReady())
        return;

    playing = loop.isPlaying();
    dubbing = playing && s.getRawParameterValue (ParamIDs::overdub)->load() > 0.5f;
    frozen = wetOn && s.getRawParameterValue (ParamIDs::freeze)->load() > 0.5f;
    asleep = ! playing && ! frozen && ! synth && t - lastSound > 4.0;

    const int len = std::max (1, loop.length());
    const int filled = loop.filledSamples();
    fill = playing ? 1.0f : (float) filled / (float) len;
    playFrac = loop.positionFraction();

    // bars
    const float* l = loop.data (0);
    const float* r = loop.data (1);
    const int cols = (int) peaks.size();
    const int shownLen = playing ? len : std::max (1, filled);
    const int shownStart = playing ? loop.origin() : (filled < len ? 0 : loop.origin());
    const int stride = std::max (1, shownLen / cols / 64);
    for (int c = 0; c < cols; ++c)
    {
        float pk = 0.0f;
        const int s0 = (int) ((int64_t) c * shownLen / cols), s1 = (int) ((int64_t) (c + 1) * shownLen / cols);
        for (int k = s0; k < s1; k += stride)
        {
            const int idx = (shownStart + k) % len;
            pk = std::max (pk, std::max (std::fabs (l[idx]), std::fabs (r[idx])));
        }
        peaks[(size_t) c] += (std::min (1.0f, pk * 1.8f) - peaks[(size_t) c]) * 0.35f;
    }

    // Pebble's face
    const float lookTarget = playing ? playFrac * 2.0f - 1.0f : (level > 0.004f ? 0.8f : 0.0f);
    look += (lookTarget - look) * (float) (1.0 - std::exp (-dt / 0.12));
    const float mouthTarget = dubbing ? 1.0f : jlimit (0.0f, 1.0f, (level - 0.02f) * 5.0f);
    mouth += (mouthTarget - mouth) * (float) (1.0 - std::exp (-dt / 0.06));

    if (synth && eng.synthNote() != lastNote) { hop = 1.0f; lastNote = eng.synthNote(); }
    if (! synth) lastNote = -1;
    hop *= (float) std::exp (-dt / 0.12);

    if (t > nextBlink)
    {
        blinkUntil = t + 0.13;
        nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.0;
    }
    blinking = t < blinkUntil;

    const double coreHz = eng.uiClockHz.load();
    const double secs = (double) (playing ? len : filled) / std::max (1000.0, coreHz);
    status = asleep ? "ASLEEP" : dubbing ? "DUBBING" : playing ? "PLAYING" : frozen ? "FROZEN" : "LISTENING";
    detail = String (secs, 1) + (playing ? " S LOOP" : " S HEARD");
    repaint();
}

void BufferStrip::paintPebble (Graphics& g, Rectangle<float> a)
{
    // Pebble from Flip Fella, drawn in its 200 unit space and scaled to fit.
    const float s = a.getWidth() / 140.0f;
    const float speed = playing ? 5.0f : 2.4f;
    const float bob = asleep ? 0.0f : (playing ? 3.0f : 1.5f) * (float) std::sin (now * speed);
    const float breathe = asleep ? 1.0f + 0.025f * (float) std::sin (now * 1.6) : 1.0f;
    const float dy = bob - hop * 14.0f;
    const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s * breathe, s / breathe)
                        .translated (a.getCentreX(), a.getCentreY() + 6.0f * s);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col)
    {
        Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2);
        g.setColour (col); g.fillPath (p, tf);
    };
    auto stroke = [&] (Path& p, float w) { g.setColour (ff::faceInk); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf); };

    ell (107, 109 + dy, 58, 48, ff::blobShade);
    ell (100, 100 + dy, 58, 48, ff::blob);

    // eyes
    if (asleep || blinking)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 12, 94 + dy); lid.quadraticTo (ex, 102 + dy, ex + 12, 94 + dy);
            stroke (lid, 6.0f);
        }
    }
    else if (frozen && ! playing)
    {
        for (float ex : { 80.0f, 120.0f }) // happy, squinting
        {
            Path lid; lid.startNewSubPath (ex - 12, 98 + dy); lid.quadraticTo (ex, 84 + dy, ex + 12, 98 + dy);
            stroke (lid, 6.0f);
        }
    }
    else
    {
        const float er = dubbing ? 16.5f : 14.0f, pr = dubbing ? 8.0f : 7.0f;
        for (float ex : { 80.0f, 120.0f })
        {
            ell (ex, 92 + dy, er, er, ff::eye);
            ell (ex + look * 5.0f, 94 + dy, pr, pr, ff::faceInk);
        }
    }

    // mouth
    if (asleep)
    {
        Path m; m.startNewSubPath (94, 124 + dy); m.lineTo (106, 124 + dy);
        stroke (m, 6.0f);
    }
    else if (mouth > 0.15f)
    {
        ell (100, 124 + dy, 7.0f + 3.0f * mouth, 4.0f + 8.0f * mouth, ff::faceInk);
    }
    else
    {
        Path m; m.startNewSubPath (90, 120 + dy); m.quadraticTo (100, playing ? 132 + dy : 128 + dy, 110, 120 + dy);
        stroke (m, 6.0f);
    }

    // drifting z's while asleep
    if (asleep)
    {
        g.setFont (ff::font (11.0f + 4.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (a.getRight() - 14.0f + ph * 8.0f, a.getY() + 10.0f - ph * 18.0f, 16.0f, 16.0f),
                        Justification::centred, false);
        }
    }
}

void BufferStrip::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);

    auto inner = r.reduced (18, 14);
    paintPebble (g, inner.removeFromLeft (76).toFloat());
    inner.removeFromLeft (18);

    auto info = inner.removeFromLeft (120);
    ff::label (g, "BUFFER", info.removeFromTop (18));
    ff::label (g, status, info.removeFromTop (30), Justification::centredLeft, ff::ink, 22.0f);
    ff::label (g, detail, info.removeFromTop (18));
    inner.removeFromLeft (18);

    auto bars = inner.withSizeKeepingCentre (inner.getWidth(), 60).toFloat();
    const int cols = (int) peaks.size();
    const float step = bars.getWidth() / (float) cols;
    const float barW = step * 0.58f;
    const int playCol = (int) (playFrac * (float) cols);
    const int fillCol = (int) (fill * (float) cols);
    for (int c = 0; c < cols; ++c)
    {
        const float h = std::max (4.0f, peaks[(size_t) c] * 60.0f);
        Colour col = playing ? (c <= playCol ? ff::ink : ff::faint) : (c < fillCol ? ff::dim : ff::card);
        if (dubbing && c <= playCol) col = ff::ink;
        g.setColour (col);
        g.fillRoundedRectangle (bars.getX() + (float) c * step, bars.getCentreY() - h * 0.5f, barW, h, 2.0f);
    }
    if (playing)
    {
        g.setColour (ff::red);
        g.fillRect (bars.getX() + playFrac * bars.getWidth() - 1.2f, bars.getY(), 2.4f, bars.getHeight());
    }
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;

MurmurEditor::MurmurEditor (MurmurProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      presets (p),
      wetMode (p.apvts, ParamIDs::wetMode, { "REVERB", "DELAY", "SLIP" }),
      loopMode (p.apvts, ParamIDs::loopMode, { "ENV", "TAPE", "STRETCH" }),
      routing (p.apvts, ParamIDs::routing, { "IN", "BOTH", "LOOP" }),
      time (p.apvts, ParamIDs::time, "Time"),
      wetModify (p.apvts, ParamIDs::wetModify, "Modify"),
      length (p.apvts, ParamIDs::length, "Length"),
      loopModify (p.apvts, ParamIDs::loopModify, "Modify"),
      clock (p.apvts, p),
      mix (p.apvts, ParamIDs::mix, "Mix"),
      wetSwitch (p.apvts, ParamIDs::wetOn, ParamIDs::freeze, "WET", "TAP ON / OFF", "HOLD FREEZE", "FROZEN"),
      loopSwitch (p.apvts, ParamIDs::loopOn, ParamIDs::overdub, "LOOP", "TAP PLAY / LISTEN", "HOLD OVERDUB", "DUBBING"),
      strip (p),
      width (p.apvts, ParamIDs::width, "Width"),
      tone (p.apvts, ParamIDs::tone, "Tone"),
      fade (p.apvts, ParamIDs::fade, "Fade"),
      direct (p.apvts, ParamIDs::direct, "Direct loop"),
      balance (p.apvts, ParamIDs::balance, "Balance"),
      sync (p.apvts, ParamIDs::sync, { "UNSYNCED", "LOOP FOLLOWS WET", "WET FOLLOWS LOOP" }),
      spreadSolo (p.apvts, ParamIDs::spreadSolo, { "BOTH", "WET", "LOOP" }),
      halfLength (p.apvts, ParamIDs::halfLength, { "HALF", "FULL" }),
      stepSet (p.apvts, ParamIDs::stepSet, { "PEDAL", "OCTAVES", "4THS + 5THS", "SEMITONES" }),
      inGain (p.apvts, ParamIDs::inGain, "Input"),
      outGain (p.apvts, ParamIDs::outGain, "Output"),
      bufferSeconds (p.apvts, ParamIDs::bufferSeconds, "Max loop at 64 kHz"),
      bounce (p.apvts, ParamIDs::bounce, { "BOUNCE", "RAMP" }),
      sweep (p.apvts, ParamIDs::sweep, { "TOP", "BOTTOM" }),
      modSource (p.apvts, ParamIDs::modSource, { "RAMP", "EXPRESSION" }),
      expPolarity (p.apvts, ParamIDs::expPolarity, { "NORMAL", "REVERSED" }),
      tempoSync (p.apvts, ParamIDs::tempoSync, { "OFF", "ABLETON", "TAP" }),
      synthGate (p.apvts, ParamIDs::synthGate, { "DRONE", "GATE", "ADSR" }),
      rampSpeed (p.apvts, ParamIDs::rampSpeed, "Speed"),
      rampWave (p.apvts, ParamIDs::rampWave, "Waveform, slide to morph"),
      expression (p.apvts, ParamIDs::expression, "Expression"),
      waveCards (p.apvts),
      synthEnable (p.apvts, ParamIDs::synthEnable, "SYNTH MODE", CardToggle::Style::NameInside),
      attack (p.apvts, ParamIDs::attack, "Attack"),
      decay (p.apvts, ParamIDs::decay, "Decay"),
      sustain (p.apvts, ParamIDs::sustain, "Sustain"),
      release (p.apvts, ParamIDs::release, "Release"),
      glide (p.apvts, ParamIDs::glide, "Glide")
{
    setLookAndFeel (&lnf);
    lnf.setColour (TextButton::buttonColourId, ff::bg);
    lnf.setColour (TextButton::textColourOffId, ff::ink);
    lnf.setColour (ComboBox::outlineColourId, ff::line);

    addAndMakeVisible (pages);
    pages.onPick = [this] (int i) { showPage (i); };
    addAndMakeVisible (presets);

    wetCard.setTextHeight (48.0f);
    loopCard.setTextHeight (48.0f);
    rateCard.setTextHeight (38.0f);
    khzCard.setTextHeight (20.0f);
    khzCard.setValue ("KHZ");
    bpmCard.setTextHeight (46.0f);
    noteCard.setTextHeight (36.0f);
    octaveCard.setTextHeight (30.0f);

    panelComps = { &wetCard, &loopCard, &rateCard, &khzCard, &wetMode, &loopMode, &routing, &time, &wetModify,
                   &length, &loopModify, &clock, &mix, &wetSwitch, &loopSwitch, &strip };

    struct Sw { const char* id; const char* name; };
    for (auto s : { Sw { ParamIDs::miso, "MISO" }, Sw { ParamIDs::spread, "SPREAD" }, Sw { ParamIDs::dryKill, "DRY KILL" },
                    Sw { ParamIDs::trails, "TRAILS" }, Sw { ParamIDs::latch, "LATCH" }, Sw { ParamIDs::noDub, "NO DUB" },
                    Sw { ParamIDs::smoothClock, "SMOOTH" }, Sw { ParamIDs::classic, "CLASSIC" } })
        deepComps.add (switches.add (new CardToggle (p.apvts, s.id, s.name, CardToggle::Style::OnOffBeside)));
    for (auto* c : std::initializer_list<Component*> { &width, &tone, &fade, &direct, &balance, &sync, &spreadSolo,
                                                       &halfLength, &stepSet, &resetHidden, &inGain, &outGain, &bufferSeconds })
        deepComps.add (c);
    resetHidden.onClick = [this] { proc.resetHiddenOptions(); };

    for (auto s : { Sw { ParamIDs::rampTime, "TIME" }, Sw { ParamIDs::rampWetMod, "WET MOD" }, Sw { ParamIDs::rampClock, "CLOCK" },
                    Sw { ParamIDs::rampLoopMod, "LOOP MOD" }, Sw { ParamIDs::rampLength, "LENGTH" } })
        motionComps.add (targets.add (new CardToggle (p.apvts, s.id, s.name, CardToggle::Style::NameInside)));
    for (auto* c : std::initializer_list<Component*> { &bounce, &sweep, &rampSpeed, &waveCards, &rampWave, &modSource,
                                                       &expression, &expPolarity, &bpmCard, &tempoSync, &tap, &synthEnable,
                                                       &noteCard, &octaveCard, &synthGate, &attack, &decay, &sustain,
                                                       &release, &glide, &rootDown, &rootUp })
        motionComps.add (c);

    tap.getProperties().set ("tap", true);
    tap.onClick = [this] { proc.tapTempo(); };
    auto shiftRoot = [this] (int semis)
    {
        if (auto* prm = proc.apvts.getParameter (ParamIDs::synthRoot))
        {
            const float v = jlimit (24.0f, 96.0f, prm->convertFrom0to1 (prm->getValue()) + (float) semis);
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    rootDown.onClick = [shiftRoot] { shiftRoot (-12); };
    rootUp.onClick = [shiftRoot] { shiftRoot (12); };

    for (auto* c : panelComps) addChildComponent (c);
    for (auto* c : deepComps) addChildComponent (c);
    for (auto* c : motionComps) addChildComponent (c);

    setSize (960, 640);
    showPage (0);
    lastTick = nowSeconds();
    startTimerHz (60);
}

MurmurEditor::~MurmurEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MurmurEditor::showPage (int p)
{
    page = p;
    pages.setSelected (p);
    for (auto* c : panelComps) c->setVisible (page == 0);
    for (auto* c : deepComps) c->setVisible (page == 1);
    for (auto* c : motionComps) c->setVisible (page == 2);
    resized();
    repaint();
}

void MurmurEditor::section (Graphics& g, Rectangle<int> r) const
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);
}

void MurmurEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.setColour (ff::ink);
    g.setFont (ff::font (26.0f, 0.04f));
    g.drawText ("MURMUR", 20, 20, 140, 40, Justification::centredLeft, false);
    ff::label (g, "MICRO-LOOPS AND MOODY SPACES", { 150, 20, 260, 40 });

    if (page == 0)
    {
        section (g, secA); section (g, secB); section (g, secC);
        ff::label (g, "WET CHANNEL", { 36, 92, 200, 16 });
        ff::label (g, "CLOCK", { 356, 92, 200, 16 });
        ff::label (g, "MICRO-LOOPER", { 656, 92, 200, 16 });
        ff::label (g, interval, { 340, 366, 280, 20 }, Justification::centred, ff::ink, 15.0f);
    }
    else if (page == 1)
    {
        section (g, secA); section (g, secB);
        ff::label (g, "HIDDEN OPTIONS", { 36, 92, 300, 16 });
        ff::label (g, "CHANNEL SYNC", { 36, 266, 300, 16 });
        ff::label (g, "SPREAD ON", { 36, 334, 200, 16 });
        ff::label (g, "LOOP LENGTH", { 377, 334, 200, 16 });
        ff::label (g, "CLOCK STEPS", { 36, 402, 300, 16 });
        ff::label (g, "SWITCHES", { 646, 92, 200, 16 });
        g.setColour (ff::card);
        g.fillRect (646, 300, 278, 2);
        ff::label (g, "LEVELS", { 646, 314, 200, 16 });
    }
    else
    {
        section (g, secA); section (g, secB); section (g, secC);
        ff::label (g, String::fromUTF8 ("RAMPING · KNOBS THAT MOVE"), { 36, 92, 400, 16 });
        ff::label (g, "MOVEMENT", { 36, 172, 200, 16 });
        ff::label (g, "KNOB SETS THE", { 253, 172, 200, 16 });
        g.setColour (ff::card);
        g.fillRect (36, 404, 418, 2);
        ff::label (g, "DRIVEN BY", { 36, 420, 200, 16 });
        ff::label (g, "EXPRESSION PEDAL", { 253, 496, 200, 16 });
        ff::label (g, String::fromUTF8 ("TEMPO SYNC · BPM"), { 634, 92, 290, 16 });
        ff::label (g, String::fromUTF8 ("SYNTH MODE · PLAY MIDI NOTES, MOVE CLOCK TO LEAVE"), { 506, 236, 330, 16 });
        ff::label (g, "NOTE OUTPUT", { 640, 270, 200, 16 });
        ff::label (g, "ORIGINAL PITCH ON", { 727, 548, 140, 16 });
        auto* root = proc.apvts.getParameter (ParamIDs::synthRoot);
        ff::label (g, root->getCurrentValueAsText(), { 727, 548, 197, 16 }, Justification::centredRight, ff::ink);
    }
}

void MurmurEditor::resized()
{
    pages.setBounds (532, 24, 240, 32);
    presets.setBounds (788, 24, 140, 40);

    if (page == 0)
    {
        secA = { 20, 76, 300, 432 };
        secB = { 340, 76, 280, 432 };
        secC = { 640, 76, 300, 432 };

        wetCard.setBounds (36, 120, 268, 84);
        wetMode.setBounds (36, 216, 268, 32);
        time.setBounds (40, 262, 110, 128);
        wetModify.setBounds (190, 262, 110, 128);
        wetSwitch.setBounds (36, 428, 268, 64);

        loopCard.setBounds (656, 120, 268, 84);
        loopMode.setBounds (656, 216, 268, 32);
        length.setBounds (660, 262, 110, 128);
        loopModify.setBounds (810, 262, 110, 128);
        loopSwitch.setBounds (656, 428, 268, 64);

        rateCard.setBounds (403, 116, 84, 56);
        khzCard.setBounds (493, 116, 64, 56);
        clock.setBounds (390, 180, 180, 180);
        routing.setBounds (356, 396, 248, 32);
        mix.setBounds (356, 444, 248, 40);

        strip.setBounds (20, 524, 920, 96);
    }
    else if (page == 1)
    {
        secA = { 20, 76, 590, 544 };
        secB = { 630, 76, 310, 544 };
        resetHidden.setBounds (500, 86, 94, 28);
        int x = 36;
        for (auto* d : { &width, &tone, &fade, &direct, &balance })
        {
            d->setBounds (x, 120, 110, 128);
            x += 112;
        }
        sync.setBounds (36, 288, 558, 32);
        spreadSolo.setBounds (36, 356, 325, 32);
        halfLength.setBounds (377, 356, 217, 32);
        stepSet.setBounds (36, 424, 558, 32);

        for (int i = 0; i < switches.size(); ++i)
            switches[i]->setBounds (646 + (i % 2) * 145, 118 + (i / 2) * 44, 133, 34);
        inGain.setBounds (646, 338, 278, 40);
        outGain.setBounds (646, 392, 278, 40);
        bufferSeconds.setBounds (646, 446, 278, 40);
    }
    else
    {
        secA = { 20, 76, 450, 544 };
        secB = { 490, 76, 450, 124 };
        secC = { 490, 220, 450, 400 };

        for (int i = 0; i < targets.size(); ++i)
            targets[i]->setBounds (36 + i * 85, 118, 79, 40);
        bounce.setBounds (36, 194, 201, 32);
        sweep.setBounds (253, 194, 201, 32);
        rampSpeed.setBounds (36, 244, 418, 40);
        waveCards.setBounds (36, 296, 418, 44);
        rampWave.setBounds (36, 350, 418, 40);
        modSource.setBounds (36, 442, 201, 32);
        expression.setBounds (253, 420, 201, 40);
        expPolarity.setBounds (253, 518, 201, 32);

        bpmCard.setBounds (506, 100, 112, 76);
        tempoSync.setBounds (634, 114, 290, 32);
        tap.setBounds (634, 154, 290, 36);

        synthEnable.setBounds (836, 230, 88, 28);
        noteCard.setBounds (506, 270, 64, 64);
        octaveCard.setBounds (575, 270, 48, 64);
        synthGate.setBounds (640, 292, 284, 32);
        attack.setBounds (506, 360, 199, 40);
        decay.setBounds (725, 360, 199, 40);
        sustain.setBounds (506, 420, 199, 40);
        release.setBounds (725, 420, 199, 40);
        glide.setBounds (506, 480, 199, 40);
        rootDown.setBounds (727, 572, 44, 28);
        rootUp.setBounds (779, 572, 44, 28);
    }
}

void MurmurEditor::timerCallback()
{
    const double now = nowSeconds();
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    auto& s = proc.apvts;
    const auto& eng = proc.getEngine();

    for (auto* c : { &wetCard, &loopCard, &rateCard, &khzCard, &bpmCard, &noteCard, &octaveCard })
        c->advance (dt);

    if (page == 0)
    {
        static const char* wetNames[] = { "REVERB", "DELAY", "SLIP" };
        static const char* loopNames[] = { "ENV", "TAPE", "STRETCH" };
        const int wm = jlimit (0, 2, (int) std::lround (s.getRawParameterValue (ParamIDs::wetMode)->load()));
        const int lm = jlimit (0, 2, (int) std::lround (s.getRawParameterValue (ParamIDs::loopMode)->load()));
        wetCard.setValue (wetNames[wm]);
        loopCard.setValue (loopNames[lm]);

        static const char* timeCap[] = { "Time \xc2\xb7 decay", "Time \xc2\xb7 delay", "Time \xc2\xb7 size" };
        static const char* wetModCap[] = { "Modify \xc2\xb7 smear", "Modify \xc2\xb7 repeats", "Modify \xc2\xb7 speed" };
        static const char* lenCap[] = { "Length \xc2\xb7 slice", "Length \xc2\xb7 loop", "Length \xc2\xb7 slice" };
        static const char* loopModCap[] = { "Modify \xc2\xb7 touch", "Modify \xc2\xb7 speed", "Modify \xc2\xb7 stretch" };
        const bool synced = proc.currentBpm() > 0.0f;
        time.setCaption (synced && wm != 0 ? String::fromUTF8 ("Time · note") : String::fromUTF8 (timeCap[wm]));
        wetModify.setCaption (String::fromUTF8 (wetModCap[wm]));
        length.setCaption (synced && lm == 1 ? String::fromUTF8 ("Length · bar") : String::fromUTF8 (lenCap[lm]));
        loopModify.setCaption (String::fromUTF8 (loopModCap[lm]));

        auto mod = [&] (Dial& d, const char* rampId, const char* knobId, const std::atomic<float>& actual)
        {
            const bool active = s.getRawParameterValue (rampId)->load() > 0.5f;
            juce::ignoreUnused (knobId);
            d.setModulated (actual.load(), active);
            if (active) d.repaint();
        };
        mod (time, ParamIDs::rampTime, ParamIDs::time, eng.uiTime);
        mod (wetModify, ParamIDs::rampWetMod, ParamIDs::wetModify, eng.uiWetMod);
        mod (length, ParamIDs::rampLength, ParamIDs::length, eng.uiLength);
        mod (loopModify, ParamIDs::rampLoopMod, ParamIDs::loopModify, eng.uiLoopMod);

        // Clock cards and the musical interval
        const float hz = eng.uiClockHz.load();
        const float k = hz / 1000.0f;
        const bool whole = std::abs (k - std::round (k)) < 0.05f;
        rateCard.setValue (whole ? String (roundToInt (k)) : String (k, 1));
        const float semis = 12.0f * std::log2 (hz / 64000.0f);
        const int rounded = (int) std::lround (semis);
        if (eng.isSynthActive())
            interval = String::fromUTF8 ("SYNTH · ") + MidiMessage::getMidiNoteName (eng.synthNote(), true, true, 4);
        else if (std::abs (semis - (float) rounded) > 0.15f)
            interval = String (semis, 1) + " SEMITONES";
        else if (rounded >= 0)
            interval = "ORIGINAL PITCH";
        else
        {
            static const char* names[] = { "", "A SEMITONE", "A TONE", "A MINOR 3RD", "A MAJOR 3RD", "A 4TH", "A TRITONE",
                                           "A 5TH", "A MINOR 6TH", "A MAJOR 6TH", "A MINOR 7TH", "A MAJOR 7TH" };
            const int down = -rounded, oct = down / 12, rem = down % 12;
            if (rem == 0) interval = "DOWN " + String (oct) + (oct == 1 ? " OCTAVE" : " OCTAVES");
            else interval = "DOWN " + (oct > 0 ? String (oct) + " OCT + " : String()) + names[rem];
        }

        strip.update (now, dt);
        clock.repaint();
        wetSwitch.repaint();
        loopSwitch.repaint();
        repaint (340, 360, 280, 30);
    }
    else if (page == 2)
    {
        const float bpm = proc.currentBpm();
        bpmCard.setValue (bpm > 0.0f ? String (roundToInt (bpm)) : "--");
        if (eng.isSynthActive())
        {
            const int n = eng.synthNote();
            noteCard.setValue (MidiMessage::getMidiNoteName (n, true, false, 4));
            octaveCard.setValue (String (n / 12 - 1));
        }
        else
        {
            noteCard.setValue ("-");
            octaveCard.setValue ("-");
        }
        repaint (720, 544, 210, 24);
    }
    presets.repaint();
}
