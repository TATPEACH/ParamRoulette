#include "HwWidgets.h"

namespace pr
{

using theme::Weight;

namespace
{
    juce::Rectangle<float> circleBox (juce::Point<float> c, float r) { return { c.x - r, c.y - r, r * 2.0f, r * 2.0f }; }
}

//==============================================================================
HwKnob::HwKnob()
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setRepaintsOnMouseActivity (true);
    setScrollWheelEnabled (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void HwKnob::mouseDown (const juce::MouseEvent& e)
{
    // 右键什么都不做（不转给 juce::Slider：它没开菜单时会把右键当成一次拖动，
    // 白白给 DAW 发一对 begin/end 手势）。双击的第二下也不开始拖——紧接着来的
    // mouseDoubleClick 会走 Slider 自带的复位，两边都发手势就嵌套了。
    if (e.mods.isPopupMenu() || ! isEnabled() || e.getNumberOfClicks() > 1)
        return;

    // Ctrl / Alt + 点 = 复位（和双击一样）。插件界面的通用手势，JUCE 自带的旋钮也认 Alt——
    // 这里拖动是自己写的，绕开了它那段代码，所以要自己补上。
    if ((e.mods.isCommandDown() || e.mods.isAltDown()) && isDoubleClickReturnEnabled())
    {
        if (onDragStart != nullptr) onDragStart();
        setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
        if (onDragEnd != nullptr) onDragEnd();
        return;
    }

    dragging   = true;
    last       = e.position;
    proportion = (float) valueToProportionOfLength (getValue());

    e.source.enableUnboundedMouseMovement (true, false);

    if (onDragStart != nullptr)
        onDragStart();

    repaint();
}

void HwKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto d = e.position - last;
    last = e.position;

    const float fine = e.mods.isShiftDown() ? 5.0f : 1.0f;
    proportion = juce::jlimit (0.0f, 1.0f, proportion + (d.x - d.y) / (pixelsPerRange * fine));

    setValue (proportionOfLengthToValue (proportion), juce::sendNotificationSync);
}

void HwKnob::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    dragging = false;
    e.source.enableUnboundedMouseMovement (false);

    if (onDragEnd != nullptr)
        onDragEnd();

    repaint();
}

//==============================================================================
HwBatToggle::HwBatToggle()
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    onLegend  = "开"_u8;
    offLegend = "关"_u8;
}

void HwBatToggle::setLegends (const juce::String& onText, const juce::String& offText)
{
    onLegend  = onText;
    offLegend = offText;
    repaint();
}

