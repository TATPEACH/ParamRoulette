#include "MorphPad.h"

namespace pr
{

using theme::Weight;
using theme::slotColour;

namespace
{
    juce::String slotLabel (const SnapshotBank& b, int slot)
    {
        if (! juce::isPositiveAndBelow (slot, kNumSlots))
            return {};

        const auto n = b[slot].name;
        return n.isNotEmpty() ? n : ("快照 "_u8 + juce::String (slot + 1));
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
    return r.reduced (kHandleR + 4.0f, 0.0f);
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
    using namespace theme;

    auto full = getLocalBounds();
    auto readout = full.removeFromBottom (kReadoutH + kMargin / 2).reduced (kMargin, 0);

    const auto r = padArea();

    if (r.getWidth() < 8.0f || r.getHeight() < 8.0f)
        return;

    if (mode == MorphMode::TwoD)
        paintTwoD (g, r);
    else
        paintOneD (g, r);

    if (bank.numUsed() < 2)
    {
        // 没法插值的时候：整块 pad 压暗，提示写在正中——而不是挤在一根细线旁边
        const auto area = (mode == MorphMode::TwoD ? r : getLocalBounds().toFloat().reduced ((float) kMargin))
                              .withTrimmedBottom (mode == MorphMode::TwoD ? 0.0f : (float) kReadoutH);
        g.setColour (module.withAlpha (0.78f));
        g.fillRoundedRectangle (area, rWell);

        g.setColour (textDim);
        g.setFont (font (12.5f, Weight::medium));
        g.drawFittedText (hintText(), area.toNearestInt().reduced (16, 0), juce::Justification::centred, 3, 0.9f);
        return;
    }

    paintReadout (g, readout.toFloat());
}

void MorphPad::paintOneD (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace theme;

    // 布局：上面一块是每个快照的**权重曲线**（x 拖到哪、谁占多少一目了然），
    // 下面是真正拖的那根轨道。轨道和曲线共用同一个 x 坐标。
    const float trackY = r.getBottom() - 22.0f;
    const auto  plot   = juce::Rectangle<float> (r.getX() - kHandleR, r.getY(),
                                                 r.getWidth() + 2.0f * kHandleR, juce::jmax (0.0f, trackY - 18.0f - r.getY()));

    int used[kNumSlots];
    int nUsed = 0;
    for (int i = 0; i < kNumSlots; ++i)
        if (bank.isUsed (i))
            used[nUsed++] = i;

    if (plot.getHeight() > 24.0f)
    {
        g.setColour (well);
        g.fillRoundedRectangle (plot, rWell);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.reduced (2.0f).toNearestInt());
            fillDotGrid (g, plot.toNearestInt(), 16);
        }

        g.setColour (stroke);
        g.drawRoundedRectangle (plot.reduced (0.5f), rWell, 1.0f);

        if (nUsed >= 2)
        {
            // 每个快照一条帐篷形的权重曲线——和 computeWeights 里 1D 的分段线性完全一致，
            // 所以直接拿它采样，不另写一份公式（两份公式迟早会对不上）。
            const auto inner = plot.reduced (0.0f, 10.0f);
            constexpr int kSteps = 96;

            for (int k = 0; k < nUsed; ++k)
            {
                const int s = used[k];
                juce::Path curve;

                for (int i = 0; i <= kSteps; ++i)
                {
                    const float x = (float) i / (float) kSteps;
                    const float w = bank.computeWeights (MorphMode::OneD, x, 0.0f).weightOf (s);
                    const juce::Point<float> p { r.getX() + x * r.getWidth(), inner.getBottom() - w * inner.getHeight() };

                    if (i == 0) curve.startNewSubPath (p);
                    else        curve.lineTo (p);
                }

                juce::Path fillPath (curve);
                fillPath.lineTo (r.getRight(), inner.getBottom());
                fillPath.lineTo (r.getX(), inner.getBottom());
                fillPath.closeSubPath();

                g.setColour (slotColour (s).withAlpha (0.14f));
                g.fillPath (fillPath);
                g.setColour (slotColour (s).withAlpha (0.85f));
                g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }

            // 当前位置的竖线
            const float hx = r.getX() + posX * r.getWidth();
            g.setColour (morph.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (hx - 0.75f, plot.getY() + 6.0f, 1.5f, plot.getHeight() - 12.0f));
        }
    }

