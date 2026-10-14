#include "Widgets.h"

namespace pr
{

using theme::Weight;
using theme::Icon;

//==============================================================================
SegmentedControl::SegmentedControl()
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedControl::setItems (std::vector<Item> newItems)
{
    items = std::move (newItems);
    selected = juce::jlimit (0, juce::jmax (0, (int) items.size() - 1), selected);
    repaint();
}

void SegmentedControl::setSelected (int index, juce::NotificationType n)
{
    index = juce::jlimit (0, juce::jmax (0, (int) items.size() - 1), index);

    if (index == selected)
        return;

    selected = index;
    repaint();

    if (n != juce::dontSendNotification && onChange != nullptr)
        onChange (selected);
}

juce::Rectangle<float> SegmentedControl::segment (int i) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float w = r.getWidth() / (float) juce::jmax (1, (int) items.size());
    return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
}

int SegmentedControl::indexAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (segment (i).contains (p))
            return i;
    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    g.setColour (well.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (stroke.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto seg = segment (i);
        const bool sel = (i == selected);
        const bool hov = (i == hover) && isEnabled();

        if (sel)
        {
            // 凸起的那一段：浅一级的底 + 一点点投影
            g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
            g.fillRoundedRectangle (seg.translated (0.0f, 1.0f), seg.getHeight() * 0.5f);
            g.setColour (controlHi.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
        }
        else if (hov)
        {
            g.setColour (control.withAlpha (0.45f));
            g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
        }

        const auto c = (sel ? accent : (hov ? text : textDim)).withMultipliedAlpha (alpha);
        const auto& it = items[(size_t) i];

        if (it.glyph != nullptr)
        {
            const float s = juce::jmin (seg.getHeight() - 8.0f, seg.getWidth() - 10.0f);
            it.glyph (g, seg.withSizeKeepingCentre (s * 1.25f, s), c);
        }
        else
        {
            g.setColour (c);
            g.setFont (font (12.0f, sel ? Weight::bold : Weight::medium));
            g.drawFittedText (it.text, seg.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        }
    }
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int h = indexAt (e.position);
    if (h != hover) { hover = h; repaint(); }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (const int i = indexAt (e.position); i >= 0)
        setSelected (i, juce::sendNotificationSync);
}

juce::String SegmentedControl::getTooltip()
{
    if (juce::isPositiveAndBelow (hover, (int) items.size()))
        return items[(size_t) hover].tooltip;
    return {};
}

//==============================================================================
Stepper::Stepper()
{
    setRepaintsOnMouseActivity (true);
}

void Stepper::setValue (int v, juce::NotificationType n)
{
    v = juce::jlimit (minV, maxV, v);
    if (v == value)
        return;

    value = v;
    repaint();

    if (n != juce::dontSendNotification && onChange != nullptr)
        onChange (value);
}

juce::Rectangle<float> Stepper::minusArea() const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    return r.withWidth (r.getHeight());
}

juce::Rectangle<float> Stepper::plusArea() const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    return r.withLeft (r.getRight() - r.getHeight());
}

void Stepper::step (int delta)
{
    setValue (value + delta, juce::sendNotificationSync);
}

void Stepper::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    g.setColour (well.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (stroke.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    auto button = [&] (juce::Rectangle<float> a, bool plus, bool active, bool hov)
    {
        g.setColour ((hov && active ? controlHi : control).withMultipliedAlpha (alpha * (active ? 1.0f : 0.5f)));
        g.fillEllipse (a);

        juce::Path p;
        const auto c = a.getCentre();
        const float s = a.getWidth() * 0.22f;
        p.startNewSubPath (c.x - s, c.y); p.lineTo (c.x + s, c.y);
        if (plus) { p.startNewSubPath (c.x, c.y - s); p.lineTo (c.x, c.y + s); }

        g.setColour ((active ? text : textFaint).withMultipliedAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    button (minusArea(), false, value > minV, hover < 0);
    button (plusArea(),  true,  value < maxV, hover > 0);

    const auto txt = textFor != nullptr ? textFor (value) : juce::String (value);
    g.setFont (font (12.5f, Weight::medium));
    g.setColour ((value == minV ? textDim : accent.brighter (0.1f)).withMultipliedAlpha (alpha));
    g.drawText (txt, r.reduced (r.getHeight(), 0.0f), juce::Justification::centred, true);
}

void Stepper::mouseMove (const juce::MouseEvent& e)
{
    const int h = minusArea().contains (e.position) ? -1 : plusArea().contains (e.position) ? 1 : 0;
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Stepper::mouseExit (const juce::MouseEvent&)
{
    hover = 0;
    repaint();
}

void Stepper::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (minusArea().contains (e.position))     step (-1);
    else if (plusArea().contains (e.position)) step (+1);
}

void Stepper::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 双击中间的数字回到最小值（「只动 N 个」里就是"不限"）
    if (isEnabled() && ! minusArea().contains (e.position) && ! plusArea().contains (e.position))
        setValue (minV, juce::sendNotificationSync);
}

void Stepper::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! isEnabled())
        return;

    wheelAcc += w.deltaY;

    // 鼠标滚轮一格 deltaY 约 0.12；触控板会给一串很小的值，攒够了才走一步
    while (wheelAcc >= 0.1f)  { step (+1); wheelAcc -= 0.1f; }
    while (wheelAcc <= -0.1f) { step (-1); wheelAcc += 0.1f; }
}