void HwBatToggle::paintButton (juce::Graphics& g, bool, bool)
{
    const float target = getToggleState() ? 1.0f : 0.0f;

    // 第一次画直接落在终点（截图 / 刚打开界面都不该看到一段动画）；之后状态变了才动
    if (! animInit)
    {
        anim = target;
        animInit = true;
    }
    else if (std::abs (anim - target) > 0.001f && ! isTimerRunning())
    {
        startTimerHz (60);
    }

    // 竖排：「● 开」/ 拨杆 / 「关」/ 说明文字。拨上去 = 开，灯跟着亮
    auto r = getLocalBounds().toFloat();
    const bool  enabled = isEnabled();
    const bool  on      = getToggleState();
    const float a       = enabled ? 1.0f : 0.45f;
    const auto  caption = getButtonText();

    auto captionRow = caption.isNotEmpty() ? r.removeFromBottom (15.0f) : juce::Rectangle<float>();
    auto onRow      = r.removeFromTop (14.0f);
    auto offRow     = r.removeFromBottom (14.0f);

    {
        const auto f  = hw::font (10.5f, Weight::bold);
        const float tw = juce::GlyphArrangement::getStringWidth (f, onLegend);
        const float x0 = onRow.getCentreX() - (tw + 14.0f) * 0.5f;

        hw::drawLed (g, { x0 + 3.5f, onRow.getCentreY() }, 3.0f, led, on && enabled ? 1.0f : 0.0f);

        g.setFont (f);
        g.setColour ((on ? hw::legend : hw::legendFaint).withMultipliedAlpha (a));
        g.drawText (onLegend, juce::Rectangle<float> (x0 + 14.0f, onRow.getY(), tw + 2.0f, onRow.getHeight()),
                    juce::Justification::centredLeft, false);
        g.setColour ((on ? hw::legendFaint : hw::legend).withMultipliedAlpha (a));
        g.drawText (offLegend, offRow, juce::Justification::centred, false);
    }

    hw::drawBatToggle (g, r, anim, enabled);

    if (! captionRow.isEmpty())
    {
        g.setFont (hw::font (9.5f, Weight::bold));
        g.setColour (hw::legendDim.withMultipliedAlpha (a));
        g.drawFittedText (caption, captionRow.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

void HwBatToggle::timerCallback()
{
    const float target = getToggleState() ? 1.0f : 0.0f;
    const float stepAmt = 1.0f / (0.09f * 60.0f);     // ~90ms 拨完

    if (std::abs (target - anim) <= stepAmt)
    {
        anim = target;
        stopTimer();
    }
    else
    {
        anim += target > anim ? stepAmt : -stepAmt;
    }

    repaint();
}

//==============================================================================
namespace
{
    /** VU 表的静态部分（外壳、表盘、刻度、字）。单独做成子组件并缓存成图——
        60Hz 刷新时只有指针在动，表盘每帧重画一遍是浪费。 */
    class VuFace final : public juce::Component
    {
    public:
        VuFace() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }

        juce::String caption;
        std::function<juce::Point<float>()> pivot;
        std::function<float (float)>        angleFor;
        juce::Rectangle<float>              face;

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();

            // —— 外壳：黑色塑料框
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (b.translated (0.0f, 2.0f), 7.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3137), 0.0f, b.getY(),
                                                     juce::Colour (0xff0f1215), 0.0f, b.getBottom(), false));
            g.fillRoundedRectangle (b, 7.0f);
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillRect (b.reduced (7.0f, 0.0f).withHeight (1.0f));

            // —— 表盘：暖色背光，中间亮、四周暗
            const auto f = face;
            g.setColour (juce::Colours::black);
            g.fillRoundedRectangle (f.expanded (1.5f), 3.5f);
            g.setGradientFill (juce::ColourGradient (hw::vuFace.brighter (0.12f), f.getCentreX(), f.getBottom() - f.getHeight() * 0.15f,
                                                     hw::vuFace.darker (0.32f), f.getX(), f.getY(), true));
            g.fillRoundedRectangle (f, 2.5f);

            const auto p   = pivot();
            const float L  = p.y - f.getY() - 6.0f;
            const float rs = L * 0.74f;       // 刻度弧半径：留够地方给外圈的数字，两端不被表框切掉

            // —— 刻度弧 + 红区
            {
                juce::Path arc;
                arc.addCentredArc (p.x, p.y, rs, rs, 0.0f, angleFor (0.0f), angleFor (HwVuMeter::deflectionForVu (0.0f)), true);
                g.setColour (hw::vuInk);
                g.strokePath (arc, juce::PathStrokeType (1.2f));

                juce::Path red;
                red.addCentredArc (p.x, p.y, rs + 1.5f, rs + 1.5f, 0.0f,
                                   angleFor (HwVuMeter::deflectionForVu (0.0f)), angleFor (1.0f), true);
                g.setColour (hw::vuRed);
                g.strokePath (red, juce::PathStrokeType (4.0f));
            }

            // —— 刻度线 + 数字（位置按真的 VU 表：偏转 ∝ 电压）
            struct Mark { float vu; const char* text; bool major; };
            static const Mark marks[] = { { -20, "20", true }, { -10, "10", true }, { -7, "7", true }, { -5, "5", true },
                                          { -3, "3", true }, { -2, "2", false }, { -1, "1", false }, { 0, "0", true },
                                          { 1, "1", false }, { 2, "2", false }, { 3, "3", true } };

            const float numR = rs + juce::jmax (9.0f, L * 0.13f);
            g.setFont (hw::legendFont (juce::jmax (7.5f, L * 0.1f), true));

            for (const auto& m : marks)
            {
                const float a  = angleFor (HwVuMeter::deflectionForVu (m.vu));
                const bool  hi = m.vu >= 0.0f;
                const auto  p0 = p.getPointOnCircumference (rs - (m.major ? 5.0f : 3.0f), a);
                const auto  p1 = p.getPointOnCircumference (rs + (m.major ? 4.0f : 2.0f), a);

                g.setColour (hi ? hw::vuRed : hw::vuInk);
                g.drawLine ({ p0, p1 }, m.major ? 1.4f : 1.0f);

                if (m.major)
                    g.drawText (m.text, juce::Rectangle<float> (20.0f, 11.0f).withCentre (p.getPointOnCircumference (numR, a)),
                                juce::Justification::centred, false);
            }

            // 两端的 − / +：放在弧线**下方**、往里收一点（真的 VU 表就是这么摆的）
            g.setFont (hw::legendFont (juce::jmax (9.0f, L * 0.13f), true));
            g.setColour (hw::vuInk);
            g.drawText (juce::String::fromUTF8 ("\xe2\x88\x92"),
                        juce::Rectangle<float> (14.0f, 12.0f).withCentre (p.getPointOnCircumference (rs - 13.0f, angleFor (0.04f))),
                        juce::Justification::centred, false);
            g.setColour (hw::vuRed);
            g.drawText ("+", juce::Rectangle<float> (14.0f, 12.0f).withCentre (p.getPointOnCircumference (rs - 13.0f, angleFor (0.96f))),
                        juce::Justification::centred, false);

            // —— VU 字样 + 小字
            g.setColour (hw::vuInk);
            g.setFont (hw::legendFont (juce::jmax (10.0f, L * 0.2f), true));
            g.drawText ("VU", juce::Rectangle<float> (f.getX(), p.y - L * 0.52f, f.getWidth(), L * 0.22f), juce::Justification::centred, false);

            g.setColour (hw::vuInk.withAlpha (0.65f));
            g.setFont (hw::legendFont (juce::jmax (6.5f, L * 0.075f), false));
            g.drawText (caption, juce::Rectangle<float> (f.getX() + 5.0f, f.getY() + 3.0f, f.getWidth() * 0.5f, 10.0f),
                        juce::Justification::centredLeft, false);
        }
    };
}

