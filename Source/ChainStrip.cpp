#include "ChainStrip.h"

namespace pr
{

using theme::Weight;
using theme::Icon;

namespace
{
    constexpr float kNodeW  = 18.0f;
    constexpr float kGap    = 20.0f;   ///< 模块之间留给连线的距离
    constexpr float kGuardW = 160.0f;
    constexpr float kSag    = 8.0f;

    juce::Point<float> portIn  (juce::Rectangle<float> r) { return { r.getX(),     r.getCentreY() }; }
    juce::Point<float> portOut (juce::Rectangle<float> r) { return { r.getRight(), r.getCentreY() }; }

    void dashedRoundedRect (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c)
    {
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, radius);
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.1f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (c);
        g.fillPath (dashed);
    }
}

//==============================================================================
ChainStrip::ChainStrip()
{
    setRepaintsOnMouseActivity (false);     // 自己按区域重画，不要整条刷
}

void ChainStrip::setState (const std::array<Slot, kMaxChain>& s, int f, const Guard& gd)
{
    const bool slotsChanged = (s != slots) || (f != focused);
    const bool guardChanged = ! (gd == guard);

    slots = s;
    focused = f;
    guard = gd;

    if (slotsChanged)
        repaint();
    else if (guardChanged)
        repaint (guardRect.expanded (6.0f).toNearestInt());   // GR 表 20Hz 在跳，只重画这一块
}

void ChainStrip::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float h = juce::jmin (b.getHeight() - 10.0f, 76.0f);
    const float y = b.getCentreY() - h * 0.5f - 2.0f;

    inNode  = { b.getX() + kNodeW * 0.5f + 2.0f, y + h * 0.5f };
    outNode = { b.getRight() - kNodeW * 0.5f - 2.0f, y + h * 0.5f };

    const float avail = b.getWidth() - 2.0f * kNodeW - 8.0f * kGap - kGuardW;
    const float w     = juce::jmax (64.0f, avail / (float) kMaxChain);

    float x = b.getX() + kNodeW + kGap;
    for (auto& r : rects)
    {
        r = { x, y, w, h };
        x += w + kGap;
    }

    guardRect = { x, y, kGuardW, h };
}

juce::Rectangle<float> ChainStrip::powerRect (juce::Rectangle<float> m) const
{
    return { m.getRight() - 26.0f, m.getY() + 7.0f, 18.0f, 18.0f };
}

std::pair<int, ChainStrip::Part> ChainStrip::hitTest (juce::Point<float> p) const
{
    for (int i = 0; i < kMaxChain; ++i)
    {
        if (! rects[(size_t) i].contains (p))
            continue;

        if (slots[(size_t) i].loaded && powerRect (rects[(size_t) i]).expanded (3.0f).contains (p))
            return { i, Part::power };

        return { i, Part::body };
    }

    if (guardRect.contains (p))
        return { -1, powerRect (guardRect).expanded (3.0f).contains (p) ? Part::guardPower : Part::guardBody };

    return { -1, Part::none };
}

//==============================================================================
void ChainStrip::paint (juce::Graphics& g)
{
    using namespace theme;

    // —— 连线先画（在模块底下），顺便算出信号走到每个模块时是什么颜色
    juce::Colour sig = audio;
    auto from = inNode;

    for (int i = 0; i < kMaxChain; ++i)
    {
        const auto& s = slots[(size_t) i];
        inColour[(size_t) i] = sig;

        drawCable (g, from, portIn (rects[(size_t) i]), sig, kSag);

        if (s.loaded && ! s.bypassed)
            sig = s.colour;

        from = portOut (rects[(size_t) i]);
    }

    drawCable (g, from, portIn (guardRect), sig, kSag);
    drawCable (g, portOut (guardRect), outNode, sig, kSag);

    // —— IN / OUT 节点
    for (auto [node, label, c] : { std::tuple { inNode, "IN", audio }, std::tuple { outNode, "OUT", sig } })
    {
        drawPort (g, node, c, 5.5f);
        g.setFont (font (10.0f, Weight::bold));
        g.setColour (textFaint);
        g.drawText (label, juce::Rectangle<float> (40.0f, 14.0f).withCentre (node.translated (0.0f, 17.0f)),
                    juce::Justification::centred, false);
    }

    for (int i = 0; i < kMaxChain; ++i)
        paintModule (g, i);

    paintGuard (g);
}

