#include "MorphPad.h"

namespace pr
{

/** 四个 slot 各给一个色。和 ParamListView 用的是同一套调色板。 */
juce::Colour slotColour (int slot)
{
    static const juce::Colour c[kNumSlots] = { juce::Colour (0xff5aa9e6),   // 蓝
                                               juce::Colour (0xff6aa84f),   // 绿
                                               juce::Colour (0xffd9a441),   // 琥珀
                                               juce::Colour (0xffb07cd6) }; // 紫
    return c[(size_t) juce::jlimit (0, kNumSlots - 1, slot)];
}

namespace
{
    juce::String slotLabel (const SnapshotBank& b, int slot)
    {
        if (! juce::isPositiveAndBelow (slot, kNumSlots))
            return {};

        const auto n = b[slot].name;
        return n.isNotEmpty() ? n : ("Slot " + juce::String (slot + 1));
    }

    /** 文字框贴着组件边缘时会被裁掉半个字，先把它推回可视范围。 */
    juce::Rectangle<int> clampLabel (juce::Rectangle<int> label, juce::Rectangle<int> bounds)
    {
        return label.withX (juce::jlimit (bounds.getX(),
                                          juce::jmax (bounds.getX(), bounds.getRight() - label.getWidth()),
                                          label.getX()));
    }
}

//==============================================================================
MorphPad::MorphPad (const SnapshotBank& bankRef)
    : bank (bankRef)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MorphPad::setMode (MorphMode m)
{
    if (mode == m)
        return;

    // 只换视图，**不动 posY**。用户在 2D 里拖到某个位置、切去 1D 看一眼再切回来，
    // 位置应该还在原处——把 y 清零等于偷偷改了声音。
    mode = m;
    repaint();
}

void MorphPad::setPosition (float x, float y)
{
    // 拖动中拒绝外部同步：自动化回读和用户的手会互相打架，手优先。
    if (dragging)
        return;

    const float nx = juce::jlimit (0.0f, 1.0f, x);
    const float ny = juce::jlimit (0.0f, 1.0f, y);

    if (juce::approximatelyEqual (nx, posX) && juce::approximatelyEqual (ny, posY))
        return;

    posX = nx;
    posY = ny;
    repaint();
}

//==============================================================================
juce::Rectangle<float> MorphPad::padArea() const
{
    auto r = getLocalBounds().reduced (kMargin).withTrimmedBottom (kReadoutH).toFloat();

    if (r.getWidth() < 8.0f || r.getHeight() < 8.0f)
        return r;

    if (mode == MorphMode::TwoD)
    {
        // 正方形才像个 pad：长条形的双线性插值在视觉上会被拉歪。
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        return r.withSizeKeepingCentre (s, s);
    }

    // 1D：两端留出手柄半径，免得拖到头时画出框外。
    return r.reduced (kHandleR + 2.0f, 0.0f);
}

void MorphPad::positionFromMouse (const juce::MouseEvent& e)
{
    const auto r = padArea();
    if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f)
        return;

    posX = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());

    // 1D 模式下 y 不参与计算，也就不该被鼠标改掉——留着给切回 2D 时用。
    if (mode == MorphMode::TwoD)
        posY = juce::jlimit (0.0f, 1.0f, (e.position.y - r.getY()) / r.getHeight());

    if (onPositionChanged != nullptr)
        onPositionChanged (posX, posY);

    repaint();
}

//==============================================================================
void MorphPad::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;

    if (onDragStart != nullptr)
        onDragStart();

    positionFromMouse (e);
}

void MorphPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        positionFromMouse (e);
}

void MorphPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;

    if (onDragEnd != nullptr)
        onDragEnd();
}

void MorphPad::mouseDoubleClick (const juce::MouseEvent&)
{
    // 回中。走完整的手势（begin → change → end），否则 DAW 录不到这一下。
    if (onDragStart != nullptr)
        onDragStart();

    posX = 0.5f;
    if (mode == MorphMode::TwoD)
        posY = 0.5f;

    if (onPositionChanged != nullptr)
        onPositionChanged (posX, posY);

    if (onDragEnd != nullptr)
        onDragEnd();

    repaint();
}

//==============================================================================
void MorphPad::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff222222));

    auto full = getLocalBounds();
    auto readout = full.removeFromBottom (kReadoutH + kMargin / 2).reduced (kMargin, 0);

    const auto r = padArea();

    if (r.getWidth() < 8.0f || r.getHeight() < 8.0f)
        return;

    if (bank.numUsed() == 0)
    {
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (juce::Colour (0xff6a6a6a));
        g.setFont (13.0f);
        g.drawText (hintText(), r.toNearestInt(), juce::Justification::centred, true);
        return;
    }

    if (mode == MorphMode::TwoD)
        paintTwoD (g, r);
    else
        paintOneD (g, r);

    paintReadout (g, readout.toFloat());
}

void MorphPad::paintOneD (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float cy = r.getCentreY();

    // 轨道
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX(), cy - 2.0f, r.getWidth(), 4.0f), 2.0f);

    // 刻度：已用 slot 按**序号顺序**压实。只存了 1 和 3 的话，线上就只有两点，
    // 左端是 1、右端是 3——和 SnapshotBank::computeWeights 里 `x * (N-1)` 完全一致。
    int used[kNumSlots];
    int nUsed = 0;
    for (int i = 0; i < kNumSlots; ++i)
        if (bank.isUsed (i))
            used[nUsed++] = i;

    for (int i = 0; i < nUsed; ++i)
    {
        const float t = (nUsed == 1) ? 0.5f : (float) i / (float) (nUsed - 1);
        const juce::Point<float> c { r.getX() + t * r.getWidth(), cy };

        paintSlotDot (g, c, used[i], kSlotR);

        auto label = clampLabel ({ (int) c.x - 40, (int) (cy + kSlotR + 4.0f), 80, 14 }, getLocalBounds());
        g.setColour (juce::Colour (0xff9a9a9a));
        g.setFont (11.0f);
        g.drawText (slotLabel (bank, used[i]), label, juce::Justification::centred, true);
    }

    paintHandle (g, { r.getX() + posX * r.getWidth(), cy });
}