//==============================================================================
SnapshotTiles::SnapshotTiles()
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SnapshotTiles::setTiles (const std::array<Tile, 4>& t, int w, bool store)
{
    bool same = (w == winner) && (store == canStore);
    for (size_t i = 0; i < 4 && same; ++i)
        same = t[i].used == tiles[i].used && t[i].name == tiles[i].name && t[i].numSlots == tiles[i].numSlots;

    if (same)
        return;

    tiles = t;
    winner = w;
    canStore = store;
    repaint();
}

juce::Rectangle<float> SnapshotTiles::tileRect (int i) const
{
    const auto r = getLocalBounds().toFloat();
    const float gapPx = 8.0f;
    const float w = (r.getWidth() - gapPx) * 0.5f;
    const float h = (r.getHeight() - gapPx) * 0.5f;
    return { r.getX() + (float) (i % 2) * (w + gapPx), r.getY() + (float) (i / 2) * (h + gapPx), w, h };
}

juce::Rectangle<float> SnapshotTiles::clearRect (int i) const
{
    const auto t = tileRect (i);
    return { t.getRight() - 25.0f, t.getCentreY() - 9.5f, 19.0f, 19.0f };
}

juce::Rectangle<float> SnapshotTiles::overwriteRect (int i) const
{
    return clearRect (i).translated (-22.0f, 0.0f);
}

std::pair<int, SnapshotTiles::Hit> SnapshotTiles::hitTest (juce::Point<float> p) const
{
    for (int i = 0; i < 4; ++i)
    {
        if (! tileRect (i).contains (p))
            continue;

        if (tiles[(size_t) i].used)
        {
            if (clearRect (i).contains (p))     return { i, Hit::clear };
            if (overwriteRect (i).contains (p)) return { i, Hit::overwrite };
        }

        return { i, Hit::tile };
    }

    return { -1, Hit::none };
}

void SnapshotTiles::paint (juce::Graphics& g)
{
    using namespace theme;

    for (int i = 0; i < 4; ++i)
    {
        const auto& t   = tiles[(size_t) i];
        const auto  r   = tileRect (i);
        const auto  c   = slotColour (i);
        const bool  hov = (i == hoverTile);

        if (t.used)
        {
            g.setColour (tint (c, hov ? 0.34f : 0.24f));
            g.fillRoundedRectangle (r, 10.0f);

            if (i == winner)
            {
                g.setColour (c);
                g.drawRoundedRectangle (r.reduced (0.75f), 10.0f, 1.6f);
            }
        }
        else
        {
            g.setColour (hov && canStore ? control.withAlpha (0.5f) : well);
            g.fillRoundedRectangle (r, 10.0f);

            // 虚线框 = "这里是空的，点我存"
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (0.75f), 10.0f);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.5f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (hov && canStore ? strokeHi.brighter (0.3f) : strokeHi);
            g.fillPath (dashed);
        }

        // 编号徽章：竖直居中，名字 / 状态写在它右边
        const auto badge = juce::Rectangle<float> (r.getX() + 10.0f, r.getCentreY() - 9.0f, 18.0f, 18.0f);
        g.setColour (t.used ? c : control);
        g.fillEllipse (badge);
        g.setColour (t.used ? ink : textDim);
        g.setFont (font (11.0f, Weight::bold));
        g.drawText (juce::String (i + 1), badge, juce::Justification::centred, false);

        auto body = r.withLeft (badge.getRight() + 8.0f).withTrimmedRight (8.0f);

        if (t.used)
        {
            if (hov)
                body.setRight (overwriteRect (i).getX() - 4.0f);   // 给浮出来的两个小按钮让位

            const bool twoLines = t.numSlots > 1;
            auto nameRow = twoLines ? body.withHeight (16.0f).withY (r.getCentreY() - 15.0f)
                                    : body.withSizeKeepingCentre (body.getWidth(), 16.0f);

            g.setColour (text);
            g.setFont (font (12.0f, Weight::medium));
            g.drawText (t.name, nameRow, juce::Justification::centredLeft, true);

            if (twoLines)
            {
                g.setColour (textDim);
                g.setFont (font (10.5f));
                g.drawText (juce::String (t.numSlots) + " 个链位"_u8, nameRow.translated (0.0f, 16.0f),
                            juce::Justification::centredLeft, true);
            }

            if (hov)
            {
                for (auto [rect, ic, hit] : { std::tuple { overwriteRect (i), Icon::save,  Hit::overwrite },
                                              std::tuple { clearRect (i),     Icon::close, Hit::clear } })
                {
                    const bool on = (hoverHit == hit);
                    g.setColour (on ? controlHi : control.withAlpha (0.9f));
                    g.fillEllipse (rect);
                    drawIcon (g, ic, rect.reduced (5.0f), on ? text : textDim, 1.5f);
                }
            }
        }
        else
        {
            // "+ 存入" 一行居中
            const auto col = canStore ? (hov ? text : textDim) : textFaint;
            g.setFont (font (11.5f, Weight::medium));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "存入"_u8);
            const float x0 = body.getCentreX() - (12.0f + 5.0f + tw) * 0.5f - 6.0f;
            drawIcon (g, Icon::plus, juce::Rectangle<float> (x0, r.getCentreY() - 6.0f, 12.0f, 12.0f), col, 1.6f);
            g.setColour (col);
            g.drawText ("存入"_u8, juce::Rectangle<float> (x0 + 17.0f, r.getY(), tw + 4.0f, r.getHeight()),
                        juce::Justification::centredLeft, false);
        }
    }
}

