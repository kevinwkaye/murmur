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

String intervalName (int semis)
{
    static const char* names[13] = { "UNISON", "MIN 2ND", "2ND", "MIN 3RD", "3RD", "4TH", "TRITONE",
                                     "5TH", "MIN 6TH", "6TH", "MIN 7TH", "7TH", "OCT" };
    const String minus = "-"; // the Flip Fella font has no true minus glyph
    const int a = std::abs (semis);
    if (a == 0) return "UNISON";
    const String sign = semis > 0 ? "+" : minus;
    if (a == 24) return sign + "2 OCT";
    return sign + names[jlimit (0, 12, a)];
}

String stepText (int semis, bool reverse)
{
    const String minus = "-";
    const String num = semis > 0 ? "+" + String (semis) : (semis < 0 ? minus + String (-semis) : String ("0"));
    return String (reverse ? "REV " : "FWD ") + intervalName (semis) + " " + num;
}

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

    const float hinge = card.getHeight() < 36.0f ? 1.0f : 3.0f;
    g.setColour (Colours::black.withAlpha (card.getHeight() < 36.0f ? 0.7f : 1.0f));
    g.fillRect (card.getX(), card.getCentreY() - hinge * 0.5f, card.getWidth(), hinge);
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
    stepCard.setTextHeight (17.0f);
    addChildComponent (stepCard);
}

void Dial::setStepCard (const String& text)
{
    const bool show = text.isNotEmpty();
    if (show) stepCard.setValue (text);
    if (stepCard.isVisible() != show) { stepCard.setVisible (show); repaint(); }
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (34).withSizeKeepingCentre (80, 80));
    stepCard.setBounds (getLocalBounds().removeFromBottom (32).withSizeKeepingCentre (getWidth(), 32));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred);
    if (! stepCard.isVisible())
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
// Footswitch (on the panel)
// =====================================================================================
Footswitch::Footswitch (MurmurProcessor& p, int s, const String& n, const String& tapT, const String& holdT)
    : proc (p), sw (s), name (n), tapText (tapT), holdText (holdT)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void Footswitch::mouseDown (const MouseEvent&)
{
    pressed = true;
    proc.footDown (sw);
    repaint();
}

void Footswitch::mouseUp (const MouseEvent&)
{
    if (! pressed) return;
    pressed = false;
    proc.footUp (sw);
    repaint();
}

void Footswitch::paint (Graphics& g)
{
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load() > 0.5f; };
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool recording = sw == 1 && v (ParamIDs::loopRec);
    const bool on = sw == 0 ? v (ParamIDs::wetOn) : (v (ParamIDs::loopOn) || recording);
    const bool holding = sw == 0 ? v (ParamIDs::freeze) && on : v (ParamIDs::overdub) && v (ParamIDs::loopOn);

    g.setColour (pressed ? ff::card : ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (on ? ff::ink : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    const Point<float> led (r.getX() + 25.0f, r.getCentreY());
    if (on)
    {
        const float pulse = holding || recording ? 0.5f + 0.5f * (float) std::sin (nowSeconds() * (recording ? 5.0 : 8.0)) : 1.0f;
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
    if (holding || recording)
    {
        ff::label (g, recording ? "RECORDING" : (sw == 0 ? "FROZEN" : "DUBBING"), cap.toNearestInt(), Justification::centredRight, ff::ink, 14.0f);
    }
    else
    {
        const bool latch = v (ParamIDs::latch);
        g.setColour (ff::dim);
        g.setFont (ff::font (12.0f, 0.06f));
        g.drawText (tapText, cap.withTrimmedBottom (cap.getHeight() * 0.5f).withTrimmedTop (8.0f), Justification::centredRight, false);
        g.drawText (latch ? holdText.replace ("HOLD", "HOLD TO LATCH") : holdText,
                    cap.withTrimmedTop (cap.getHeight() * 0.5f).withTrimmedBottom (8.0f), Justification::centredRight, false);
    }
}

// =====================================================================================
// CardButton
// =====================================================================================
CardButton::CardButton()
{
    card.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (card);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void CardButton::mouseUp (const MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onClick) onClick (e);
}

// =====================================================================================
// PedalCard
// =====================================================================================
PedalCard::PedalCard (MurmurProcessor& p, int s) : proc (p), sw (s)
{
    letter.setTextHeight (34.0f);
    letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
    state.setTextHeight (20.0f);
    tap.setTextHeight (15.0f);
    hold.setTextHeight (15.0f);
    for (auto* c : { &letter, &state, &tap, &hold })
    {
        c->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*c);
    }
    addAndMakeVisible (learn);
    learn.onClick = [this]
    {
        if (proc.learningWhat() == sw) proc.cancelLearn();
        else proc.learnFootswitch (sw);
    };
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void PedalCard::resized()
{
    const int w = getWidth();
    letter.setBounds (0, 0, 52, 56);
    state.setBounds (58, 0, w - 58, 56);
    tap.setBounds (0, 80, w, 36);
    hold.setBounds (0, 140, w, 36);
    learn.setBounds (0, 198, w, 28);
}

void PedalCard::paint (Graphics& g)
{
    ff::label (g, "TAP", { 0, 62, 100, 16 }, Justification::centredLeft, ff::dim, 12.0f);
    ff::label (g, "HOLD", { 0, 122, 100, 16 }, Justification::centredLeft, ff::dim, 12.0f);
    ff::label (g, sends, { 0, 180, getWidth(), 14 }, Justification::centred, ff::dim, 12.0f);

    // a red ring round the state card while the switch's job is on, readable from standing height
    const auto st = proc.footState (sw);
    const bool lit = st == "WET ON" || st == "FROZEN" || st == "REC" || st == "PLAYING" || st == "DUBBING";
    if (lit || pressed)
    {
        g.setColour (pressed ? ff::ink : ff::red);
        g.drawRoundedRectangle (state.getBounds().toFloat().expanded (2.0f), 11.0f, 3.0f);
    }
}

void PedalCard::mouseDown (const MouseEvent& e)
{
    if (e.y > 60) return;
    pressed = true;
    proc.footDown (sw);
    repaint();
}

void PedalCard::mouseUp (const MouseEvent&)
{
    if (! pressed) return;
    pressed = false;
    proc.footUp (sw);
    repaint();
}

void PedalCard::tick (double dt, int tableMode)
{
    state.setValue (proc.footState (sw));
    tap.setValue (MurmurProcessor::tapJob (tableMode, sw));
    hold.setValue (MurmurProcessor::holdJob (tableMode, sw));
    for (auto* c : { &letter, &state, &tap, &hold }) c->advance (dt);
    const bool learning = proc.learningWhat() == sw;
    learn.setButtonText (learning ? "PRESS IT" : "LEARN");
    const auto& s = proc.pedals();
    const String t = "CH " + String (s.fsChannel[(size_t) sw]) + String::fromUTF8 (" · CC ") + String (s.fsCC[(size_t) sw]);
    if (t != sends) { sends = t; repaint(); }
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    letter.setAlpha (down ? 0.6f : 1.0f);
    repaint (state.getBounds().expanded (4));
}

// =====================================================================================
// ExpressionPanel
// =====================================================================================
ExpressionPanel::ExpressionPanel (MurmurProcessor& p) : proc (p)
{
    target.setTextHeight (28.0f);
    addAndMakeVisible (target);
    for (auto* b : { &change, &calibrate, &learn }) addAndMakeVisible (*b);
    addAndMakeVisible (curve);
    change.onClick = [this] { targetMenu(); };
    calibrate.onClick = [this] { proc.calibrateExpression(); };
    learn.onClick = [this] { if (proc.learningWhat() == MurmurProcessor::LearnExpression) proc.cancelLearn(); else proc.learnExpression(); };
    curve.onPick = [this] (int i) { proc.pedals().curve = i; proc.pedals().save(); curve.setSelected (i); };
    curve.setSelected (proc.pedals().curve);
}

void ExpressionPanel::resized()
{
    const int w = getWidth();
    target.setBounds (0, 24, w - 88, 52);
    change.setBounds (w - 80, 35, 80, 30);
    calibrate.setBounds (0, 168, (w - 8) / 2, 32);
    learn.setBounds ((w + 8) / 2, 168, (w - 8) / 2, 32);
    curve.setBounds (0, 230, w, 30);
}

void ExpressionPanel::targetMenu()
{
    PopupMenu m;
    const auto& t = names::expTargets();
    const int cur = roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load());
    for (int i = 0; i < (int) t.size(); ++i) m.addItem (100 + i, t[(size_t) i].label, true, i == cur);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&change), [this] (int r)
    {
        if (r >= 100)
            if (auto* prm = proc.apvts.getParameter (ParamIDs::expTarget))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (r - 100)));
                prm->endChangeGesture();
            }
    });
}