    // —— 轨道：一条凹进去的胶囊槽
    const auto track = juce::Rectangle<float> (r.getX() - kHandleR, trackY - 5.0f, r.getWidth() + 2.0f * kHandleR, 10.0f);
    g.setColour (well);
    g.fillRoundedRectangle (track, 5.0f);
    g.setColour (stroke);
    g.drawRoundedRectangle (track.reduced (0.5f), 5.0f, 1.0f);

    if (nUsed >= 2)
    {
        const float hx = r.getX() + posX * r.getWidth();
        g.setColour (morph.withAlpha (0.55f));
        g.fillRoundedRectangle (track.withRight (hx).reduced (2.0f), 3.0f);
    }

    // 刻度：已用快照按**序号顺序**压实。只存了 1 和 3 的话，线上就只有两点，
    // 左端是 1、右端是 3——和 SnapshotBank::computeWeights 里 `x * (N-1)` 完全一致。
    for (int i = 0; i < nUsed; ++i)
    {
        const float t = (nUsed == 1) ? 0.5f : (float) i / (float) (nUsed - 1);
        const juce::Point<float> c { r.getX() + t * r.getWidth(), trackY };

        paintSlotDot (g, c, used[i], kSlotR);

        auto label = clampLabel ({ (int) c.x - 50, (int) (trackY + kSlotR + 3.0f), 100, 14 }, getLocalBounds());
        g.setColour (textDim);
        g.setFont (font (11.0f));
        g.drawText (slotLabel (bank, used[i]), label, juce::Justification::centred, true);
    }

    paintHandle (g, { r.getX() + posX * r.getWidth(), trackY });
}