void ChainStrip::paintModule (juce::Graphics& g, int i)
{
    using namespace theme;

    const auto& s   = slots[(size_t) i];
    const auto  r   = rects[(size_t) i];
    const bool  foc = (i == focused);
    const bool  hov = (i == hoverIndex);
    const auto  in  = inColour[(size_t) i];

    if (! s.loaded)
    {
        // 空位：虚线框 + 一道淡淡的直通线 + 加号
        if (hov)
        {
            g.setColour (control.withAlpha (0.35f));
            g.fillRoundedRectangle (r, rModule);
        }

        g.setColour (in.withAlpha (0.22f));
        g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.5f);

        dashedRoundedRect (g, r.reduced (0.6f), rModule,
                           foc ? accent.withAlpha (0.85f) : (hov ? strokeHi.brighter (0.35f) : strokeHi));

        g.setFont (font (10.5f, Weight::bold));
        g.setColour (foc ? accent : textFaint);
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (r.getX() + 9.0f, r.getY() + 6.0f, 20.0f, 14.0f),
                    juce::Justification::centredLeft, false);

        const auto plus = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ r.getCentreX(), r.getCentreY() - 7.0f });
        g.setColour (well);
        g.fillEllipse (plus.expanded (5.0f));
        drawIcon (g, Icon::plus, plus, hov ? text : textDim, 1.7f);

        g.setFont (font (11.0f));
        g.setColour (hov ? textDim : textFaint);
        g.drawText ("添加插件"_u8, juce::Rectangle<float> (r.getX(), r.getCentreY() + 8.0f, r.getWidth(), 16.0f),
                    juce::Justification::centred, false);

        drawPort (g, portIn (r),  in.withAlpha (0.5f), 4.0f);
        drawPort (g, portOut (r), in.withAlpha (0.5f), 4.0f);
        return;
    }

    const bool active = ! s.bypassed;
    const auto out    = active ? s.colour : in;

    // —— 本体
    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), rModule);

    g.setColour (foc ? moduleHi : (hov ? juce::Colour (0xff2f2f2f) : module));
    g.fillRoundedRectangle (r, rModule);

    // 顶部一条分类色的细边：Grid 模块靠标题颜色分类，模块小的时候一条色带更显眼
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, rModule);
        g.reduceClipRegion (clip);
        g.setColour ((active ? s.colour : textFaint).withAlpha (active ? 0.9f : 0.5f));
        g.fillRect (r.withHeight (3.0f));
    }

    if (foc)
    {
        g.setColour (accent);
        g.drawRoundedRectangle (r.reduced (0.8f), rModule, 1.6f);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.reduced (0.5f), rModule, 1.0f);
    }

    // —— 内容：编号 + 分类 / 电源键，名字，格式 · 参数数
    auto c = r.reduced (10.0f, 8.0f).withTrimmedTop (2.0f);

    auto row1 = c.removeFromTop (16.0f);
    const auto badge = row1.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f);
    g.setColour (active ? s.colour : control);
    g.fillEllipse (badge);
    g.setColour (active ? ink : textDim);
    g.setFont (font (10.0f, Weight::bold));
    g.drawText (juce::String (i + 1), badge, juce::Justification::centred, false);

    row1.removeFromLeft (6.0f);
    row1.removeFromRight (22.0f);   // 电源键的位置

    g.setFont (font (10.5f, Weight::medium));
    g.setColour (active ? s.colour : warn);
    g.drawText (active ? s.tag : "已旁通"_u8, row1, juce::Justification::centredLeft, true);

    const auto pw   = powerRect (r);
    const bool pHov = hov && hoverPart == Part::power;
    if (pHov)
    {
        g.setColour (control);
        g.fillEllipse (pw.expanded (2.0f));
    }
    drawIcon (g, Icon::power, pw.reduced (2.0f), active ? accent : (pHov ? textDim : textFaint), 1.7f);

    c.removeFromTop (5.0f);
    g.setFont (font (13.0f, Weight::bold));
    g.setColour (active ? text : textDim);
    g.drawText (s.name, c.removeFromTop (18.0f), juce::Justification::centredLeft, true);

    g.setFont (font (10.5f));
    g.setColour (textFaint.brighter (0.25f));
    g.drawText (s.detail, c.removeFromTop (15.0f), juce::Justification::centredLeft, true);

    if (! active)
    {
        // 旁通：盖一层暗色，再画一道直通线，表示"信号从这里直接穿过去了"
        g.setColour (canvas.withAlpha (0.35f));
        g.fillRoundedRectangle (r, rModule);
    }

    drawPort (g, portIn (r),  in);
    drawPort (g, portOut (r), out);
}