HwVuMeter::HwVuMeter (LevelSource s)
    : source (std::move (s))
{
    // 不响应点击，但要能被鼠标"指到"——提示气泡靠这个找到它
    setInterceptsMouseClicks (true, false);

    auto f = std::make_unique<VuFace>();
    f->pivot    = [this] { return pivot(); };
    f->angleFor = [this] (float d) { return needleAngle (d); };
    f->caption  = caption;
    addAndMakeVisible (*f);
    face = std::move (f);

    // 峰值是"自上次读以来的最大值"。Grid 皮肤不读它，切过来的时候里面攒着的是
    // 不知道多久以前的峰值——先读一次扔掉，免得一打开峰值灯就亮。
    if (source != nullptr)
    {
        float r = 0.0f, p = 0.0f;
        source (r, p);
    }

    startTimerHz (60);
}

void HwVuMeter::setCaption (const juce::String& s)
{
    caption = s;
    if (auto* f = dynamic_cast<VuFace*> (face.get()))
    {
        f->caption = s;
        f->repaint();
    }
}

void HwVuMeter::resized()
{
    if (auto* f = dynamic_cast<VuFace*> (face.get()))
    {
        f->face = faceArea();
        f->setBounds (getLocalBounds());
        f->repaint();
    }
}

float HwVuMeter::deflectionForVu (float vu)
{
    return 0.708f * std::pow (10.0f, vu / 20.0f);
}

juce::Rectangle<float> HwVuMeter::faceArea() const
{
    auto b = getLocalBounds().toFloat().reduced (7.0f);
    return b.withTrimmedBottom (b.getHeight() * 0.2f);
}

juce::Point<float> HwVuMeter::pivot() const
{
    const auto f = faceArea();
    return { f.getCentreX(), f.getBottom() + f.getHeight() * 0.42f };
}

float HwVuMeter::needleAngle (float d) const
{
    return -kSweep + 2.0f * kSweep * juce::jlimit (-0.03f, 1.06f, d);
}

void HwVuMeter::timerCallback()
{
    if (! isShowing())
        return;

    const auto now = juce::Time::getMillisecondCounter();
    const float dt = lastTick == 0 ? 1.0f / 60.0f : juce::jlimit (0.0f, 0.05f, (float) (now - lastTick) * 0.001f);
    lastTick = now;

    float rms = 0.0f, peak = 0.0f;
    if (source != nullptr)
        source (rms, peak);

    // 0 VU = −18 dBFS RMS
    constexpr float ref = 0.12589f;
    const float target = juce::jlimit (0.0f, 1.08f, 0.708f * rms / ref);

    // 二阶弹道：x'' = ω²(目标 − x) − 2ζω·x'。分小步积分，大 dt 下也稳定。
    constexpr float w = 13.0f, zeta = 0.78f;
    const float before = needle;
    const int   steps  = juce::jmax (1, (int) std::ceil (dt / (1.0f / 240.0f)));
    const float h      = dt / (float) steps;

    for (int i = 0; i < steps; ++i)
    {
        const float acc = w * w * (target - needle) - 2.0f * zeta * w * velocity;
        velocity += acc * h;
        needle   += velocity * h;
    }

    // 峰值灯：−1 dBFS 以上亮，撑 0.6 秒
    if (peak >= 0.891f)
        peakUntil = now + 600;

    const bool lit = now < peakUntil;

    if (std::abs (needle - before) > 0.0004f || lit != peakLit)
    {
        peakLit = lit;
        repaint();
    }
}