void ExpressionPanel::paint (Graphics& g)
{
    ff::label (g, "EXPRESSION MOVES", { 0, 0, getWidth(), 16 });
    const int mode = proc.currentMode();
    ff::label (g, String ("SAVED WITH THE PRESET. ") + (mode == 1 ? "MURK DEFAULT: GLUE" : "MURMUR DEFAULT: CLOCK"),
               { 0, 82, getWidth(), 14 }, Justification::centredLeft, ff::dim, 11.0f);

    const float v = proc.expressionLevel();
    const bool fresh = nowSeconds() - proc.lastExpressionMove() < 1.5;
    ff::label (g, "HEEL", { 0, 104, 60, 16 }); ff::label (g, "TOE", { getWidth() - 60, 104, 60, 16 }, Justification::centredRight);
    const float x0 = 11.0f, w = (float) getWidth() - 22.0f, y = 130.0f;
    g.setColour (ff::line);
    g.fillRoundedRectangle (x0, y, w, 6.0f, 3.0f);
    g.setColour (fresh ? ff::red : ff::dim);
    g.fillRoundedRectangle (x0, y, w * v, 6.0f, 3.0f);
    g.setColour (ff::ink);
    g.fillEllipse (x0 + w * v - 11.0f, y - 8.0f, 22.0f, 22.0f);
    String hint = proc.isCalibrating() ? "ROCK HEEL TO TOE NOW" : (proc.learningWhat() == MurmurProcessor::LearnExpression ? "MOVE THE PEDAL NOW" : "");
    if (hint.isEmpty())
    {
        const auto& t = names::expTargets();
        const int cur = jlimit (0, (int) t.size() - 1, roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load()));
        if (auto* prm = proc.apvts.getParameter (t[(size_t) cur].id)) hint = prm->getCurrentValueAsText().toUpperCase();
    }
    ff::label (g, hint, { 0, 148, getWidth(), 16 }, Justification::centredRight, ff::ink, 13.0f);
    ff::label (g, "CURVE", { 0, 210, 100, 16 });
}

void ExpressionPanel::tick (double dt)
{
    const auto& t = names::expTargets();
    const int cur = jlimit (0, (int) t.size() - 1, roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load()));
    target.setValue (String (t[(size_t) cur].label).toUpperCase());
    target.advance (dt);
    learn.setButtonText (proc.learningWhat() == MurmurProcessor::LearnExpression ? "MOVE IT" : "LEARN");
    curve.setSelected (proc.pedals().curve);
    const float v = proc.expressionLevel();
    if (v != shown || proc.isCalibrating()) { shown = v; repaint(); }
}

// =====================================================================================
// ActionsOverlay
// =====================================================================================
ActionsOverlay::ActionsOverlay()
{
    addAndMakeVisible (close);
}

void ActionsOverlay::resized()
{
    close.setBounds (getWidth() - 40 - 90, 28, 90, 32);
}