void ChainStrip::paintGuard (juce::Graphics& g)
{
    using namespace theme;

    const auto r   = guardRect;
    const bool hov = (hoverPart == Part::guardBody || hoverPart == Part::guardPower);
    const auto col = guard.nanWarn ? danger : juce::Colour (0xffd8d8d8);

    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), rModule);
    g.setColour (hov ? juce::Colour (0xff2f2f2f) : module);
    g.fillRoundedRectangle (r, rModule);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, rModule);
        g.reduceClipRegion (clip);
        g.setColour (col.withAlpha (guard.on ? 0.85f : 0.35f));
        g.fillRect (r.withHeight (3.0f));
    }

    g.setColour (guard.nanWarn ? danger : juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.reduced (0.6f), rModule, guard.nanWarn ? 1.6f : 1.0f);

    auto c = r.reduced (10.0f, 8.0f).withTrimmedTop (2.0f);
    auto row1 = c.removeFromTop (16.0f);
    row1.removeFromRight (22.0f);

    g.setFont (font (10.5f, Weight::medium));
    g.setColour (guard.nanWarn ? danger : textDim);
    g.drawText (guard.nanWarn ? "NaN 已清零"_u8 : "链尾安全网"_u8, row1, juce::Justification::centredLeft, true);

    const auto pw = powerRect (r);
    if (hoverPart == Part::guardPower)
    {
        g.setColour (control);
        g.fillEllipse (pw.expanded (2.0f));
    }
    drawIcon (g, Icon::power, pw.reduced (2.0f), guard.on ? accent : textFaint, 1.7f);

    c.removeFromTop (5.0f);
    g.setFont (font (13.0f, Weight::bold));
    g.setColour (guard.on ? text : textDim);
    g.drawText ("输出保护"_u8, c.removeFromTop (18.0f), juce::Justification::centredLeft, true);

    // 增益衰减表：0~12 dB 铺满。超过 6 dB 变红——那基本是某个插件炸了。
    auto meterRow = c.removeFromTop (15.0f);
    const auto track = meterRow.removeFromLeft (meterRow.getWidth() * 0.46f).withSizeKeepingCentre (meterRow.getWidth() * 0.46f, 5.0f);
    g.setColour (well);
    g.fillRoundedRectangle (track, 2.5f);

    const float frac = juce::jlimit (0.0f, 1.0f, guard.grDb / 12.0f);
    if (guard.on && frac > 0.0f)
    {
        g.setColour (guard.grDb > 6.0f ? danger : accent);
        g.fillRoundedRectangle (track.withWidth (juce::jmax (5.0f, track.getWidth() * frac)), 2.5f);
    }

    meterRow.removeFromLeft (6.0f);
    g.setFont (font (10.5f));
    g.setColour (textFaint.brighter (0.25f));
    const juce::String readout = ! guard.on          ? "仅清 NaN"_u8
                               : guard.grDb > 0.1f   ? juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (guard.grDb, 1) + " dB"
                                                     : juce::String (guard.ceilingDb, 1) + " dB";
    g.drawText (readout, meterRow, juce::Justification::centredLeft, true);

    drawPort (g, portIn (r),  juce::Colour (0xffb4b4b4));
    drawPort (g, portOut (r), juce::Colour (0xffb4b4b4));
}

//==============================================================================
void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    const auto [i, part] = hitTest (e.position);

    if (i != hoverIndex || part != hoverPart)
    {
        hoverIndex = i;
        hoverPart  = part;
        repaint();
    }

    setMouseCursor (part == Part::none || part == Part::guardBody ? juce::MouseCursor::NormalCursor
                                                                  : juce::MouseCursor::PointingHandCursor);
}

void ChainStrip::mouseExit (const juce::MouseEvent&)
{
    hoverIndex = -1;
    hoverPart  = Part::none;
    repaint();
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto [i, part] = hitTest (e.position);

    if (part == Part::guardPower)
    {
        if (onGuardToggle != nullptr)
            onGuardToggle (! guard.on);
        return;
    }

    if (i < 0)
        return;

    const auto& s = slots[(size_t) i];

    if (e.mods.isPopupMenu())
    {
        if (onFocus != nullptr) onFocus (i);
        if (onContextMenu != nullptr) onContextMenu (i);
        return;
    }

    if (part == Part::power)
    {
        if (onBypass != nullptr)
            onBypass (i, ! s.bypassed);
        return;
    }

    if (onFocus != nullptr)
        onFocus (i);

    // 空位：聚焦之后直接弹插件菜单，省掉"先点格子再去找『选择插件』"那一步
    if (! s.loaded && onAdd != nullptr)
        onAdd (i);
}

void ChainStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto [i, part] = hitTest (e.position);

    if (i >= 0 && part == Part::body && slots[(size_t) i].loaded && onOpenEditor != nullptr)
        onOpenEditor (i);
}

juce::String ChainStrip::getTooltip()
{
    if (hoverPart == Part::guardPower)
        return guard.on ? "关掉输出保护（关掉后仍会把 NaN 清零）"_u8 : "打开输出保护"_u8;

    if (hoverPart == Part::guardBody)
        return "链尾安全网：把非法样本（NaN / Inf）清零，并把峰值压到天花板以下（默认 −1 dBFS）。"_u8
               "随机化很容易把反馈类参数推到自激，这一级专门兜底。"_u8;

    if (! juce::isPositiveAndBelow (hoverIndex, kMaxChain))
        return {};

    const auto& s = slots[(size_t) hoverIndex];
    const auto  n = juce::String (hoverIndex + 1);

    if (! s.loaded)
        return "链位 "_u8 + n + " 是空的，点一下装一个插件进来。信号会直接穿过空位。"_u8;

    if (hoverPart == Part::power)
        return s.bypassed ? "取消旁通"_u8 : "旁通链位 "_u8 + n + "（信号直接穿过它）。旁通跟着位置走，换插件不会重置。"_u8;

    return s.name + "\n"_u8 + (s.tag.isNotEmpty() ? s.tag + " · "_u8 : juce::String()) + s.detail
           + "\n\n单击：聚焦这一位（随机化、列表、快照都作用在它上面）\n双击：打开插件界面\n右键：更多操作"_u8;
}

} // namespace pr
