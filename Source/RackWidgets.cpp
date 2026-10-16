#include "RackWidgets.h"

namespace pr
{

namespace
{
    juce::Rectangle<float> circleBox (juce::Point<float> c, float r) { return { c.x - r, c.y - r, r * 2.0f, r * 2.0f }; }
}

//==============================================================================
RackKnob::RackKnob (rk::Knob s)
    : style (s)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setScrollWheelEnabled (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void RackKnob::paint (juce::Graphics& g)
{
    const float maxA = rk::knobMaxAngle (style);
    const float prop = (float) valueToProportionOfLength (getValue());
    const float D    = rk::knobDiameter (style) * zoom;

    rk::paintKnob (g, getLocalBounds().toFloat().getCentre(), D, -maxA + prop * 2.0f * maxA, style,
                   isEnabled(), isMouseOverOrDragging());
}

void RackKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu != nullptr)
            onMenu (*this);
        return;
    }

    // 双击的第二下不开始拖：紧接着的 mouseDoubleClick 走 Slider 自带的复位
    if (! isEnabled() || e.getNumberOfClicks() > 1)
        return;

    dragging   = true;
    proportion = (float) valueToProportionOfLength (getValue());
    lastY      = e.position.y;

    // Rack 的 "Lock cursor while dragging params"：光标藏起来，拖多远都行
    e.source.enableUnboundedMouseMovement (true, false);

    if (onDragStart != nullptr)
        onDragStart();

    if (onTip != nullptr)
        onTip (*this, true);
}

void RackKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const float dy = e.position.y - lastY;
    lastY = e.position.y;

    // Rack 的 getModSpeed：Ctrl 1/10，Shift ×4，Ctrl+Shift 1/100。
    // 基准速度比 Rack 快一倍（满量程约 400 px）：插件窗口比 Rack 的机架小得多
    float speed = 1.0f / 400.0f;
    const bool ctrl = e.mods.isCommandDown(), shift = e.mods.isShiftDown();
    if (ctrl && shift) speed /= 100.0f;
    else if (ctrl)     speed /= 10.0f;
    else if (shift)    speed *= 4.0f;

    proportion = juce::jlimit (0.0f, 1.0f, proportion - dy * speed);
    setValue (proportionOfLengthToValue (proportion), juce::sendNotificationSync);
}

void RackKnob::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    dragging = false;
    e.source.enableUnboundedMouseMovement (false);

    if (onDragEnd != nullptr)
        onDragEnd();

    if (onTip != nullptr && ! isMouseOver())
        onTip (*this, false);

    repaint();
}

void RackKnob::mouseEnter (const juce::MouseEvent&)
{
    if (onTip != nullptr)
        onTip (*this, true);
    repaint();
}

void RackKnob::mouseExit (const juce::MouseEvent&)
{
    if (onTip != nullptr && ! dragging)
        onTip (*this, false);
    repaint();
}

void RackKnob::valueChanged()
{
    if (onTip != nullptr && (dragging || isMouseOver()))
        onTip (*this, true);
}

//==============================================================================
RackButton::RackButton (Kind k, juce::Colour c, float d)
    : kind (k), lightColour (c),
      diameter (d > 0.0f ? d : (k == Kind::bezel ? 21.26f : 15.36f))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RackButton::setLight (float b)
{
    if (std::abs (b - light) > 0.004f)
    {
        light = b;
        repaint();
    }
}

void RackButton::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float D = diameter * zoom;
    const bool hov = isMouseOver() && isEnabled();

    if (kind == Kind::bezel)
    {
        rk::paintBezel (g, c, D, lightColour, isEnabled() ? light : 0.0f, pressed, hov);

        if (! isEnabled())
        {
            g.setColour (rk::shade (rk::panel().bg).withAlpha (0.45f));
            g.fillEllipse (circleBox (c, D * 0.55f));
        }
    }
    else
    {
        rk::paintTL1105 (g, c, D, pressed, hov, isEnabled());
    }
}

void RackButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu != nullptr)
            onMenu();
        return;
    }

    if (! isEnabled())
        return;

    pressed = true;
    repaint();
}

void RackButton::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;

    pressed = false;
    repaint();

    // 回调放最后：它可能把这颗键连同整个皮肤一起删掉（比如换外观）
    if (getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
        onClick();
}