void ActionsOverlay::paint (Graphics& g)
{
    g.fillAll (Colour (0xff020202));
    g.setColour (ff::ink);
    g.setFont (ff::font (24.0f, 0.06f));
    g.drawText ("WHAT YOUR FEET DO", 40, 28, 400, 32, Justification::centredLeft, false);
    ff::label (g, String::fromUTF8 ("TAP = QUICK PRESS · HOLD = KEEP IT DOWN"), { 400, 28, getWidth() - 560, 32 }, Justification::centredRight);

    struct Row { const char* sw; const char* murmur; const char* murk; };
    static const Row rows[7] = {
        { "A", "Tap turns the wet sound on or off. Hold to freeze it.", "Same: tap for wet on or off, hold to freeze it." },
        { "B", "Tap to record a loop, tap to play it, tap to stop. Hold to add a layer.", "Same: record, play, stop. Hold to add a layer." },
        { "C", "Tap in the tempo. Hold to save the sound to this preset.", "Same: tap the tempo, hold to save." },
        { "D", "Tap for the next preset. Hold for the previous one.", "Same: tap next, hold previous." },
        { "A+B", "Tap both together: both sounds on or off at once.", "Same." },
        { "B+C", "Hold both for a second: switch to Murk.", "Hold both for a second: switch back to Murmur." },
        { "C+D", "Hold both for a second: Keeper saves this moment.", "Same: Keeper saves this moment." } };

    const int x0 = 40, colW = (getWidth() - 80 - 86) / 2;
    ff::label (g, "IN MURMUR", { x0 + 86, 72, colW, 16 });
    ff::label (g, "IN MURK", { x0 + 86 + colW + 16, 72, colW, 16 });
    int y = 94;
    for (const auto& r : rows)
    {
        g.setColour (ff::face);
        g.fillRoundedRectangle ((float) x0, (float) y, (float) (getWidth() - 80), 54.0f, 10.0f);
        auto key = Rectangle<float> ((float) x0 + 14, (float) y + 9, 56, 36);
        g.setColour (ff::housing);
        g.fillRoundedRectangle (key, 6.0f);
        g.setGradientFill (ColourGradient (ff::cardHi, 0, key.getY(), ff::cardLow, 0, key.getBottom(), false));
        g.fillRoundedRectangle (key.reduced (3.0f), 4.0f);
        g.setColour (Colours::black);
        g.fillRect (key.getX() + 3, key.getCentreY() - 0.5f, key.getWidth() - 6, 1.0f);
        g.setColour (ff::ink);
        g.setFont (ff::font (18.0f, 0.02f));
        g.drawText (r.sw, key, Justification::centred, false);
        g.setFont (ff::font (17.0f, 0.01f));
        g.drawFittedText (r.murmur, x0 + 86, y + 4, colW - 8, 46, Justification::centredLeft, 2);
        g.drawFittedText (r.murk, x0 + 86 + colW + 16, y + 4, colW - 8, 46, Justification::centredLeft, 2);
        y += 62;
    }
    g.setColour (ff::dim);
    g.setFont (ff::font (14.0f, 0.02f));
    g.drawFittedText ("No action needs a double press. Only the two-foot holds take a full second, so they can't fire by accident. "
                      "The expression pedal moves Clock in Murmur and Glue in Murk until you pick something else.",
                      x0, y + 4, getWidth() - 80, 40, Justification::centredLeft, 2);
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
// Pebble
// =====================================================================================
void Pebble::update (double t, double dt, bool isPlaying, bool isDubbing, bool isFrozen, bool isRecording, float level, float lookTarget)
{
    now = t;
    playing = isPlaying; dubbing = isDubbing; frozen = isFrozen; recording = isRecording;
    const auto& eng = proc.getEngine();
    const bool synth = eng.isSynthActive();
    if (level > 0.004f) lastSound = t;
    asleep = ! playing && ! frozen && ! recording && ! synth && t - lastSound > 4.0;

    auto ease = [dt] (float& v, float target, float tau) { v += (target - v) * (float) (1.0 - std::exp (-dt / tau)); };
    ease (look, lookTarget, 0.12f);
    const float mouthTarget = dubbing || recording ? 1.0f : jlimit (0.0f, 1.0f, (level - 0.02f) * 5.0f);
    ease (mouth, mouthTarget, 0.06f);
    ease (murk, proc.currentMode() == 1 ? 1.0f : 0.0f, 0.25f);

    if (synth && eng.synthNote() != lastNote) { hop = 1.0f; lastNote = eng.synthNote(); }
    if (! synth) lastNote = -1;

    // footswitch presses: a little foot tap
    const int foot = proc.uiFootPulse.load();
    if (foot != seenFoot) { seenFoot = foot; tap = 1.0f; lastSound = t; }
    // Keeper: a nod (happy in Murmur, reluctant in Murk)
    const int keep = proc.uiKeepPulse.load();
    if (keep != seenKeep) { seenKeep = keep; nod = 1.0f; lastSound = t; }
    // mode switch: a scowl-and-flip into Murk, a relieved shake back to Murmur
    const int mode = proc.uiModePulse.load();
    if (mode != seenMode) { seenMode = mode; intoMurk = proc.currentMode() == 1; (intoMurk ? flip : shake) = 1.0f; lastSound = t; }
    // the expression pedal: Pebble leans with it
    const bool expFresh = t - proc.lastExpressionMove() < 1.5;
    ease (lean, expFresh ? proc.expressionLevel() * 2.0f - 1.0f : 0.0f, 0.15f);

    const float k = (float) std::exp (-dt / 0.12);
    hop *= k; tap *= (float) std::exp (-dt / 0.09);
    nod *= (float) std::exp (-dt / 0.35);
    flip *= (float) std::exp (-dt / 0.3);
    shake *= (float) std::exp (-dt / 0.35);

    if (t > nextBlink)
    {
        blinkUntil = t + 0.13;
        nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.0;
    }
    blinking = t < blinkUntil;
}

void Pebble::paint (Graphics& g, Rectangle<float> a) const
{
    // Pebble from Flip Fella, drawn in its 200 unit space and scaled to fit
    const float s = a.getWidth() / 140.0f;
    const float speed = playing ? 5.0f : 2.4f;
    const float bob = asleep ? 0.0f : (playing ? 3.0f : 1.5f) * (float) std::sin (now * speed);
    const float breathe = asleep ? 1.0f + 0.025f * (float) std::sin (now * 1.6) : 1.0f;
    const float nodDip = nod * 10.0f * (float) std::sin ((1.0f - nod) * 12.0f);
    const float dy = bob - hop * 14.0f - tap * 6.0f + std::max (0.0f, nodDip) * (murk > 0.5f ? 0.5f : 1.0f);
    const float squash = 1.0f - 0.25f * flip * (float) std::cos ((1.0f - flip) * 9.0f);       // the flip: squash and spring
    const float wobble = shake * 0.22f * (float) std::sin ((1.0f - shake) * 40.0f);             // the shake: side to side
    const float tilt = lean * 0.12f + wobble;
    const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s * breathe, s / breathe * squash)
                        .rotated (tilt).translated (a.getCentreX(), a.getCentreY() + 6.0f * s);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col)
    {
        Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2);
        g.setColour (col); g.fillPath (p, tf);
    };
    auto rect = [&] (float x, float y, float w, float h, Colour col)
    {
        Path p; p.addRectangle (x, y, w, h);
        g.setColour (col); g.fillPath (p, tf);
    };
    auto stroke = [&] (Path& p, float w) { g.setColour (ff::faceInk); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf); };

    const Colour body = ff::blob.interpolatedWith (ff::murkBlob, murk), shade = ff::blobShade.interpolatedWith (ff::murkShade, murk);
    const float grump = murk;                    // 0 calm, 1 grumpy
    const float bodyY = 100.0f + 2.0f * grump;
    ell (107, bodyY + 9 + dy, 58, 48 - 2 * grump, shade);
    ell (100, bodyY + dy, 58, 48 - 2 * grump, body);

    const float eyeY = 92.0f + 4.0f * grump;
    if (asleep || blinking)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 12, eyeY + 2 + dy); lid.quadraticTo (ex, eyeY + 10 + dy, ex + 12, eyeY + 2 + dy);
            stroke (lid, 6.0f);
        }
    }
    else if (flip > 0.3f) // scowling as the card flips into Murk: eyes screwed shut
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 14, eyeY + 6 + dy); lid.lineTo (ex + 14, eyeY + 6 + dy);
            stroke (lid, 6.0f);
        }
    }
    else if (frozen && ! playing && grump < 0.5f)
    {
        for (float ex : { 80.0f, 120.0f }) // happy, squinting
        {
            Path lid; lid.startNewSubPath (ex - 12, eyeY + 6 + dy); lid.quadraticTo (ex, eyeY - 8 + dy, ex + 12, eyeY + 6 + dy);
            stroke (lid, 6.0f);
        }
    }
    else
    {
        const float er = (dubbing || recording ? 16.5f : 14.0f) - grump, pr = dubbing || recording ? 8.0f : 7.0f - grump;
        for (float ex : { 80.0f, 120.0f })
        {
            ell (ex, eyeY + dy, er, er, ff::eye);
            ell (ex + look * 5.0f, eyeY + 2 + 2 * grump + dy, pr, pr, ff::faceInk);
        }
        if (grump > 0.01f) // heavy lids: Murk's eyes are half shut
        {
            const float lidH = 15.0f * grump * (dubbing || recording ? 0.5f : 1.0f);
            rect (64, eyeY - er - 1 + dy, 32, lidH, body);
            rect (104, eyeY - er - 1 + dy, 32, lidH, body);
        }
    }

    if (grump > 0.01f && ! asleep) // the brows: angled down to the middle
    {
        const float lift = nod > 0.3f ? 4.0f : 0.0f;
        Path l; l.startNewSubPath (64, eyeY - 12 - lift + dy); l.lineTo (64 + 32 * grump, eyeY - 12 + 9 * grump - lift + dy);
        Path r; r.startNewSubPath (136, eyeY - 12 - lift + dy); r.lineTo (136 - 32 * grump, eyeY - 12 + 9 * grump - lift + dy);
        stroke (l, 8.0f * grump);
        stroke (r, 8.0f * grump);
    }

    // mouth
    const float mouthY = 120.0f + 6.0f * grump;
    if (asleep)
    {
        Path m; m.startNewSubPath (94, mouthY + 4 + dy); m.lineTo (106, mouthY + 4 + dy);
        stroke (m, 6.0f);
    }
    else if (mouth > 0.15f)
    {
        ell (100, mouthY + 4 + dy, 7.0f + 3.0f * mouth, 4.0f + (8.0f - 3.0f * grump) * mouth, ff::faceInk);
    }
    else if (flip > 0.3f)
    {
        Path m; m.startNewSubPath (86, mouthY + 8 + dy); m.lineTo (114, mouthY + 8 + dy);
        stroke (m, 6.0f);
    }
    else
    {
        // Murmur smiles; Murk's mouth turns down into a flat frown
        const float curve = (playing ? 12.0f : 8.0f) * (1.0f - grump) - 7.0f * grump + (nod > 0.2f ? 4.0f : 0.0f);
        Path m; m.startNewSubPath (90 - 2 * grump, mouthY + dy); m.quadraticTo (100, mouthY + curve + dy, 110 + 2 * grump, mouthY + dy);
        stroke (m, 6.0f);
    }

    if (shake > 0.2f) // the relieved shake throws off a little murk
    {
        g.setColour (ff::dim.withAlpha (shake));
        for (float side : { -1.0f, 1.0f })
        {
            Path w; w.startNewSubPath (100 + side * 70, 80); w.quadraticTo (100 + side * 78, 95, 100 + side * 70, 110);
            g.strokePath (w, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded), tf);
        }
    }

    // drifting z's while asleep
    if (asleep)
    {
        g.setFont (ff::font (15.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (a.getRight() - 14.0f + ph * 8.0f, a.getY() + 10.0f - ph * 18.0f, 16.0f, 16.0f),
                        Justification::centred, false);
        }
    }
}