void HwVuMeter::paintOverChildren (juce::Graphics& g)
{
    const auto f = faceArea();
    const auto p = pivot();
    const float L = p.y - f.getY() - 6.0f;
    const float a = needleAngle (needle);

    // 指针：落在表盘上的影子 + 黑色细针
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (f.toNearestInt());

        const auto tip  = p.getPointOnCircumference (L, a);
        const auto root = p.getPointOnCircumference (L * 0.35f, a);
        g.setColour (juce::Colours::black.withAlpha (0.13f));
        g.drawLine ({ root.translated (2.2f, 2.6f), tip.translated (2.2f, 2.6f) }, 1.3f);
        g.setColour (juce::Colour (0xff15110c));
        g.drawLine ({ root, tip }, 1.3f);
    }

    // 下沿压条：遮住指针根部（真的 VU 表也是这样，转轴藏在下面）
    const auto strip = juce::Rectangle<float> (f.getX() - 1.5f, f.getBottom() - 1.0f, f.getWidth() + 3.0f,
                                               (float) getHeight() - 7.0f - f.getBottom() + 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23272c), 0.0f, strip.getY(),
                                             juce::Colour (0xff0d1013), 0.0f, strip.getBottom(), false));
    g.fillRoundedRectangle (strip, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (strip.withHeight (1.0f));

    // 峰值灯 + 小字
    const auto ledC = juce::Point<float> (strip.getRight() - 12.0f, strip.getCentreY());
    hw::drawLed (g, ledC, 3.2f, hw::ledRed, peakLit ? 1.0f : 0.0f);
    g.setColour (hw::legendFaint);
    g.setFont (hw::legendFont (8.0f, true));
    g.drawText ("PEAK", juce::Rectangle<float> (ledC.x - 40.0f, strip.getY(), 30.0f, strip.getHeight()),
                juce::Justification::centredRight, false);

    hw::drawGlassSheen (g, f, 2.5f, 1.3f);
}

//==============================================================================
HwDigitStepper::HwDigitStepper()
{
    setRepaintsOnMouseActivity (true);
}

void HwDigitStepper::setValue (int v, juce::NotificationType n)
{
    v = juce::jlimit (minV, maxV, v);
    if (v == value)
        return;

    value = v;
    repaint();

    if (n != juce::dontSendNotification && onChange != nullptr)
        onChange (value);
}

namespace
{
    float stepperKeyWidth (juce::Rectangle<float> r) { return juce::jmax (20.0f, r.getHeight() * 0.74f); }
}

juce::Rectangle<float> HwDigitStepper::displayArea() const
{
    auto r = getLocalBounds().toFloat();
    return r.withTrimmedRight (stepperKeyWidth (r) + 5.0f);
}

juce::Rectangle<float> HwDigitStepper::upArea() const
{
    auto r = getLocalBounds().toFloat();
    auto col = r.removeFromRight (stepperKeyWidth (r));
    return col.removeFromTop (col.getHeight() * 0.5f);
}

juce::Rectangle<float> HwDigitStepper::downArea() const
{
    auto r = getLocalBounds().toFloat();
    auto col = r.removeFromRight (stepperKeyWidth (r));
    return col.removeFromBottom (col.getHeight() * 0.5f);
}

void HwDigitStepper::step (int delta)
{
    setValue (value + delta, juce::sendNotificationSync);
}

void HwDigitStepper::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const auto d = displayArea();

    // 数码管窗口：深红色的滤光片后面是红色 LED 段
    hw::drawRecess (g, d, 4.0f, juce::Colour (0xff120607));
    const auto txt = value <= 0 ? juce::String ("--") : juce::String (value).paddedLeft ('0', 2);
    hw::drawSevenSegment (g, d.reduced (d.getHeight() * 0.18f, d.getHeight() * 0.16f), txt,
                          enabled ? hw::ledRed : hw::ledRed.withAlpha (0.35f));
    hw::drawGlassSheen (g, d.reduced (1.0f), 3.0f);

    for (auto [area, up] : { std::pair { upArea(), true }, std::pair { downArea(), false } })
    {
        const bool active = enabled && (up ? value < maxV : value > minV);
        const bool down   = pressed == (up ? 1 : -1);
        const auto top    = hw::drawKey (g, area, down, hover == (up ? 1 : -1), hw::rubber, 3.0f, active);

        juce::Path tri;
        const auto c = top.getCentre();
        const float s = juce::jmin (top.getWidth(), top.getHeight()) * 0.22f;
        if (up) tri.addTriangle (c.x - s, c.y + s * 0.6f, c.x + s, c.y + s * 0.6f, c.x, c.y - s * 0.7f);
        else    tri.addTriangle (c.x - s, c.y - s * 0.6f, c.x + s, c.y - s * 0.6f, c.x, c.y + s * 0.7f);
        g.setColour (active ? hw::legend : hw::legendFaint.withAlpha (0.6f));
        g.fillPath (tri);
    }
}