void MorphPad::paintTwoD (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff1c1c1c));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    // 十字辅助线
    g.setColour (juce::Colour (0xff2e2e2e));
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);
    g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom(), 1.0f);

    // 四角固定绑 slot 0..3，摆位 0──1 / 2──3，y 向下。
    const juce::Point<float> corner[kNumSlots] =
    {
        { r.getX(),     r.getY()      },
        { r.getRight(), r.getY()      },
        { r.getX(),     r.getBottom() },
        { r.getRight(), r.getBottom() }
    };

    const juce::Point<float> handle { r.getX() + posX * r.getWidth(),
                                      r.getY() + posY * r.getHeight() };

    // 从手柄拉向每个已用角的连线，粗细/透明度跟着权重走——
    // 光看手柄位置猜不出权重，这条线是唯一直观的反馈。
    const auto w = bank.computeWeights (MorphMode::TwoD, posX, posY);

    for (int s = 0; s < kNumSlots; ++s)
    {
        const float weight = w.weightOf (s);
        if (weight <= 0.001f)
            continue;

        g.setColour (slotColour (s).withAlpha (0.15f + 0.45f * weight));
        g.drawLine (handle.x, handle.y, corner[s].x, corner[s].y, 1.0f + 2.0f * weight);
    }

    for (int s = 0; s < kNumSlots; ++s)
    {
        paintSlotDot (g, corner[s], s, kSlotR);

        if (! bank.isUsed (s))
            continue;

        const bool left = (s == 0 || s == 2);
        const bool top  = (s == 0 || s == 1);

        const int lw = 84;
        auto label = clampLabel ({ left ? (int) r.getX() + 10 : (int) r.getRight() - 10 - lw,
                                   top  ? (int) r.getY() + 10 : (int) r.getBottom() - 10 - 14,
                                   lw, 14 },
                                 getLocalBounds());

        g.setColour (juce::Colour (0xff9a9a9a));
        g.setFont (11.0f);
        g.drawText (slotLabel (bank, s), label,
                    left ? juce::Justification::centredLeft : juce::Justification::centredRight, true);
    }

    paintHandle (g, handle);
}

void MorphPad::paintSlotDot (juce::Graphics& g, juce::Point<float> centre, int slot, float radius)
{
    const auto box = juce::Rectangle<float> (centre.x - radius, centre.y - radius,
                                             radius * 2.0f, radius * 2.0f);
    const bool used = bank.isUsed (slot);

    // winner 每帧现读：capture() 会把它重置成 -1，缓存下来就会指向已经不存在的 slot。
    const bool winner = used && (bank.getWinner() == slot);

    if (used)
    {
        g.setColour (slotColour (slot));
        g.fillEllipse (box);

        if (winner)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (box.expanded (2.5f), 1.6f);
        }
    }
    else
    {
        g.setColour (juce::Colour (0xff4a4a4a));
        g.drawEllipse (box, 1.0f);
    }

    g.setColour (used ? juce::Colours::black.withAlpha (0.75f) : juce::Colour (0xff5a5a5a));
    g.setFont (10.0f);
    g.drawText (juce::String (slot + 1), box.toNearestInt(), juce::Justification::centred, false);
}

void MorphPad::paintHandle (juce::Graphics& g, juce::Point<float> centre)
{
    const auto box = juce::Rectangle<float> (centre.x - kHandleR, centre.y - kHandleR,
                                             kHandleR * 2.0f, kHandleR * 2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (box.expanded (1.5f));

    g.setColour (bank.numUsed() >= 2 ? juce::Colours::white : juce::Colour (0xff777777));
    g.fillEllipse (box.reduced (2.0f));
}

void MorphPad::paintReadout (juce::Graphics& g, juce::Rectangle<float> strip)
{
    juce::String text;

    if (bank.numUsed() < 2)
    {
        text = hintText();
        g.setColour (juce::Colour (0xffd9a441));
    }
    else
    {
        const auto w = bank.computeWeights (mode, posX, posY);

        for (int s = 0; s < kNumSlots; ++s)
        {
            const float weight = w.weightOf (s);
            if (weight <= 0.005f)
                continue;

            text << (text.isEmpty() ? "" : "   ")
                 << (s + 1) << ":" << juce::roundToInt (weight * 100.0f) << "%";
        }

        g.setColour (juce::Colour (0xff9a9a9a));
    }

    g.setFont (11.0f);
    g.drawText (text, strip.toNearestInt(), juce::Justification::centred, true);
}

juce::String MorphPad::hintText() const
{
    if (bank.numUsed() == 0)
        return "还没有快照 —— 随机到喜欢的状态就按上面的「存」"_u8;

    // processor 那边 numUsed() < 2 直接不 morph：只有一个 slot 时权重会全塌到它身上，
    // 等于每 33ms 强行召回一次，用户自己转旋钮都会被拽回去。
    return "只有 1 个快照，再存一个才能插值"_u8;
}

} // namespace pr