// =====================================================================================
// BufferStrip
// =====================================================================================
void BufferStrip::update (double t, double dt)
{
    now = t;
    const auto& eng = proc.getEngine();
    const auto& loop = eng.getLoop();
    auto& s = proc.apvts;
    auto v = [&] (const char* id) { return s.getRawParameterValue (id)->load(); };
    const bool wetOn = v (ParamIDs::wetOn) > 0.5f;

    level = eng.uiInputLevel.load();
    if (! loop.isReady())
        return;

    playing = loop.isPlaying();
    recording = loop.isRecording();
    dubbing = playing && v (ParamIDs::overdub) > 0.5f;
    frozen = wetOn && v (ParamIDs::freeze) > 0.5f;
    radio = proc.currentMode() == 1 && roundToInt (v (ParamIDs::murkLoop)) == 1;
    tuning = murmur::MurkLoop::stationPos (v (ParamIDs::loopModify)) / 4.0f;

    const int len = std::max (1, loop.length());
    const int filled = loop.filledSamples();
    fill = playing || recording ? 1.0f : (float) filled / (float) len;
    playFrac = loop.positionFraction();

    // bars
    const float* l = loop.data (0);
    const float* r = loop.data (1);
    const int cols = (int) peaks.size();
    const int shownLen = playing ? len : std::max (1, filled);
    const int shownStart = playing ? loop.origin() : (filled < len || recording ? 0 : loop.origin());
    const int stride = std::max (1, shownLen / cols / 64);
    for (int c = 0; c < cols; ++c)
    {
        float pk = 0.0f;
        const int s0 = (int) ((int64_t) c * shownLen / cols), s1 = (int) ((int64_t) (c + 1) * shownLen / cols);
        for (int k = s0; k < s1; k += stride)
        {
            const int i = (shownStart + k) % len;
            pk = std::max (pk, std::max (std::fabs (l[i]), std::fabs (r[i])));
        }
        peaks[(size_t) c] += (std::min (1.0f, pk * 1.8f) - peaks[(size_t) c]) * 0.35f;
    }

    look = playing ? playFrac * 2.0f - 1.0f : (level > 0.004f ? 0.8f : 0.0f);
    pebble.update (t, dt, playing, dubbing, frozen, recording, level, look);

    const double coreHz = eng.uiClockHz.load();
    const double secs = (double) (playing ? len : filled) / std::max (1000.0, coreHz);
    status = pebble.isAsleep() ? "ASLEEP" : recording ? "RECORDING" : dubbing ? "DUBBING" : playing ? "PLAYING" : frozen ? "FROZEN" : "LISTENING";
    detail = String (secs, 1) + (playing ? " S LOOP" : (recording ? " S SO FAR" : " S HEARD"));
    if (t - proc.uiMessageAt < 2.5 && proc.uiMessage.isNotEmpty()) detail = proc.uiMessage;
    repaint();
}

void BufferStrip::paintRadio (Graphics& g, Rectangle<float> a) const
{
    g.setColour (ff::housing);
    g.fillRoundedRectangle (a, 8.0f);
    const float x0 = a.getX() + 24.0f, w = a.getWidth() - 48.0f;
    for (int i = 0; i <= 20; ++i)
    {
        const bool major = i % 5 == 0;
        const float x = x0 + w * (float) i / 20.0f;
        g.setColour (major ? ff::ink : ff::line);
        g.fillRect (x - (major ? 1.5f : 1.0f), a.getY() + (major ? 10.0f : 16.0f), major ? 3.0f : 2.0f, major ? 14.0f : 6.0f);
    }
    const int nearest = roundToInt (tuning * 4.0f);
    for (int st = 0; st < murmur::MurkLoop::kStations; ++st)
    {
        const float x = x0 + w * (float) st / 4.0f;
        ff::label (g, murmur::MurkLoop::stationName (st), Rectangle<float> (x - 60.0f, a.getY() + 34.0f, 120.0f, 18.0f).toNearestInt(),
                   Justification::centred, st == nearest && std::abs (tuning * 4.0f - (float) st) < 0.15f ? ff::ink : ff::dim, 14.0f);
    }
    g.setColour (ff::red);
    g.fillRect (x0 + w * tuning - 1.5f, a.getY() + 4.0f, 3.0f, a.getHeight() - 8.0f);
}