void HwDigitStepper::mouseMove (const juce::MouseEvent& e)
{
    const int h = upArea().contains (e.position) ? 1 : downArea().contains (e.position) ? -1 : 0;
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
}

void HwDigitStepper::mouseExit (const juce::MouseEvent&)
{
    hover = 0;
    repaint();
}

void HwDigitStepper::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (upArea().contains (e.position))        { pressed = 1;  step (+1); }
    else if (downArea().contains (e.position)) { pressed = -1; step (-1); }
    else
    {
        dragDigits = true;
        dragAcc = 0.0f;
        lastY = e.position.y;
    }

    repaint();
}

void HwDigitStepper::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragDigits)
        return;

    // 在数码管上上下拖：每 9px 一格（像拨一个拇指轮）
    dragAcc += lastY - e.position.y;
    lastY = e.position.y;

    while (dragAcc >= 9.0f)  { step (+1); dragAcc -= 9.0f; }
    while (dragAcc <= -9.0f) { step (-1); dragAcc += 9.0f; }
}

void HwDigitStepper::mouseUp (const juce::MouseEvent&)
{
    pressed = 0;
    dragDigits = false;
    repaint();
}

void HwDigitStepper::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isEnabled() && displayArea().contains (e.position))
        setValue (minV, juce::sendNotificationSync);
}

void HwDigitStepper::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! isEnabled())
        return;

    wheelAcc += w.deltaY;
    while (wheelAcc >= 0.1f)  { step (+1); wheelAcc -= 0.1f; }
    while (wheelAcc <= -0.1f) { step (-1); wheelAcc += 0.1f; }
}

//==============================================================================
HwRotarySelector::HwRotarySelector()
{
    setRepaintsOnMouseActivity (true);
}

void HwRotarySelector::setItems (std::vector<SegmentedControl::Item> newItems)
{
    items = std::move (newItems);
    selected = juce::jlimit (0, juce::jmax (0, (int) items.size() - 1), selected);
    shownAngle = detentAngle (selected);
    repaint();
}

void HwRotarySelector::setSelected (int index, juce::NotificationType n)
{
    index = juce::jlimit (0, juce::jmax (0, (int) items.size() - 1), index);

    if (index == selected)
        return;

    selected = index;

    // 外部同步（恢复工程、自动化）直接到位；用户手拨的才有动画
    if (n == juce::dontSendNotification)
        shownAngle = detentAngle (selected);
    else
        startTimerHz (60);

    repaint();

    if (n != juce::dontSendNotification && onChange != nullptr)
        onChange (selected);
}

void HwRotarySelector::choose (int i)
{
    if (isEnabled())
        setSelected (i, juce::sendNotificationSync);
}

void HwRotarySelector::resized()
{
    const auto b = getLocalBounds().toFloat();
    knobR  = juce::jlimit (14.0f, 30.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.2f);
    glyphR = knobR + 22.0f;
    centre = { b.getCentreX(), b.getCentreY() + 4.0f };
}

float HwRotarySelector::detentAngle (int i) const
{
    const int n = juce::jmax (2, (int) items.size());
    constexpr float span = juce::MathConstants<float>::pi * 0.72f;   // ±130°
    return -span + 2.0f * span * (float) i / (float) (n - 1);
}

juce::Rectangle<float> HwRotarySelector::glyphBox (int i) const
{
    return juce::Rectangle<float> (26.0f, 17.0f).withCentre (centre.getPointOnCircumference (glyphR, detentAngle (i)));
}

int HwRotarySelector::glyphAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (glyphBox (i).expanded (4.0f, 3.0f).contains (p))
            return i;
    return -1;
}

bool HwRotarySelector::overKnob (juce::Point<float> p) const
{
    return p.getDistanceFrom (centre) <= knobR + 6.0f;
}