void SnapshotTiles::mouseMove (const juce::MouseEvent& e)
{
    const auto [i, hit] = hitTest (e.position);
    if (i != hoverTile || hit != hoverHit)
    {
        hoverTile = i;
        hoverHit = hit;
        repaint();
    }
}

void SnapshotTiles::mouseExit (const juce::MouseEvent&)
{
    hoverTile = -1;
    hoverHit = Hit::none;
    repaint();
}

void SnapshotTiles::mouseDown (const juce::MouseEvent& e)
{
    const auto [i, hit] = hitTest (e.position);
    if (i < 0 || ! isEnabled())
        return;

    if (e.mods.isPopupMenu())
    {
        showMenu (i);
        return;
    }

    const bool used = tiles[(size_t) i].used;

    switch (hit)
    {
        case Hit::clear:     if (onClear != nullptr) onClear (i); break;
        case Hit::overwrite: if (canStore && onStore != nullptr) onStore (i); break;
        case Hit::tile:
            if (used)                       { if (onRecall != nullptr) onRecall (i); }
            else if (canStore)              { if (onStore  != nullptr) onStore (i); }
            break;
        case Hit::none: break;
    }
}

void SnapshotTiles::showMenu (int i)
{
    const bool used = tiles[(size_t) i].used;

    juce::PopupMenu m;
    m.addSectionHeader ("快照 "_u8 + juce::String (i + 1));
    m.addItem (1, used ? "召回"_u8 : "（空）"_u8, used);
    m.addItem (2, used ? "用当前状态覆盖"_u8 : "存入当前状态"_u8, canStore);
    m.addSeparator();
    m.addItem (3, "清空"_u8, used);

    juce::Component::SafePointer<SnapshotTiles> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal (tileRect (i).toNearestInt())),
                     [safe, i] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr) return;

        if (r == 1 && self->onRecall != nullptr) self->onRecall (i);
        if (r == 2 && self->onStore  != nullptr) self->onStore (i);
        if (r == 3 && self->onClear  != nullptr) self->onClear (i);
    });
}

juce::String SnapshotTiles::getTooltip()
{
    if (hoverTile < 0)
        return {};

    const int n = hoverTile + 1;

    switch (hoverHit)
    {
        case Hit::overwrite: return "用当前状态覆盖快照 "_u8 + juce::String (n) + "（Shift+"_u8 + juce::String (n) + "）"_u8;
        case Hit::clear:     return "清空快照 "_u8 + juce::String (n);
        case Hit::tile:
            return tiles[(size_t) hoverTile].used
                     ? "点一下召回快照 "_u8 + juce::String (n) + "（键盘 "_u8 + juce::String (n) + "）。右键有更多操作。"_u8
                     : "点一下把当前整条链存成快照 "_u8 + juce::String (n) + "（Shift+"_u8 + juce::String (n) + "）"_u8;
        case Hit::none: break;
    }

    return {};
}

} // namespace pr