void BufferStrip::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);

    auto inner = r.reduced (18, 14);
    pebble.paint (g, inner.removeFromLeft (76).toFloat());
    inner.removeFromLeft (18);

    auto info = inner.removeFromLeft (120);
    ff::label (g, "BUFFER", info.removeFromTop (18));
    ff::label (g, status, info.removeFromTop (30), Justification::centredLeft, ff::ink, 22.0f);
    ff::label (g, detail, info.removeFromTop (18));
    inner.removeFromLeft (18);

    if (radio)
    {
        paintRadio (g, inner.withSizeKeepingCentre (inner.getWidth(), 64).toFloat());
        return;
    }

    auto bars = inner.withSizeKeepingCentre (inner.getWidth(), 60).toFloat();
    const int cols = (int) peaks.size();
    const float step = bars.getWidth() / (float) cols;
    const float barW = step * 0.58f;
    const int playCol = (int) (playFrac * (float) cols);
    const int fillCol = (int) (fill * (float) cols);
    for (int c = 0; c < cols; ++c)
    {
        const float h = std::max (4.0f, peaks[(size_t) c] * 60.0f);
        Colour col = playing ? (c <= playCol ? ff::ink : ff::faint) : (c < fillCol ? (recording ? ff::red : ff::dim) : ff::card);
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
      murkWet (p.apvts, ParamIDs::murkWet, { "SOUP", "RELAY", "FLIP" }),
      murkLoop (p.apvts, ParamIDs::murkLoop, { "BURST", "RADIO", "MASK" }),
      routing (p.apvts, ParamIDs::routing, { "IN", "BOTH", "LOOP" }),
      time (p.apvts, ParamIDs::time, "Time"),
      wetModify (p.apvts, ParamIDs::wetModify, "Modify"),
      length (p.apvts, ParamIDs::length, "Length"),
      loopModify (p.apvts, ParamIDs::loopModify, "Modify"),
      clock (p.apvts, p),
      mix (p.apvts, ParamIDs::mix, "Mix"),
      glue (p.apvts, ParamIDs::glue, "Glue"),
      wetSwitch (p, 0, "WET", "TAP ON / OFF", "HOLD FREEZE"),
      loopSwitch (p, 1, "LOOP", "TAP REC / PLAY / STOP", "HOLD OVERDUB"),
      strip (p),
      width (p.apvts, ParamIDs::width, "Width"),
      tone (p.apvts, ParamIDs::tone, "Tone"),
      eqTilt (p.apvts, ParamIDs::eqTilt, "EQ tilt"),
      fade (p.apvts, ParamIDs::fade, "Fade"),
      direct (p.apvts, ParamIDs::direct, "Direct loop"),
      blend (p.apvts, ParamIDs::direct, "Blend"),
      balance (p.apvts, ParamIDs::balance, "Balance"),
      glueDial (p.apvts, ParamIDs::glue, "Glue"),
      crossDial (p.apvts, ParamIDs::cross, "Cross"),
      volume (p.apvts, ParamIDs::outGain, "Volume"),
      sync (p.apvts, ParamIDs::sync, { "UNSYNCED", "LOOP FOLLOWS WET", "WET FOLLOWS LOOP" }),
      spreadSolo (p.apvts, ParamIDs::spreadSolo, { "BOTH", "WET", "LOOP" }),
      halfLength (p.apvts, ParamIDs::halfLength, { "HALF", "FULL" }),
      stepSet (p.apvts, ParamIDs::stepSet, { "PEDAL", "OCTAVES", "4THS + 5THS", "SEMITONES" }),
      crossSource (p.apvts, ParamIDs::crossSource, { "INPUT", "WET", "LOOP" }),
      eqOn (p.apvts, ParamIDs::eqOn, { "WET", "BOTH", "LOOP" }),
      inGain (p.apvts, ParamIDs::inGain, "Input"),
      outGain (p.apvts, ParamIDs::outGain, "Output"),
      bufferSeconds (p.apvts, ParamIDs::bufferSeconds, "Max loop at 64 kHz"),
      expressionPanel (p),
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
    lnf.setColour (PopupMenu::backgroundColourId, ff::face);
    lnf.setColour (PopupMenu::textColourId, ff::ink);
    lnf.setColour (PopupMenu::highlightedBackgroundColourId, ff::ink);
    lnf.setColour (PopupMenu::highlightedTextColourId, ff::bg);

    addAndMakeVisible (pages);
    pages.onPick = [this] (int i) { showPage (i); };
    addAndMakeVisible (presets);

    // the mode card: click to switch, or hold B+C for a second
    modeButton.card.setTextHeight (22.0f);
    modeButton.onClick = [this] (const MouseEvent&) { proc.switchMode(); };
    addAndMakeVisible (modeButton);

    // KEEP: click to keep this moment, right-click for notes, export and the Keeper switch
    keepButton.card.setTextHeight (14.0f);
    keepButton.card.setValue ("KEEP");
    keepButton.onClick = [this] (const MouseEvent& e) { if (e.mods.isPopupMenu()) keeperMenu(); else proc.keepNow(); };
    addAndMakeVisible (keepButton);
    addChildComponent (noteEditor);
    noteEditor.setTextToShowWhenEmpty ("Add a note (optional), Enter to save", ff::faint);
    noteEditor.setFont (FontOptions (13.0f));
    noteEditor.setIndents (10, 8);
    noteEditor.setColour (TextEditor::backgroundColourId, ff::bg);
    noteEditor.setColour (TextEditor::textColourId, ff::ink);
    noteEditor.setColour (TextEditor::outlineColourId, ff::line);
    noteEditor.setColour (TextEditor::focusedOutlineColourId, ff::ink);
    noteEditor.setColour (CaretComponent::caretColourId, ff::ink);
    noteEditor.setInputRestrictions (200);
    noteEditor.onReturnKey = [this] { closeNote (true); };
    noteEditor.onEscapeKey = [this] { closeNote (false); };
    noteEditor.onTextChange = [this] { noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0; };
    seenKeep = proc.uiKeepPulse.load();

    wetCard.setTextHeight (48.0f);
    loopCard.setTextHeight (48.0f);
    rateCard.setTextHeight (38.0f);
    khzCard.setTextHeight (20.0f);
    khzCard.setValue ("KHZ");
    bpmCard.setTextHeight (46.0f);
    noteCard.setTextHeight (36.0f);
    octaveCard.setTextHeight (30.0f);

    panelComps = { &wetCard, &loopCard, &rateCard, &khzCard, &routing, &time, &wetModify,
                   &length, &loopModify, &clock, &mix, &wetSwitch, &loopSwitch, &strip };
    murmurPanel = { &wetMode, &loopMode };
    murkPanel = { &murkWet, &murkLoop, &glue };

    struct Sw { const char* id; const char* name; };
    for (auto s : { Sw { ParamIDs::miso, "MISO" }, Sw { ParamIDs::spread, "SPREAD" }, Sw { ParamIDs::dryKill, "DRY KILL" },
                    Sw { ParamIDs::trails, "TRAILS" }, Sw { ParamIDs::latch, "LATCH" }, Sw { ParamIDs::noDub, "NO DUB" },
                    Sw { ParamIDs::smoothClock, "SMOOTH" }, Sw { ParamIDs::classic, "CLASSIC" } })
        murmurDeep.add (switches.add (new CardToggle (p.apvts, s.id, s.name, CardToggle::Style::OnOffBeside)));
    // In Murk, HALF and DRY GLUE take the places of NO DUB and CLASSIC
    for (auto s : { Sw { ParamIDs::miso, "MISO" }, Sw { ParamIDs::spread, "SPREAD" }, Sw { ParamIDs::dryKill, "DRY KILL" },
                    Sw { ParamIDs::trails, "TRAILS" }, Sw { ParamIDs::latch, "LATCH" }, Sw { ParamIDs::halfLength, "HALF" },
                    Sw { ParamIDs::smoothClock, "SMOOTH" }, Sw { ParamIDs::dryGlue, "DRY GLUE" } })
        murkDeep.add (murkSwitches.add (new CardToggle (p.apvts, s.id, s.name, CardToggle::Style::OnOffBeside)));
    for (auto* c : std::initializer_list<Component*> { &width, &fade, &balance, &sync, &spreadSolo, &stepSet, &resetHidden, &inGain, &bufferSeconds })
        deepComps.add (c);
    for (auto* c : std::initializer_list<Component*> { &tone, &direct, &halfLength, &outGain }) murmurDeep.add (c);
    for (auto* c : std::initializer_list<Component*> { &eqTilt, &blend, &glueDial, &crossDial, &volume, &crossSource, &eqOn }) murkDeep.add (c);
    resetHidden.onClick = [this] { proc.resetHiddenOptions(); };

    for (auto s : { Sw { ParamIDs::rampTime, "TIME" }, Sw { ParamIDs::rampWetMod, "WET MOD" }, Sw { ParamIDs::rampClock, "CLOCK" },
                    Sw { ParamIDs::rampLoopMod, "LOOP MOD" }, Sw { ParamIDs::rampLength, "LENGTH" } })
        motionComps.add (targets.add (new CardToggle (p.apvts, s.id, s.name, CardToggle::Style::NameInside)));
    for (auto* c : std::initializer_list<Component*> { &bounce, &sweep, &rampSpeed, &waveCards, &rampWave, &modSource,
                                                       &expression, &expPolarity, &bpmCard, &tempoSync, &tap, &synthEnable,
                                                       &noteCard, &octaveCard, &synthGate, &attack, &decay, &sustain,
                                                       &release, &glide, &rootDown, &rootUp })
        motionComps.add (c);

    // ---- Pedal page ----
    for (int k = 0; k < 4; ++k) pedalComps.add (pedalCards.add (new PedalCard (p, k)));
    for (auto* c : std::initializer_list<Component*> { &tableMode, &showActions, &tryBoth, &tryMode, &tryKeep, &keepLearn, &footDevice,
                                                       &pedalMap, &keepExport, &footDefaults, &keeperContext, &expressionPanel })
        pedalComps.add (c);
    tableMode.onPick = [this] (int i) { pedalTable = i; tableMode.setSelected (i); repaint(); };
    showActions.onClick = [this] { showOverlay (true); };
    overlay.close.onClick = [this] { showOverlay (false); };
    addChildComponent (overlay);
    tryBoth.onClick = [this]
    {
        // the same as tapping A and B together
        const bool anyOn = proc.apvts.getRawParameterValue (ParamIDs::wetOn)->load() > 0.5f
                        || proc.apvts.getRawParameterValue (ParamIDs::loopOn)->load() > 0.5f
                        || proc.apvts.getRawParameterValue (ParamIDs::loopRec)->load() > 0.5f;
        const char* const offIds[5] = { ParamIDs::wetOn, ParamIDs::loopOn, ParamIDs::loopRec, ParamIDs::freeze, ParamIDs::overdub };
        for (int i = 0; i < (anyOn ? 5 : 2); ++i)
            if (auto* prm = proc.apvts.getParameter (offIds[i]))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (anyOn ? 0.0f : 1.0f);
                prm->endChangeGesture();
            }
    };
    tryMode.onClick = [this] { proc.switchMode(); };
    tryKeep.onClick = [this] { proc.keepNow(); };
    keepLearn.onClick = [this] { if (proc.learningWhat() == MurmurProcessor::LearnKeeper) proc.cancelLearn(); else proc.learnKeeper(); };
    keepExport.onClick = [this] { proc.exportKeepers(); };
    footDefaults.onClick = [this] { proc.resetFootswitches(); };
    keeperContext.onPick = [this] (int i) { proc.setKeeperLive (i == 1); keeperContext.setSelected (i); };
    keeperContext.setSelected (proc.pedals().keeperLive ? 1 : 0);
    footDevice.onClick = [this]
    {
        PopupMenu m;
        m.addItem (-1, proc.footInputStatus(), false, false);
        m.addSeparator();
        const auto chosen = proc.pedals().footDevice;
        m.addItem (10, "Find my footswitch automatically", true, chosen.isEmpty());
        const auto devices = DirectMidiInput::deviceNames();
        for (int i = 0; i < devices.size() && i < 40; ++i)
            m.addItem (100 + i, "Use " + devices[i], true, chosen == devices[i]);
        m.addItem (11, "Only from a MIDI track in Ableton", true, chosen == "none");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&footDevice), [this, devices] (int r)
        {
            if (r == 10) proc.setFootDevice ({});
            else if (r == 11) proc.setFootDevice ("none");
            else if (r >= 100 && r - 100 < devices.size()) proc.setFootDevice (devices[r - 100]);
        });
    };
    pedalMap.onClick = [this]
    {
        PopupMenu m;
        m.addItem (300, "Off", true, proc.pedals().pedalMapChannel == 0);
        for (int ch = 1; ch <= 16; ++ch) m.addItem (300 + ch, "Channel " + String (ch), true, proc.pedals().pedalMapChannel == ch);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&pedalMap), [this] (int r) { if (r >= 300) proc.setPedalMapChannel (r - 300); });
    };

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

    for (auto* list : { &panelComps, &murmurPanel, &murkPanel, &deepComps, &murmurDeep, &murkDeep, &motionComps, &pedalComps })
        for (auto* c : *list) addChildComponent (c);
    overlay.toFront (false);

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
    if (p != PedalPage) overlay.setVisible (false);
    updateVisibility();
    resized();
    repaint();
}