void HwRotarySelector::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const float a = enabled ? 1.0f : 0.45f;

    // 档位刻度：每档一颗点，再往外是那一档的图形
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const bool sel = (i == selected);
        const bool hov = (i == hover) && enabled;
        const auto dot = centre.getPointOnCircumference (knobR + 7.0f, detentAngle (i));

        g.setColour (hw::legend.withMultipliedAlpha (a * (sel ? 1.0f : 0.55f)));
        g.fillEllipse (circleBox (dot, sel ? 2.0f : 1.4f));

        const auto box = glyphBox (i);
        if (sel)
        {
            g.setColour (hw::ledCyan.withAlpha (0.14f * a));
            g.fillRoundedRectangle (box.expanded (3.0f, 2.0f), 4.0f);
        }

        const auto col = sel ? (enabled ? hw::ledCyan.brighter (0.2f) : hw::legendDim)
                             : (hov ? hw::legend : hw::legendDim.withMultipliedAlpha (a * 0.85f));

        if (items[(size_t) i].glyph != nullptr)
            items[(size_t) i].glyph (g, box, col);
    }

    hw::drawKnob (g, circleBox (centre, knobR), shownAngle, hw::Knob::pointerBlack, enabled,
                  enabled && (isMouseOver() && hover < 0));
}

void HwRotarySelector::timerCallback()
{
    const float target = detentAngle (selected);
    const float d = target - shownAngle;

    if (std::abs (d) < 0.01f)
    {
        shownAngle = target;
        stopTimer();
    }
    else
    {
        shownAngle += d * 0.45f;    // 指数逼近：~70ms 到位，开头快结尾慢，像拨到档位上"咔"一下
    }

    repaint();
}

void HwRotarySelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = glyphAt (e.position);
    if (h != hover) { hover = h; repaint(); }

    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor
                           : overKnob (e.position) ? juce::MouseCursor::UpDownResizeCursor
                                                   : juce::MouseCursor::NormalCursor);
}

void HwRotarySelector::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void HwRotarySelector::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (const int i = glyphAt (e.position); i >= 0)
    {
        choose (i);
        return;
    }

    if (overKnob (e.position))
    {
        dragging = true;
        dragAcc = 0.0f;
        lastDrag = e.position;
    }
}

void HwRotarySelector::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    // 上 / 右 = 顺时针一档。每 22px 一档：比连续旋钮"重"，拨档的手感
    dragAcc += (e.position.x - lastDrag.x) - (e.position.y - lastDrag.y);
    lastDrag = e.position;

    while (dragAcc >= 22.0f)  { choose (juce::jmin ((int) items.size() - 1, selected + 1)); dragAcc -= 22.0f; }
    while (dragAcc <= -22.0f) { choose (juce::jmax (0, selected - 1));                       dragAcc += 22.0f; }
}

void HwRotarySelector::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void HwRotarySelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! isEnabled())
        return;

    wheelAcc += w.deltaY;
    while (wheelAcc >= 0.1f)  { choose (juce::jmin ((int) items.size() - 1, selected + 1)); wheelAcc -= 0.1f; }
    while (wheelAcc <= -0.1f) { choose (juce::jmax (0, selected - 1));                       wheelAcc += 0.1f; }
}

juce::String HwRotarySelector::getTooltip()
{
    const int i = hover >= 0 ? hover : selected;
    if (juce::isPositiveAndBelow (i, (int) items.size()))
        return items[(size_t) i].tooltip + (hover < 0 ? "\n\n拖动旋钮或滚轮换档，也可以直接点图形"_u8 : juce::String());
    return {};
}

//==============================================================================
HwMemoryBank::HwMemoryBank()
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HwMemoryBank::setTiles (const std::array<SnapshotTiles::Tile, 4>& t, int w, bool store)
{
    bool same = (w == winner) && (store == canStore);
    for (size_t i = 0; i < 4 && same; ++i)
        same = t[i].used == tiles[i].used && t[i].name == tiles[i].name && t[i].numSlots == tiles[i].numSlots;

    if (same)
        return;

    tiles = t;
    winner = w;
    canStore = store;

    if (! canStore && armed)
        setArmed (false);

    repaint();
}

void HwMemoryBank::setArmed (bool a)
{
    armed = a && canStore;
    armedAt = juce::Time::getMillisecondCounter();

    if (armed) startTimerHz (8);
    else       stopTimer();

    repaint();
}

void HwMemoryBank::timerCallback()
{
    // 写入键是「按一下 → 选格子」的两步手势；6 秒不选就当作反悔了
    if (armed && juce::Time::getMillisecondCounter() - armedAt > 6000)
    {
        setArmed (false);
        return;
    }

    repaint();
}