//==============================================================================
RackSwitch::RackSwitch (int n)
    : positions (juce::jlimit (2, 3, n))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RackSwitch::setPosition (int p, juce::NotificationType n)
{
    p = juce::jlimit (0, positions - 1, p);
    if (p == position)
        return;

    position = p;
    repaint();

    if (n != juce::dontSendNotification && onChange != nullptr)
        onChange (position);
}

void RackSwitch::paint (juce::Graphics& g)
{
    const float w = 14.0f * zoom;
    const float h = (positions == 3 ? 27.0f : 20.6f) * zoom;
    rk::paintSwitch (g, juce::Rectangle<float> (w, h).withCentre (getLocalBounds().toFloat().getCentre()),
                     positions, position, isEnabled(), isMouseOver());
}

void RackSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragStartY   = e.position.y;
    dragStartPos = position;

    // 两档：点哪儿都是拨到另一档（Rack 的 CKSS 就是这样）；三档：点哪一段就去哪一档
    if (positions == 2)
        setPosition (1 - position, juce::sendNotificationSync);
    else
        setPosition (juce::jlimit (0, 2, (int) (e.position.y / (float) juce::jmax (1, getHeight()) * 3.0f)),
                     juce::sendNotificationSync);
}

void RackSwitch::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || positions == 2)
        return;

    const int steps = (int) std::round ((e.position.y - dragStartY) / (9.0f * zoom));
    setPosition (dragStartPos + steps, juce::sendNotificationSync);
}

//==============================================================================
juce::Rectangle<float> RackLedRadio::segment (int i) const
{
    const int n    = juce::jmax (1, (int) items.size());
    const int rows = (n + cols - 1) / cols;
    const auto b   = getLocalBounds().toFloat();
    const float w  = b.getWidth() / (float) cols;
    const float h  = b.getHeight() / (float) rows;
    return { b.getX() + w * (float) (i % cols), b.getY() + h * (float) (i / cols), w, h };
}

void RackLedRadio::paint (juce::Graphics& g)
{
    const auto& p = rk::panel();
    const bool enabled = isEnabled();

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto seg = segment (i);
        const bool sel = i == selected;
        const bool hov = i == hover && enabled;

        if (hov)
        {
            g.setColour (rk::shade (p.text).withAlpha (0.07f));
            g.fillRoundedRectangle (seg.reduced (1.0f), 3.0f * zoom);
        }

        auto r = seg.reduced (4.0f * zoom, 0.0f);
        const float lr = 3.2f * zoom;
        rk::paintLight (g, { r.getX() + lr + 1.0f, r.getCentreY() }, lr, lightColour,
                        enabled ? (sel ? 1.0f : 0.0f) : 0.0f);
        r.removeFromLeft (lr * 2.0f + 6.0f * zoom);

        const auto& it = items[(size_t) i];
        const auto col = rk::shade (sel ? p.title : p.text).withMultipliedAlpha (enabled ? (sel ? 1.0f : 0.75f) : 0.4f);

        if (it.glyph != nullptr)
        {
            const float gw = juce::jmin (24.0f * zoom, r.getWidth() * 0.45f);
            it.glyph (g, r.removeFromLeft (gw).withSizeKeepingCentre (gw, juce::jmin (r.getHeight() - 4.0f, 13.0f * zoom)), col);
            r.removeFromLeft (4.0f * zoom);
        }

        g.setColour (col);
        g.setFont (rk::labelFont (it.text, 10.0f * zoom));
        g.drawFittedText (it.text, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }
}

//==============================================================================
RackLcd::RackLcd()
{
    setInterceptsMouseClicks (false, false);
}

void RackLcd::paint (juce::Graphics& g)
{
    rk::paintDisplay (g, getLocalBounds().toFloat(), zoom);

    const auto lines = juce::StringArray::fromLines (text);
    if (lines.isEmpty())
        return;

    const auto b   = getLocalBounds().toFloat().reduced (6.0f * zoom, 2.0f * zoom);
    const float lh = size * zoom * 1.32f;
    const float total = lh * (float) lines.size();

    float y = just.testFlags (juce::Justification::top)    ? b.getY()
            : just.testFlags (juce::Justification::bottom) ? b.getBottom() - total
                                                            : b.getCentreY() - total * 0.5f;

    const auto hj = juce::Justification (just.getOnlyHorizontalFlags() | juce::Justification::verticallyCentred);

    g.setColour (colour);
    for (const auto& l : lines)
    {
        g.setFont (rk::displayFont (l, size * zoom));
        g.drawText (l, juce::Rectangle<float> (b.getX(), y, b.getWidth(), lh), hj, true);
        y += lh;
    }
}