void MurmurEditor::showOverlay (bool show)
{
    overlay.setVisible (show && page == PedalPage);
    if (show) overlay.toFront (false);
    repaint();
}

void MurmurEditor::updateVisibility()
{
    shownMode = proc.currentMode();
    const bool murk = shownMode == 1;
    for (auto* c : panelComps) c->setVisible (page == PanelPage);
    for (auto* c : murmurPanel) c->setVisible (page == PanelPage && ! murk);
    for (auto* c : murkPanel) c->setVisible (page == PanelPage && murk);
    for (auto* c : deepComps) c->setVisible (page == DeepPage);
    for (auto* c : murmurDeep) c->setVisible (page == DeepPage && ! murk);
    for (auto* c : murkDeep) c->setVisible (page == DeepPage && murk);
    for (auto* c : motionComps) c->setVisible (page == MotionPage);
    for (auto* c : pedalComps) c->setVisible (page == PedalPage);
    if (pedalTable < 0 || page != PedalPage) pedalTable = shownMode;
    tableMode.setSelected (pedalTable);
}

void MurmurEditor::keeperMenu()
{
    PopupMenu m;
    m.addItem (1, "Add a note to Keeper No. " + String (proc.lastKeeper()), proc.lastKeeper() > 0);
    m.addItem (2, "Export for Claude");
    m.addItem (3, "Open the Keepers folder");
    m.addSeparator();
    m.addItem (5, "Tag Keepers as Live", true, proc.pedals().keeperLive);
    m.addItem (4, "Learn a Keeper footswitch");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&keepButton), [this] (int r)
    {
        if (r == 1)
        {
            noteNumber = proc.lastKeeper();
            noteEditor.clear();
            noteEditor.setVisible (true);
            noteEditor.grabKeyboardFocus();
            noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0;
        }
        else if (r == 2) proc.exportKeepers();
        else if (r == 3) proc.openKeepersFolder();
        else if (r == 4) proc.learnKeeper();
        else if (r == 5) { proc.setKeeperLive (! proc.pedals().keeperLive); keeperContext.setSelected (proc.pedals().keeperLive ? 1 : 0); }
    });
}

void MurmurEditor::closeNote (bool save)
{
    if (save && noteEditor.getText().trim().isNotEmpty())
    {
        proc.addKeeperNote (noteNumber, noteEditor.getText());
        proc.uiMessage = "NOTED";
        proc.uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
    }
    noteEditor.setVisible (false);
    noteEditor.clear();
    noteUntil = 0.0;
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
    g.drawText ("MURMUR", 20, 20, 120, 40, Justification::centredLeft, false);
    g.setColour (ff::dim);
    g.setFont (ff::font (12.0f, 0.06f));
    g.drawText ("MODE", 246, 26, 60, 14, Justification::centredLeft, false);
    g.drawText ("HOLD B+C", 246, 40, 60, 14, Justification::centredLeft, false);
    const bool murk = proc.currentMode() == 1;

    if (page == PanelPage)
    {
        section (g, secA); section (g, secB); section (g, secC);
        ff::label (g, "WET CHANNEL", { 36, 92, 200, 16 });
        ff::label (g, "CLOCK", { 356, 92, 200, 16 });
        ff::label (g, "MICRO-LOOPER", { 656, 92, 200, 16 });
        ff::label (g, interval, murk ? Rectangle<int> { 340, 342, 280, 20 } : Rectangle<int> { 340, 366, 280, 20 },
                   Justification::centred, ff::ink, 15.0f);
    }
    else if (page == DeepPage)
    {
        section (g, secA); section (g, secB);
        ff::label (g, "HIDDEN OPTIONS", { 36, 92, 300, 16 });
        if (murk)
        {
            g.setColour (ff::card);
            g.fillRect (36, 256, 558, 2);
            ff::label (g, "CROSS FROM", { 386, 274, 200, 16 });
            ff::label (g, "YOUR PLAYING MOVES THE VOLUME AND PITCH", { 386, 344, 220, 14 }, Justification::centredLeft, ff::dim, 11.0f);
            ff::label (g, "EQ ON", { 36, 412, 200, 16 });
            ff::label (g, "SPREAD ON", { 320, 412, 200, 16 });
            ff::label (g, "CHANNEL SYNC", { 36, 480, 300, 16 });
            ff::label (g, "CLOCK STEPS", { 36, 548, 300, 16 });
        }
        else
        {
            ff::label (g, "CHANNEL SYNC", { 36, 266, 300, 16 });
            ff::label (g, "SPREAD ON", { 36, 334, 200, 16 });
            ff::label (g, "LOOP LENGTH", { 377, 334, 200, 16 });
            ff::label (g, "CLOCK STEPS", { 36, 402, 300, 16 });
        }
        ff::label (g, "SWITCHES", { 646, 92, 200, 16 });
        g.setColour (ff::card);
        g.fillRect (646, 300, 278, 2);
        ff::label (g, "LEVELS", { 646, 314, 200, 16 });
        if (murk)
        {
            g.setColour (ff::dim);
            g.setFont (ff::font (11.0f, 0.08f));
            g.drawFittedText ("HALF AND DRY GLUE TAKE THE PLACES OF NO DUB AND CLASSIC IN MURK", 646, 446, 278, 32, Justification::topLeft, 2);
        }
    }
    else if (page == MotionPage)
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
    else // Pedal
    {
        section (g, secA); section (g, secB);
        ff::label (g, "FOOTSWITCHES", { 36, 92, 200, 16 });
        g.setColour (ff::card);
        g.fillRect (36, 356, 568, 2);
        ff::label (g, "TWO AT ONCE", { 36, 366, 200, 16 });
        struct Combo { const char* keys; const char* job; };
        const int t = pedalTable < 0 ? proc.currentMode() : pedalTable;
        const Combo combos[3] = { { "A + B TAP", "BOTH CHANNELS ON / OFF" },
                                  { "B + C HOLD 1 S", t == 1 ? "SWITCH MODE: MURK TO MURMUR" : "SWITCH MODE: MURMUR TO MURK" },
                                  { "C + D HOLD 1 S", "KEEPER: SAVE THIS MOMENT" } };
        for (int i = 0; i < 3; ++i)
        {
            const int y = 390 + i * 44;
            ff::label (g, combos[i].keys, { 36, y, 124, 36 }, Justification::centredLeft, ff::ink, 15.0f);
            auto r = Rectangle<float> (164.0f, (float) y, 340.0f, 36.0f);
            g.setColour (ff::housing);
            g.fillRoundedRectangle (r, 6.0f);
            g.setGradientFill (ColourGradient (ff::cardHi, 0, r.getY(), ff::cardLow, 0, r.getBottom(), false));
            g.fillRoundedRectangle (r.reduced (3.0f), 4.0f);
            g.setColour (Colours::black);
            g.fillRect (r.getX() + 3.0f, r.getCentreY() - 0.5f, r.getWidth() - 6.0f, 1.0f);
            ff::label (g, combos[i].job, r.withTrimmedLeft (12.0f).toNearestInt(), Justification::centredLeft, ff::ink, 16.0f);
        }
        ff::label (g, "C + D TAP HANDS THE FOOTSWITCHES TO THE NEXT MURMUR", { 36, 524, 568, 14 }, Justification::centredLeft, ff::dim, 11.0f);
        const double now = nowSeconds();
        if (now - proc.uiMessageAt < 3.0)
            ff::label (g, proc.uiMessage, { 36, 552, 460, 40 }, Justification::centredLeft, ff::ink, 24.0f);

        // connection, original map and Keeper
        g.setColour (ff::card);
        g.fillRect (646, 366, 278, 2);
        g.setColour (proc.pedalsDirect() ? ff::green : ff::pin);
        g.fillEllipse (646.0f, 381.0f, 10.0f, 10.0f);
        ff::label (g, proc.pedalsDirect() ? "FOOTSWITCHES CONNECTED" : "FOOTSWITCHES VIA ABLETON", { 664, 376, 260, 20 }, Justification::centredLeft, ff::ink, 14.0f);
        ff::label (g, "READ FROM", { 646, 404, 120, 28 });
        ff::label (g, "ORIGINAL PEDAL MIDI MAP", { 646, 438, 160, 28 });
        g.setColour (ff::card);
        g.fillRect (646, 476, 278, 2);
        ff::label (g, "KEEPER", { 646, 488, 120, 16 });
        ff::label (g, "LEARN A KEEPER SWITCH", { 646, 560, 180, 28 });
    }

    if (noteEditor.isVisible())
    {
        g.setColour (ff::face);
        g.fillRoundedRectangle (556.0f, 64.0f, 384.0f, 48.0f, 10.0f);
    }
}