void MorphPad::paintTwoD (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace theme;

    g.setColour (well);
    g.fillRoundedRectangle (r, rWell);

    // pad 里也铺一层点阵，和画布呼应
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (r.reduced (2.0f).toNearestInt());
        fillDotGrid (g, r.toNearestInt(), 16);
    }

    g.setColour (stroke);
    g.drawRoundedRectangle (r.reduced (0.5f), rWell, 1.0f);

    // 十字辅助线
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (r.getX() + 8.0f, r.getCentreY(), r.getRight() - 8.0f, r.getCentreY(), 1.0f);
    g.drawLine (r.getCentreX(), r.getY() + 8.0f, r.getCentreX(), r.getBottom() - 8.0f, 1.0f);

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

        juce::Path line;
        line.startNewSubPath (handle);
        line.lineTo (corner[s]);
        g.setColour (slotColour (s).withAlpha (0.18f + 0.55f * weight));
        g.strokePath (line, juce::PathStrokeType (1.0f + 2.6f * weight, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    for (int s = 0; s < kNumSlots; ++s)
    {
        paintSlotDot (g, corner[s], s, kSlotR);

        if (! bank.isUsed (s))
            continue;

        const bool left = (s == 0 || s == 2);
        const bool top  = (s == 0 || s == 1);

        const int lw = 96;
        auto label = clampLabel ({ left ? (int) r.getX() + 14 : (int) r.getRight() - 14 - lw,
                                   top  ? (int) r.getY() + 10 : (int) r.getBottom() - 10 - 14,
                                   lw, 14 },
                                 getLocalBounds());

        g.setColour (slotColour (s).withAlpha (0.85f));
        g.setFont (font (11.0f, Weight::medium));
        g.drawText (slotLabel (bank, s), label,
                    left ? juce::Justification::centredLeft : juce::Justification::centredRight, true);
    }

    paintHandle (g, handle);
}

void MorphPad::paintSlotDot (juce::Graphics& g, juce::Point<float> centre, int slot, float radius)
{
    using namespace theme;

    const auto box = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    const bool used = bank.isUsed (slot);

    // winner 每帧现读：capture() 会把它重置成 -1，缓存下来就会指向已经不存在的 slot。
    const bool winner = used && (bank.getWinner() == slot);

    // 一圈画布色的描边：角标压在 pad 边缘上，没有它会和边框粘在一起
    g.setColour (module);
    g.fillEllipse (box.expanded (2.5f));

    if (used)
    {
        g.setColour (slotColour (slot));
        g.fillEllipse (box);

        if (winner)
        {
            g.setColour (text);
            g.drawEllipse (box.expanded (3.0f), 1.5f);
        }
    }
    else
    {
        g.setColour (well);
        g.fillEllipse (box);
        g.setColour (strokeHi);
        g.drawEllipse (box.reduced (0.5f), 1.0f);
    }

    g.setColour (used ? ink : textFaint);
    g.setFont (font (10.0f, Weight::bold));
    g.drawText (juce::String (slot + 1), box, juce::Justification::centred, false);
}

void MorphPad::paintHandle (juce::Graphics& g, juce::Point<float> centre)
{
    using namespace theme;

    const bool live = bank.numUsed() >= 2;
    const auto box  = juce::Rectangle<float> (kHandleR * 2.0f, kHandleR * 2.0f).withCentre (centre);

    if (live)
    {
        // 柔光：pad 上唯一在动的东西，值得被看见
        g.setColour (morph.withAlpha (dragging ? 0.30f : 0.18f));
        g.fillEllipse (box.expanded (dragging ? 9.0f : 6.0f));
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (box.translated (0.0f, 1.5f));

    g.setColour (live ? knobFace : juce::Colour (0xff6a6a6a));
    g.fillEllipse (box);

    g.setColour (live ? morph : textFaint);
    g.drawEllipse (box.reduced (2.5f), 2.0f);
}

void MorphPad::paintReadout (juce::Graphics& g, juce::Rectangle<float> strip)
{
    using namespace theme;

    // 每个有权重的快照一枚小胶囊：色点 + 编号 + 百分比
    const auto w = bank.computeWeights (mode, posX, posY);

    juce::Array<std::pair<int, int>> chips;
    for (int s = 0; s < kNumSlots; ++s)
        if (const float weight = w.weightOf (s); weight > 0.005f)
            chips.add ({ s, juce::roundToInt (weight * 100.0f) });

    g.setFont (font (11.0f, Weight::medium));

    const float chipW = 58.0f, gapPx = 6.0f;
    const float total = (float) chips.size() * chipW + (float) juce::jmax (0, chips.size() - 1) * gapPx;
    float x = strip.getCentreX() - total * 0.5f;

    for (const auto& [s, pct] : chips)
    {
        const auto chip = juce::Rectangle<float> (x, strip.getCentreY() - 9.0f, chipW, 18.0f);
        g.setColour (tint (slotColour (s), 0.22f));
        g.fillRoundedRectangle (chip, 9.0f);

        g.setColour (slotColour (s));
        g.fillEllipse (chip.withWidth (18.0f).reduced (5.0f));

        g.setColour (text);
        g.drawText (juce::String (s + 1) + "  " + juce::String (pct) + "%", chip.withTrimmedLeft (16.0f),
                    juce::Justification::centred, false);
        x += chipW + gapPx;
    }
}

juce::String MorphPad::hintText() const
{
    if (bank.numUsed() == 0)
        return "还没有快照\n随机到喜欢的声音，点上面的方块存起来"_u8;

    // processor 那边 numUsed() < 2 直接不 morph：只有一个 slot 时权重会全塌到它身上，
    // 等于每 33ms 强行召回一次，用户自己转旋钮都会被拽回去。
    return "只有 1 个快照\n再存一个才能在它们之间插值"_u8;
}

} // namespace pr