//==============================================================================
void RackMenuBar::setItems (std::vector<Item> it)
{
    items = std::move (it);
    repaint();
}

void RackMenuBar::setStatus (const juce::String& t, juce::Colour c)
{
    if (t == status && c == statusColour)
        return;

    status = t;
    statusColour = c;
    repaint();
}

juce::Rectangle<int> RackMenuBar::itemArea (int i) const
{
    const auto f = rk::uiFont (12.5f);
    int x = 6;

    for (int k = 0; k < (int) items.size(); ++k)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (f, items[(size_t) k].title) + 22;
        if (k == i)
            return { x, 0, w, getHeight() };
        x += w;
    }

    return { x, 0, 0, getHeight() };
}

int RackMenuBar::itemAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (itemArea (i).contains (p))
            return i;
    return -1;
}

void RackMenuBar::paint (juce::Graphics& g)
{
    g.fillAll (rk::uiBg);
    g.setColour (juce::Colours::black);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    g.setFont (rk::uiFont (12.5f));

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto r = itemArea (i);

        if (i == hover || i == open)
        {
            g.setColour (rk::ui (0.2f));
            g.fillRoundedRectangle (r.reduced (1, 3).toFloat(), 4.0f);
        }

        g.setColour (rk::uiFg);
        g.drawText (items[(size_t) i].title, r, juce::Justification::centred, false);
    }

    // 右边：状态文字，再往右是品牌（Rack 在这里显示 "VCV Rack Free 2.x" 和 CPU 表）
    auto right = getLocalBounds().reduced (10, 0);
    const auto brand = juce::String ("ParamRoulette  ") + (sizeof (void*) == 4 ? "x86" : "x64");
    g.setColour (rk::ui (0.45f));
    g.setFont (rk::uiFont (11.5f));
    const int bw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), brand) + 4;
    g.drawText (brand, right.removeFromRight (bw), juce::Justification::centredRight, false);

    right.removeFromRight (16);
    right.removeFromLeft (itemArea ((int) items.size()).getX() + 16);
    g.setColour (statusColour);
    g.setFont (rk::uiFont (12.0f));
    g.drawText (status, right, juce::Justification::centredRight, true);
}

void RackMenuBar::mouseMove (const juce::MouseEvent& e)
{
    const int h = itemAt (e.getPosition());
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void RackMenuBar::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void RackMenuBar::mouseDown (const juce::MouseEvent& e)
{
    if (const int i = itemAt (e.getPosition()); i >= 0)
        openMenu (i);
}

void RackMenuBar::openMenu (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) items.size()))
        return;

    juce::PopupMenu m;
    items[(size_t) i].build (m);

    open = i;
    repaint();

    juce::Component::SafePointer<RackMenuBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal (itemArea (i))),
                     [safe] (int)
                     {
                         if (auto* self = safe.getComponent())
                         {
                             self->open = -1;
                             self->repaint();
                         }
                     });
}

//==============================================================================
RackMenuSlider::RackMenuSlider (juce::String l, float v, float d, std::function<void (float)> cb)
    : juce::PopupMenu::CustomComponent (false),
      label (std::move (l)), value (v), defaultValue (d), onChange (std::move (cb))
{
}

void RackMenuSlider::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (8.0f, 3.0f);
    g.setColour (rk::ui (0.06f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (rk::ui (0.28f));
    g.fillRoundedRectangle (r.withWidth (juce::jmax (8.0f, r.getWidth() * value)), 4.0f);

    g.setColour (rk::uiFg);
    g.setFont (rk::uiFont (12.0f));
    g.drawText (label + "："_u8 + juce::String (juce::roundToInt (value * 100.0f)) + "%", r,
                juce::Justification::centred, false);
}

void RackMenuSlider::setFromX (float x)
{
    const auto r = getLocalBounds().toFloat().reduced (8.0f, 3.0f);
    value = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
    repaint();

    if (onChange != nullptr)
        onChange (value);
}

void RackMenuSlider::mouseDown (const juce::MouseEvent& e)          { setFromX (e.position.x); }
void RackMenuSlider::mouseDrag (const juce::MouseEvent& e)          { setFromX (e.position.x); }

void RackMenuSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    value = defaultValue;
    repaint();

    if (onChange != nullptr)
        onChange (value);
}