void MurmurEditor::resized()
{
    modeButton.setBounds (134, 22, 104, 40);
    pages.setBounds (400, 26, 320, 32);
    presets.setBounds (732, 24, 140, 40);
    keepButton.setBounds (880, 24, 60, 32);
    noteEditor.setBounds (564, 70, 368, 36);
    overlay.setBounds (0, 76, 960, 564);
    const bool murk = proc.currentMode() == 1;

    if (page == PanelPage)
    {
        secA = { 20, 76, 300, 432 };
        secB = { 340, 76, 280, 432 };
        secC = { 640, 76, 300, 432 };

        wetCard.setBounds (36, 120, 268, 84);
        wetMode.setBounds (36, 216, 268, 32);
        murkWet.setBounds (36, 216, 268, 32);
        time.setBounds (30, 258, 130, 146);
        wetModify.setBounds (180, 258, 130, 146);
        wetSwitch.setBounds (36, 428, 268, 64);

        loopCard.setBounds (656, 120, 268, 84);
        loopMode.setBounds (656, 216, 268, 32);
        murkLoop.setBounds (656, 216, 268, 32);
        length.setBounds (650, 258, 130, 146);
        loopModify.setBounds (800, 258, 130, 146);
        loopSwitch.setBounds (656, 428, 268, 64);

        rateCard.setBounds (403, 116, 84, 56);
        khzCard.setBounds (493, 116, 64, 56);
        if (murk) // room for Glue under Mix
        {
            clock.setBounds (400, 180, 160, 160);
            routing.setBounds (356, 370, 248, 32);
            mix.setBounds (356, 412, 248, 40);
            glue.setBounds (356, 456, 248, 40);
        }
        else
        {
            clock.setBounds (390, 180, 180, 180);
            routing.setBounds (356, 396, 248, 32);
            mix.setBounds (356, 444, 248, 40);
        }
        strip.setBounds (20, 524, 920, 96);
    }
    else if (page == DeepPage)
    {
        secA = { 20, 76, 590, 544 };
        secB = { 630, 76, 310, 544 };
        resetHidden.setBounds (500, 86, 94, 28);
        int x = 36;
        Dial* const murkRow[5] = { &width, &eqTilt, &fade, &blend, &balance };
        Dial* const murmurRow[5] = { &width, &tone, &fade, &direct, &balance };
        for (auto* d : murk ? murkRow : murmurRow)
        {
            d->setBounds (x, 120, 110, 128);
            x += 112;
        }
        if (murk)
        {
            glueDial.setBounds (36, 268, 110, 128);
            crossDial.setBounds (148, 268, 110, 128);
            volume.setBounds (260, 268, 110, 128);
            crossSource.setBounds (386, 296, 208, 32);
            eqOn.setBounds (36, 434, 268, 32);
            spreadSolo.setBounds (320, 434, 274, 32);
            sync.setBounds (36, 502, 558, 32);
            stepSet.setBounds (36, 570, 558, 32);
        }
        else
        {
            sync.setBounds (36, 288, 558, 32);
            spreadSolo.setBounds (36, 356, 325, 32);
            halfLength.setBounds (377, 356, 217, 32);
            stepSet.setBounds (36, 424, 558, 32);
        }
        for (auto* list : { &switches, &murkSwitches })
            for (int i = 0; i < list->size(); ++i)
                (*list)[i]->setBounds (646 + (i % 2) * 145, 118 + (i / 2) * 44, 133, 34);
        inGain.setBounds (646, 338, 278, 40);
        outGain.setBounds (646, 392, 278, 40);
        bufferSeconds.setBounds (646, murk ? 392 : 446, 278, 40);
    }
    else if (page == MotionPage)
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
    else // Pedal
    {
        secA = { 20, 76, 600, 544 };
        secB = { 630, 76, 310, 544 };
        tableMode.setBounds (300, 86, 150, 28);
        showActions.setBounds (462, 86, 142, 28);
        for (int k = 0; k < 4; ++k)
            pedalCards[k]->setBounds (36 + k * 145, 122, 133, 226);
        tryBoth.setBounds (516, 393, 88, 30);
        tryMode.setBounds (516, 437, 88, 30);
        tryKeep.setBounds (516, 481, 88, 30);
        keepLearn.setBounds (836, 560, 88, 28);
        footDefaults.setBounds (500, 558, 104, 30);
        expressionPanel.setBounds (646, 92, 278, 262);
        footDevice.setBounds (766, 404, 158, 28);
        pedalMap.setBounds (846, 438, 78, 28);
        keeperContext.setBounds (646, 512, 180, 30);
        keepExport.setBounds (836, 512, 88, 30);
    }
}