juce::Rectangle<float> HwMemoryBank::keyRect (int i) const
{
    const auto r = getLocalBounds().toFloat();
    constexpr float nameH = 16.0f, storeW = 48.0f, gapPx = 7.0f;

    const float keyH = juce::jmin (r.getHeight() - nameH - 2.0f, 52.0f);
    const float kw   = (r.getWidth() - storeW - 12.0f - 3.0f * gapPx) / 4.0f;

    if (i == 4)
        return { r.getX(), r.getY(), storeW, keyH };

    return { r.getX() + storeW + 12.0f + (float) i * (kw + gapPx), r.getY(), kw, keyH };
}

int HwMemoryBank::keyAt (juce::Point<float> p) const
{
    for (int i = 0; i < 5; ++i)
        if (keyRect (i).contains (p))
            return i;
    return -1;
}

void HwMemoryBank::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const bool blink   = armed && ((juce::Time::getMillisecondCounter() / 250) % 2 == 0);

    // —— 写入键
    {
        const auto k   = keyRect (4);
        const auto top = hw::drawKey (g, k, pressed == 4 || armed, hover == 4, hw::rubber, 4.0f, enabled && canStore);
        hw::drawLed (g, { top.getCentreX(), top.getY() + 9.0f }, 3.0f, hw::ledRed, armed ? (blink ? 1.0f : 0.25f) : 0.0f);

        g.setFont (hw::font (11.5f, Weight::bold));
        g.setColour (enabled && canStore ? hw::legend : hw::legendFaint);
        g.drawText ("写入"_u8, top.withTrimmedTop (16.0f), juce::Justification::centred, false);

        g.setFont (hw::legendFont (8.5f, true));
        g.setColour (hw::legendFaint);
        g.drawText ("STORE", juce::Rectangle<float> (k.getX(), k.getBottom() + 1.0f, k.getWidth(), 14.0f),
                    juce::Justification::centred, false);
    }

    // —— 四颗数字键
    for (int i = 0; i < 4; ++i)
    {
        const auto& t   = tiles[(size_t) i];
        const auto  k   = keyRect (i);
        const bool  win = t.used && i == winner;
        const auto  top = hw::drawKey (g, k, pressed == i, hover == i, win ? hw::rubber.brighter (0.12f) : hw::rubber,
                                       4.0f, enabled);

        // 灯：写入待选时一起闪琥珀；否则 熄 = 空 / 暗 = 存了 / 亮 = 当前
        float b = 0.0f;
        auto  c = hw::ledAmber;
        if (armed)       { b = blink ? 0.9f : 0.15f; }
        else if (win)    { b = 1.0f; c = hw::ledGreen; }
        else if (t.used) { b = 0.32f; c = hw::ledGreen; }

        hw::drawLed (g, { top.getRight() - 8.5f, top.getY() + 8.5f }, 2.8f, c, enabled ? b : 0.0f);

        g.setFont (hw::legendFont (juce::jmin (20.0f, top.getHeight() * 0.48f), true));
        g.setColour (enabled ? (t.used ? hw::legend : hw::legendDim) : hw::legendFaint);
        g.drawText (juce::String (i + 1), top.translated (-2.0f, 1.0f), juce::Justification::centred, false);

        // 键下面印这一格的情况：存了几个链位（改过名的印名字），空的印「空」。
        // 键帽上已经有编号了，再印一遍「快照 1」是废话。
        g.setFont (hw::font (10.0f, Weight::medium));
        g.setColour (t.used ? hw::legendDim : hw::legendFaint);
        const bool defaultName = t.name == "快照 "_u8 + juce::String (i + 1);
        const auto label = ! t.used    ? juce::String ("空"_u8)
                         : defaultName ? juce::String (t.numSlots) + " 链位"_u8
                                       : t.name;
        g.drawFittedText (label, juce::Rectangle<float> (k.getX() - 2.0f, k.getBottom() + 1.0f, k.getWidth() + 4.0f, 15.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.75f);
    }
}

void HwMemoryBank::mouseMove (const juce::MouseEvent& e)
{
    const int h = keyAt (e.position);
    if (h != hover) { hover = h; repaint(); }
}

void HwMemoryBank::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void HwMemoryBank::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    const int i = keyAt (e.position);
    if (i < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        if (i < 4)
            showMenu (i);
        return;
    }

    pressed = i;
    repaint();
}