//==============================================================================
void RackTip::show (const juce::String& t, juce::Rectangle<int> target)
{
    text = t;

    const auto f = rk::uiFont (12.5f);
    const int w = juce::GlyphArrangement::getStringWidthInt (f, text) + 20;
    const int h = 24;

    auto r = juce::Rectangle<int> (w, h).withCentre ({ target.getCentreX(), target.getY() - h / 2 - 4 });

    if (auto* p = getParentComponent())
        r = r.constrainedWithin (p->getLocalBounds().reduced (2));

    setBounds (r);
    setVisible (true);
    toFront (false);
    repaint();
}

void RackTip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (rk::uiBg.withAlpha (0.96f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (rk::ui (0.2f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    g.setColour (rk::uiFg);
    g.setFont (rk::uiFont (12.5f));
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
juce::Rectangle<float> RackMemory::keyRect (int i) const
{
    const auto b  = getLocalBounds().toFloat();
    const float D = (i == 4 ? 24.0f : 30.0f) * zoom;

    if (i == 4)
        return juce::Rectangle<float> (D, D).withCentre ({ b.getCentreX(), b.getY() + 16.0f * zoom });

    const float top  = b.getY() + 62.0f * zoom;
    const float step = (b.getBottom() - top) / 4.0f;
    return juce::Rectangle<float> (D, D).withCentre ({ b.getCentreX() + 10.0f * zoom, top + step * ((float) i + 0.38f) });
}

void RackMemory::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const bool blink   = armed && ((juce::Time::getMillisecondCounter() / 250) % 2 == 0);

    // —— 写入键（红灯：按下它之后闪，表示"现在按数字是写"）
    {
        const auto k = keyRect (4);
        rk::paintBezel (g, k.getCentre(), k.getWidth(), rk::red, armed ? (blink ? 1.0f : 0.25f) : 0.0f,
                        pressed == 4 || armed, hover == 4 && enabled && canStore);
        rk::paintLabel (g, "写入"_u8, juce::Rectangle<float> (k.getX() - 30.0f * zoom, k.getBottom() + 2.0f * zoom,
                                                             k.getWidth() + 60.0f * zoom, 14.0f * zoom),
                        zoom, 10.0f, juce::Justification::centred, false, ! (enabled && canStore));
    }

    // —— 四颗灯键：熄 = 空，暗 = 存了东西，亮 = 当前声音就是它；待写入时一起闪
    for (int i = 0; i < 4; ++i)
    {
        const auto& t   = tiles[(size_t) i];
        const auto  k   = keyRect (i);
        const bool  win = t.used && i == winner;

        float b = 0.0f;
        if (armed)       b = blink ? 0.9f : 0.15f;
        else if (win)    b = 1.0f;
        else if (t.used) b = 0.28f;

        rk::paintBezel (g, k.getCentre(), k.getWidth(), rk::cableColour (i), enabled ? b : 0.0f,
                        pressed == i, hover == i && enabled);

        // 左边印编号，下面印这一格的情况（存了几个链位 / 改过的名字 / 空）
        rk::paintLabel (g, juce::String (i + 1),
                        juce::Rectangle<float> (getLocalBounds().toFloat().getX(), k.getY(), k.getX() - 6.0f * zoom, k.getHeight()),
                        zoom, 15.0f, juce::Justification::centredRight);

        const bool defaultName = t.name == "快照 "_u8 + juce::String (i + 1);
        const auto label = ! t.used    ? juce::String ("空"_u8)
                         : defaultName ? juce::String (t.numSlots) + " 链位"_u8
                                       : t.name;
        rk::paintLabel (g, label, juce::Rectangle<float> (k.getCentreX() - 40.0f * zoom, k.getBottom() + 4.0f * zoom, 80.0f * zoom, 13.0f * zoom),
                        zoom, 9.5f, juce::Justification::centred, false, true);
    }
}

} // namespace pr