void MurmurEditor::timerCallback()
{
    const double now = nowSeconds();
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    auto& s = proc.apvts;
    const auto& eng = proc.getEngine();
    const int mode = proc.currentMode();
    const bool murk = mode == 1;

    if (mode != shownMode)
    {
        updateVisibility();
        resized();
        repaint();
    }
    modeButton.card.setValue (murk ? "MURK" : "MURMUR");

    // KEEP flips to KEPT for a moment after every Keeper, and the note field opens
    const int kp = proc.uiKeepPulse.load();
    if (kp != seenKeep)
    {
        seenKeep = kp;
        keptUntil = now + 1.6;
        noteNumber = proc.lastKeeper();
        noteEditor.clear();
        noteEditor.setVisible (true);
        noteUntil = now + 20.0;
        repaint (556, 64, 384, 48);
    }
    keepButton.card.setValue (now < keptUntil ? "KEPT" : "KEEP");
    if (noteEditor.isVisible() && now > noteUntil && ! noteEditor.hasKeyboardFocus (true)) { closeNote (false); repaint (556, 64, 384, 48); }

    for (auto* c : { &wetCard, &loopCard, &rateCard, &khzCard, &bpmCard, &noteCard, &octaveCard, &modeButton.card, &keepButton.card })
        c->advance (dt);

    if (page == PanelPage)
    {
        const int wm = jlimit (0, 2, (int) std::lround (s.getRawParameterValue (murk ? ParamIDs::murkWet : ParamIDs::wetMode)->load()));
        const int lm = jlimit (0, 2, (int) std::lround (s.getRawParameterValue (murk ? ParamIDs::murkLoop : ParamIDs::loopMode)->load()));
        static const char* wetNames[2][3] = { { "REVERB", "DELAY", "SLIP" }, { "SOUP", "RELAY", "FLIP" } };
        static const char* loopNames[2][3] = { { "ENV", "TAPE", "STRETCH" }, { "BURST", "RADIO", "MASK" } };
        wetCard.setValue (wetNames[mode][wm]);
        loopCard.setValue (loopNames[mode][lm]);

        static const char* timeCap[2][3] = { { "Time \xc2\xb7 decay", "Time \xc2\xb7 delay", "Time \xc2\xb7 size" },
                                             { "Time \xc2\xb7 decay", "Time \xc2\xb7 delay", "Time \xc2\xb7 lag" } };
        static const char* wetModCap[2][3] = { { "Modify \xc2\xb7 smear", "Modify \xc2\xb7 repeats", "Modify \xc2\xb7 speed" },
                                               { "Modify \xc2\xb7 character", "Modify \xc2\xb7 repeats", "Modify \xc2\xb7 chord" } };
        static const char* lenCap[2][3] = { { "Length \xc2\xb7 slice", "Length \xc2\xb7 loop", "Length \xc2\xb7 slice" },
                                            { "Length \xc2\xb7 step", "Length \xc2\xb7 speed", "Length \xc2\xb7 character" } };
        static const char* loopModCap[2][3] = { { "Modify \xc2\xb7 touch", "Modify \xc2\xb7 speed", "Modify \xc2\xb7 stretch" },
                                                { "Modify \xc2\xb7 chaos", "Modify \xc2\xb7 station", "Modify \xc2\xb7 threshold" } };
        const bool synced = proc.currentBpm() > 0.0f;
        time.setCaption (synced && (murk ? wm != 0 : wm != 0) ? String::fromUTF8 ("Time · note") : String::fromUTF8 (timeCap[mode][wm]));
        wetModify.setCaption (String::fromUTF8 (wetModCap[mode][wm]));
        // in Radio, Length is each station's own control
        static const char* stationLen[5] = { "Length \xc2\xb7 speed", "Length \xc2\xb7 speed", "Length \xc2\xb7 voices", "Length \xc2\xb7 moment", "Length \xc2\xb7 rotation" };
        const float lmod = s.getRawParameterValue (ParamIDs::loopModify)->load();
        if (murk && lm == 1)
            length.setCaption (String::fromUTF8 (stationLen[roundToInt (murmur::MurkLoop::stationPos (lmod))]));
        else
            length.setCaption (! murk && synced && lm == 1 ? String::fromUTF8 ("Length · bar") : String::fromUTF8 (lenCap[mode][lm]));
        loopModify.setCaption (String::fromUTF8 (loopModCap[mode][lm]));

        auto modded = [&] (Dial& d, const char* rampId, const std::atomic<float>& actual)
        {
            const bool active = s.getRawParameterValue (rampId)->load() > 0.5f;
            d.setModulated (actual.load(), active);
            if (active) d.repaint();
        };
        modded (time, ParamIDs::rampTime, eng.uiTime);
        modded (wetModify, ParamIDs::rampWetMod, eng.uiWetMod);
        modded (length, ParamIDs::rampLength, eng.uiLength);
        modded (loopModify, ParamIDs::rampLoopMod, eng.uiLoopMod);

        // Step values read out on small flip cards. When ramping or expression moves the
        // knob, the card follows where it really is.
        auto actual = [&] (const char* rampId, const char* knobId, const std::atomic<float>& moved)
        {
            return s.getRawParameterValue (rampId)->load() > 0.5f ? moved.load() : s.getRawParameterValue (knobId)->load();
        };
        const float wmod = actual (ParamIDs::rampWetMod, ParamIDs::wetModify, eng.uiWetMod);
        const float lmodA = actual (ParamIDs::rampLoopMod, ParamIDs::loopModify, eng.uiLoopMod);
        const float lenA = actual (ParamIDs::rampLength, ParamIDs::length, eng.uiLength);
        String wetStep, loopStep, lenStep;
        if (! murk && wm == 2)
        {
            const auto st = murmur::WetChannel::slipStep (wmod);
            wetStep = ui::stepText (st.semis, st.reverse);
        }
        if (murk && wm == 1)
            wetStep = murmur::MurkWet::relayEndless (wmod) ? String ("ENDLESS")
                                                          : String (murmur::MurkWet::relayRepeats (wmod)) + (murmur::MurkWet::relayRepeats (wmod) == 1 ? " REPEAT" : " REPEATS");
        if (murk && wm == 2)
        {
            const auto& ch = murmur::FlipChords::at (murmur::FlipChords::indexFor (wmod));
            StringArray parts;
            for (int v = 0; v < ch.n; ++v) parts.add (ui::intervalName (ch.semis[v]));
            wetStep = parts.joinIntoString (" ");
        }
        if (! murk && lm == 1)
        {
            const float rate = murmur::LoopChannel::tapeRate (lmodA);
            loopStep = ui::stepText (roundToInt (12.0f * std::log2 (std::abs (rate))), rate < 0.0f);
        }
        if (murk && lm == 1)
        {
            const float sp = murmur::MurkLoop::stationPos (lmodA);
            const int near = roundToInt (sp);
            loopStep = std::abs (sp - (float) near) < 0.12f ? String (murmur::MurkLoop::stationName (near)) : String ("BETWEEN");
        }
        if (murk && lm == 2)
            lenStep = murmur::MurkLoop::maskName (roundToInt (murmur::MurkLoop::maskPos (lenA)));
        wetModify.setStepCard (wetStep);
        loopModify.setStepCard (loopStep);
        length.setStepCard (lenStep);
        for (auto* d : { &time, &wetModify, &length, &loopModify })
            d->advance (dt);

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
        repaint (340, 340, 280, 50);
    }
    // The tempo card always tracks the running tempo, whichever page is open
    const float bpm = proc.currentBpm();
    bpmCard.setValue (bpm > 0.0f ? String (roundToInt (bpm)) : "--");

    if (page == MotionPage)
    {
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
    if (page == PedalPage)
    {
        const int t = pedalTable < 0 ? mode : pedalTable;
        for (auto* c : pedalCards) c->tick (dt, t);
        expressionPanel.tick (dt);
        const auto& st = proc.pedals();
        const String dev = st.footDevice.isEmpty() ? "FIND AUTOMATICALLY" : (st.footDevice == "none" ? "ABLETON ONLY" : st.footDevice.toUpperCase());
        if (footDevice.getButtonText() != dev) footDevice.setButtonText (dev);
        const String map = st.pedalMapChannel == 0 ? "OFF" : "CH " + String (st.pedalMapChannel);
        if (pedalMap.getButtonText() != map) pedalMap.setButtonText (map);
        keepLearn.setButtonText (proc.learningWhat() == MurmurProcessor::LearnKeeper ? "PRESS IT" : (st.keeperCC >= 0 ? "CC " + String (st.keeperCC) : "LEARN"));
        repaint (36, 552, 460, 40);
        repaint (646, 370, 278, 30);
    }
    presets.repaint();
}