void HwMemoryBank::mouseUp (const juce::MouseEvent& e)
{
    const int i = keyAt (e.position);
    const int was = pressed;
    pressed = -1;
    repaint();

    if (i < 0 || i != was || ! isEnabled())
        return;

    if (i == 4)
    {
        setArmed (! armed);
        return;
    }

    if (armed)
    {
        setArmed (false);
        if (canStore && onStore != nullptr)
            onStore (i);
        return;
    }

    if (tiles[(size_t) i].used) { if (onRecall != nullptr) onRecall (i); }
    else if (canStore)          { if (onStore  != nullptr) onStore (i); }
}

void HwMemoryBank::showMenu (int i)
{
    const bool used = tiles[(size_t) i].used;

    juce::PopupMenu m;
    m.addSectionHeader ("快照 "_u8 + juce::String (i + 1));
    m.addItem (1, used ? "召回"_u8 : "（空）"_u8, used);
    m.addItem (2, used ? "用当前状态覆盖"_u8 : "存入当前状态"_u8, canStore);
    m.addSeparator();
    m.addItem (3, "清空"_u8, used);

    juce::Component::SafePointer<HwMemoryBank> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal (keyRect (i).toNearestInt())),
                     [safe, i] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr) return;

        if (r == 1 && self->onRecall != nullptr) self->onRecall (i);
        if (r == 2 && self->onStore  != nullptr) self->onStore (i);
        if (r == 3 && self->onClear  != nullptr) self->onClear (i);
    });
}

juce::String HwMemoryBank::getTooltip()
{
    if (hover == 4)
        return armed ? "再按一次取消写入"_u8
                     : "写入：先按这颗、再按 1~4，把当前整条链存进那一格（覆盖）。Shift+1~4 是同一个操作"_u8;

    if (! juce::isPositiveAndBelow (hover, 4))
        return {};

    const int n = hover + 1;

    if (armed)
        return "把当前整条链写进快照 "_u8 + juce::String (n);

    return tiles[(size_t) hover].used
             ? "召回快照 "_u8 + juce::String (n) + "（键盘 "_u8 + juce::String (n) + "）。右键：覆盖 / 清空"_u8
             : "空的。按一下把当前整条链存进来（Shift+"_u8 + juce::String (n) + "）"_u8;
}

//==============================================================================
void HwReadout::paint (juce::Graphics& g)
{
    const auto lines = juce::StringArray::fromLines (text);
    if (lines.isEmpty())
        return;

    const auto b = getLocalBounds().toFloat();
    const float lineH = juce::jmax (12.0f, font.getHeight() * 1.05f);
    const float total = lineH * (float) lines.size();

    // 多行时整体按 just 的竖直方向摆，每行各自水平对齐
    float y = just.testFlags (juce::Justification::top) ? b.getY()
            : just.testFlags (juce::Justification::bottom) ? b.getBottom() - total
            : b.getCentreY() - total * 0.5f;

    const auto hj = juce::Justification (just.getOnlyHorizontalFlags() | juce::Justification::verticallyCentred);

    for (const auto& l : lines)
    {
        hw::drawGlowText (g, l, { b.getX(), y, b.getWidth(), lineH }, colour, font, hj, true, glow);
        y += lineH;
    }
}

//==============================================================================
void HwInterlock::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto seg = segment (i).reduced (1.5f, 0.0f);
        const bool sel = (i == selected);
        const bool hov = (i == hover) && enabled;

        // 按下去的那颗沉着、灯亮；其余弹起
        const auto top = hw::drawKey (g, seg, sel, hov, sel ? hw::rubber.darker (0.08f) : hw::rubber, 3.5f, enabled);

        auto r = top.reduced (6.0f, 0.0f);
        hw::drawLed (g, { r.getX() + 3.5f, r.getCentreY() }, 2.6f, accent, sel && enabled ? 1.0f : 0.0f);
        r.removeFromLeft (10.0f);

        const auto& it = items[(size_t) i];
        const auto c = enabled ? (sel ? hw::legend.brighter (0.3f) : hw::legendDim) : hw::legendFaint;

        if (it.glyph != nullptr)
        {
            const float s = juce::jmin (r.getHeight() - 8.0f, r.getWidth() - 4.0f);
            it.glyph (g, r.withSizeKeepingCentre (s * 1.25f, s), c);
        }
        else
        {
            g.setColour (c);
            g.setFont (hw::isAscii (it.text) ? hw::legendFont (12.0f, true) : hw::font (11.5f, Weight::bold));
            g.drawFittedText (it.text, r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }
}

} // namespace pr
