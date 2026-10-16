#include "RackPanels.h"

namespace pr
{

using theme::Weight;

namespace
{
    juce::Rectangle<float> circle (juce::Point<float> c, float r) { return { c.x - r, c.y - r, r * 2.0f, r * 2.0f }; }
}

//==============================================================================
void paintHostModule (juce::Graphics& g, juce::Rectangle<float> r, const HostLook& L, float z)
{
    const auto& p = rk::panel();
    const float W = r.getWidth() / z;      // 100% 缩放下的宽度（px）

    auto X = [&r, z] (float px) { return r.getX() + px * z; };
    auto Y = [&r, z] (float px) { return r.getY() + px * z; };

    rk::paintPanel (g, r, z);
    rk::paintScrews (g, r, z);

    // —— 编号徽章 + 插件名（最多两行）
    float nameX = 10.0f;
    if (L.number > 0)
    {
        const juce::Point<float> c { X (17.0f), Y (28.0f) };
        g.setColour (rk::shade (p.title));
        g.fillEllipse (circle (c, 7.5f * z));
        g.setColour (rk::shade (p.bg));
        g.setFont (rk::titleFont (10.0f * z));
        g.drawText (juce::String (L.number), circle (c, 7.5f * z), juce::Justification::centred, false);
        nameX = 29.0f;
    }

    g.setColour (rk::shade (p.title));
    g.setFont (rk::isAscii (L.name) ? rk::titleFont (12.5f * z) : theme::font (12.0f * z, Weight::bold));
    g.drawFittedText (L.name, juce::Rectangle<float> (X (nameX), Y (14.0f), (W - nameX - 8.0f) * z, 28.0f * z).toNearestInt(),
                      juce::Justification::centredLeft, 2, 0.8f);

    // —— 被托管插件的 program：一块 LED 屏（Rack 的 MIDI 模块选设备也是这种屏），点一下弹菜单
    const auto prog = juce::Rectangle<float> (X (8.0f), Y (46.0f), (W - 16.0f) * z, 19.0f * z);
    rk::paintDisplay (g, prog, z);
    {
        const auto t = L.hasPrograms ? L.program : juce::String::fromUTF8 ("\xe2\x80\x94");
        g.setColour (L.hasPrograms ? rk::yellow : rk::yellow.withAlpha (0.35f));
        g.setFont (rk::displayFont (t, 10.5f * z));
        g.drawText (t, prog.reduced (6.0f * z, 0.0f).withTrimmedRight (L.hasPrograms ? 10.0f * z : 0.0f),
                    juce::Justification::centredLeft, true);

        if (L.hasPrograms)
            theme::drawIcon (g, theme::Icon::chevronDown,
                             juce::Rectangle<float> (prog.getRight() - 13.0f * z, prog.getCentreY() - 4.0f * z, 8.0f * z, 8.0f * z),
                             rk::yellow.withAlpha (0.7f), 1.4f * z);
    }

    // —— 参数图：每根柱子是一个（或一桶）参数的当前值。黄的 = 这次改动过，暗的 = 没勾选
    const auto map = juce::Rectangle<float> (X (8.0f), Y (71.0f), (W - 16.0f) * z, 98.0f * z);
    rk::paintDisplay (g, map, z);
    {
        g.setColour (rk::yellow.withAlpha (0.6f));
        g.setFont (rk::displayFont (L.info, 9.0f * z));
        g.drawText (L.info, map.reduced (5.0f * z, 2.0f * z).withHeight (13.0f * z), juce::Justification::centredLeft, true);

        const auto area = map.reduced (5.0f * z, 4.0f * z).withTrimmedTop (13.0f * z);
        const int n = (int) L.bars.size();

        if (n > 0)
        {
            const float bw  = area.getWidth() / (float) n;
            const float gap = bw > 2.5f * z ? 0.5f * z : 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float v  = juce::jlimit (0.0f, 1.0f, L.bars[(size_t) i]);
                const auto  fl = i < (int) L.flags.size() ? L.flags[(size_t) i] : (juce::uint8) 0;
                const float h  = juce::jmax (1.0f * z, v * area.getHeight());

                g.setColour ((fl & 1) != 0 ? rk::yellow
                                           : L.tagColour.withAlpha ((fl & 2) != 0 ? 0.28f : 0.85f));
                g.fillRect (juce::Rectangle<float> (area.getX() + bw * (float) i + gap, area.getBottom() - h,
                                                    juce::jmax (0.8f, bw - gap * 2.0f), h));
            }
        }
        else if (L.number > 0)      // 浏览器里的预览还没装上，没有参数可画，留空
        {
            g.setColour (rk::yellow.withAlpha (0.3f));
            g.setFont (rk::displayFont ("NO PARAMS", 10.0f * z));
            g.drawText ("NO PARAMS", area, juce::Justification::centred, false);
        }
    }

    // —— 功能标签（色块的颜色就是 Grid 版里模块的分类色）
    {
        const auto row = juce::Rectangle<float> (X (10.0f), Y (175.0f), (W - 20.0f) * z, 13.0f * z);
        g.setColour (rk::shade (L.tagColour));
        g.fillRoundedRectangle (juce::Rectangle<float> (8.0f * z, 8.0f * z).withCentre ({ row.getX() + 4.0f * z, row.getCentreY() }), 2.0f * z);
        rk::paintLabel (g, L.tag, row.withTrimmedLeft (13.0f * z), z, 9.5f, juce::Justification::centredLeft);
    }

    if (! L.active)
    {
        // 旁通：上半块压暗，参数图上打一行 BYPASS。下面的键不压——要能一眼找到 ON 键点回来
        g.setColour (rk::shade (p.bg).withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 1.0f, Y (12.0f), r.getWidth() - 2.0f, 180.0f * z));
        g.setColour (rk::red);
        g.setFont (rk::titleFont (14.0f * z));
        g.drawText ("BYPASS", map, juce::Justification::centred, false);
    }

    // —— 「界面」键 + 窗口开着的灯，「ON」灯键（亮 = 在工作）
    const juce::Point<float> editC { X (40.0f), Y (222.0f) }, onC { X (W - 40.0f), Y (222.0f) };
    rk::paintLight (g, { X (40.0f), Y (201.0f) }, 3.0f * z, rk::green, L.editorOpen ? 1.0f : 0.0f);
    rk::paintTL1105 (g, editC, 16.0f * z, false, false, true);
    rk::paintLabel (g, "界面"_u8, juce::Rectangle<float> (editC.x - 25.0f * z, Y (233.0f), 50.0f * z, 13.0f * z), z, 10.0f);
    rk::paintBezel (g, onC, 22.0f * z, rk::green, L.active ? 1.0f : 0.0f, false, false);
    rk::paintLabel (g, "ON", juce::Rectangle<float> (onC.x - 25.0f * z, Y (236.0f), 50.0f * z, 12.0f * z), z, 9.5f);

    // —— 信号灯：这一位处理完之后的电平
    rk::paintLabel (g, "SIG", juce::Rectangle<float> (X (8.0f), Y (262.0f), 24.0f * z, 12.0f * z), z, 8.5f,
                    juce::Justification::centredLeft, false, true);
    rk::paintLadder (g, juce::Rectangle<float> (X (34.0f), Y (262.0f), (W - 44.0f) * z, 12.0f * z), 8, L.level, z, true);

    // —— 插孔：输入在面板上，输出在反色的框里（VCV 的惯例）
    rk::paintLabel (g, "IN", juce::Rectangle<float> (X (12.0f), Y (305.0f), 40.0f * z, 11.0f * z), z, 9.0f);
    rk::paintJack (g, { X (32.0f), Y (334.0f) }, z);
    const auto box = juce::Rectangle<float> (r.getRight() - 60.0f * z, Y (300.0f), 52.0f * z, 51.0f * z);
    rk::paintOutputBox (g, box, z);
    rk::paintLabel (g, "OUT", box.withHeight (16.0f * z).translated (0.0f, 2.0f * z), z, 9.0f, juce::Justification::centred, true);
    rk::paintJack (g, { r.getRight() - 34.0f * z, Y (334.0f) }, z);

    rk::paintBrand (g, r, L.brand.toUpperCase(), z);
}

//==============================================================================
RackModule::RackModule (juce::String t, juce::String b)
    : title (std::move (t)), brand (std::move (b))
{
}

void RackModule::clearDecor()
{
    labels.clear();
    leds.clear();
    boxes.clear();
    jacks.clear();
    rules.clear();
}

int RackModule::addLabel (juce::Rectangle<float> r, const juce::String& t, float px, juce::Justification j, bool onBox, bool dim)
{
    labels.push_back ({ r, t, px, j, onBox, dim });
    return (int) labels.size() - 1;
}

void RackModule::setLabel (int index, const juce::String& t, bool dim)
{
    if (! juce::isPositiveAndBelow (index, (int) labels.size()))
        return;

    auto& l = labels[(size_t) index];
    if (l.text == t && l.dim == dim)
        return;

    l.text = t;
    l.dim  = dim;
    repaint (l.r.expanded (2.0f).getSmallestIntegerContainer());
}

int RackModule::addLed (juce::Point<float> p, float radius, juce::Colour c)
{
    leds.push_back ({ p, radius, c, 0.0f });
    return (int) leds.size() - 1;
}

void RackModule::setLed (int index, float b)
{
    if (! juce::isPositiveAndBelow (index, (int) leds.size()))
        return;

    auto& d = leds[(size_t) index];
    if (std::abs (d.b - b) < 0.01f)
        return;

    d.b = b;
    repaint (circle (d.p, d.radius * 5.0f + 2.0f).getSmallestIntegerContainer());
}

void RackModule::setLedColour (int index, juce::Colour c)
{
    if (! juce::isPositiveAndBelow (index, (int) leds.size()) || leds[(size_t) index].colour == c)
        return;

    leds[(size_t) index].colour = c;
    repaint (circle (leds[(size_t) index].p, leds[(size_t) index].radius * 5.0f + 2.0f).getSmallestIntegerContainer());
}

void RackModule::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    rk::paintPanel (g, r, zoom);
    rk::paintScrews (g, r, zoom);
    rk::paintTitle (g, r, title, zoom);

    for (const auto& b : boxes)       rk::paintOutputBox (g, b, zoom);
    for (const auto& [a, b] : rules)  rk::paintRule (g, a, b, zoom);
    for (const auto& l : labels)      rk::paintLabel (g, l.text, l.r, zoom, l.px, l.just, l.onBox, l.dim);
    for (const auto& j : jacks)       rk::paintJack (g, j, zoom);
    for (const auto& d : leds)        rk::paintLight (g, d.p, d.radius, d.colour, d.b);

    if (brand.isNotEmpty())
        rk::paintBrand (g, r, brand, zoom);
}

//==============================================================================
void RackChainRow::resized()
{
    const auto b  = getLocalBounds().toFloat();
    const float hp = 15.0f * zoom;
    float x = b.getX();

    inModule = { x, b.getY(), 4.0f * hp, b.getHeight() };
    x += 4.0f * hp;

    for (auto& r : rects)
    {
        r = { x, b.getY(), 11.0f * hp, b.getHeight() };
        x += 11.0f * hp;
    }

    guardRect = { x, b.getY(), 8.0f * hp, b.getHeight() };
    x += 8.0f * hp;
    outModule = { x, b.getY(), 7.0f * hp, b.getHeight() };

    inNode  = { inModule.getCentreX(), inModule.getY() + 334.0f * zoom };
    outNode = { outModule.getCentreX(), outModule.getY() + 334.0f * zoom };
}

juce::Rectangle<float> RackChainRow::powerRect (juce::Rectangle<float> m) const
{
    // LIMIT 的「开关」是左上那个拨动开关；插件模块的是右边的 ON 灯键
    if (m == guardRect)
        return juce::Rectangle<float> (22.0f * zoom, 30.0f * zoom).withCentre ({ m.getX() + 24.0f * zoom, m.getY() + 66.0f * zoom });

    return circle ({ m.getRight() - 40.0f * zoom, m.getY() + 222.0f * zoom }, 15.0f * zoom);
}

juce::Rectangle<float> RackChainRow::editRect (juce::Rectangle<float> m) const
{
    return circle ({ m.getX() + 40.0f * zoom, m.getY() + 222.0f * zoom }, 13.0f * zoom);
}

juce::Rectangle<float> RackChainRow::programRect (juce::Rectangle<float> m) const
{
    return { m.getX() + 8.0f * zoom, m.getY() + 46.0f * zoom, m.getWidth() - 16.0f * zoom, 19.0f * zoom };
}

juce::Rectangle<float> RackChainRow::sigRect (juce::Rectangle<float> m) const
{
    return { m.getX() + 26.0f * zoom, m.getY() + 250.0f * zoom, m.getWidth() - 30.0f * zoom, 36.0f * zoom };
}

juce::Rectangle<int> RackChainRow::ceilingKnobArea() const
{
    return juce::Rectangle<float> (52.0f * zoom, 52.0f * zoom)
               .withCentre ({ guardRect.getX() + 84.0f * zoom, guardRect.getY() + 68.0f * zoom })
               .getSmallestIntegerContainer();
}

std::array<juce::Rectangle<float>, kMaxChain> RackChainRow::displayRects() const
{
    if (! dragActive || ! juce::isPositiveAndBelow (pressSlot, kMaxChain))
        return rects;

    std::array<juce::Rectangle<float>, kMaxChain> d;
    for (int s = 0; s < kMaxChain; ++s)
        d[(size_t) s] = s == pressSlot ? rects[(size_t) s].translated (dragDx, 0.0f)
                                       : rects[(size_t) chainPositionAfterMove (s, pressSlot, dropIndex)];
    return d;
}

void RackChainRow::setExtras (const std::array<Extra, kMaxChain>& e)
{
    bool changed = false;
    for (size_t i = 0; i < e.size(); ++i)
        if (! e[i].sameAs (extras[i]))
        {
            changed = true;
            break;
        }

    if (! changed)
        return;

    extras = e;
    repaint();
}

void RackChainRow::setLevels (const std::array<float, kMaxChain>& s, float il, float ir, float ol, float orr, float m)
{
    auto differs = [] (float a, float b) { return std::abs (rk::levelToBrightness (a) - rk::levelToBrightness (b)) > 0.02f; };

    for (int i = 0; i < kMaxChain; ++i)
        if (differs (s[(size_t) i], slotLevel[(size_t) i]))
        {
            slotLevel[(size_t) i] = s[(size_t) i];
            if (slots[(size_t) i].loaded)
                repaint (sigRect (displayRects()[(size_t) i]).getSmallestIntegerContainer());
        }

    if (differs (il, inL) || differs (ir, inR) || std::abs (m - midi) > 0.02f)
    {
        inL = il; inR = ir; midi = m;
        repaint (inModule.withTrimmedTop (36.0f * zoom).withTrimmedBottom (78.0f * zoom).getSmallestIntegerContainer());
    }

    if (differs (ol, outL) || differs (orr, outR))
    {
        outL = ol; outR = orr;
        repaint (outModule.withTrimmedTop (38.0f * zoom).withTrimmedBottom (80.0f * zoom).getSmallestIntegerContainer());
    }
}

HostLook RackChainRow::lookFor (int i) const
{
    const auto& s = slots[(size_t) i];
    const auto& e = extras[(size_t) i];

    HostLook L;
    L.number      = i + 1;
    L.name        = s.name;
    L.brand       = e.brand;
    L.tag         = s.tag;
    L.tagColour   = s.colour;
    L.program     = e.program;
    L.info        = e.info;
    L.active      = ! s.bypassed;
    L.editorOpen  = e.editorOpen;
    L.hasPrograms = e.hasPrograms;
    L.bars        = e.bars;
    L.flags       = e.flags;
    L.level       = slotLevel[(size_t) i];
    return L;
}

//==============================================================================
void RackChainRow::paintInModule (juce::Graphics& g)
{
    const auto r = inModule;
    const float z = zoom;

    rk::paintPanel (g, r, z);
    rk::paintScrews (g, r, z);
    rk::paintTitle (g, r, "IN", z);

    // 宿主送进来的 MIDI（合成器版就靠它）+ 左右声道的输入电平
    rk::paintLight (g, { r.getCentreX(), r.getY() + 50.0f * z }, 3.2f * z, rk::yellow, midi);
    rk::paintLabel (g, "MIDI", { r.getX(), r.getY() + 57.0f * z, r.getWidth(), 10.0f * z }, z, 8.0f, juce::Justification::centred, false, true);

    for (const auto [cx, lvl, name] : { std::tuple { 20.0f, inL, "L" }, std::tuple { 40.0f, inR, "R" } })
    {
        rk::paintLadder (g, { r.getX() + (cx - 6.0f) * z, r.getY() + 76.0f * z, 12.0f * z, 210.0f * z }, 10, lvl, z);
        rk::paintLabel (g, name, { r.getX() + (cx - 8.0f) * z, r.getY() + 288.0f * z, 16.0f * z, 11.0f * z }, z, 8.5f);
    }

    const auto box = juce::Rectangle<float> (r.getX() + 5.0f * z, r.getY() + 302.0f * z, r.getWidth() - 10.0f * z, 50.0f * z);
    rk::paintOutputBox (g, box, z);
    rk::paintLabel (g, "OUT", box.withHeight (15.0f * z).translated (0.0f, 2.0f * z), z, 8.5f, juce::Justification::centred, true);
    rk::paintJack (g, inNode, z);
}

void RackChainRow::paintOutModule (juce::Graphics& g)
{
    const auto r = outModule;
    const float z = zoom;

    rk::paintPanel (g, r, z);
    rk::paintScrews (g, r, z);
    rk::paintTitle (g, r, "OUT", z);

    const float top = r.getY() + 50.0f * z, h = 230.0f * z;

    for (const auto [cx, lvl, name] : { std::tuple { 30.0f, outL, "L" }, std::tuple { 75.0f, outR, "R" } })
    {
        rk::paintLadder (g, { r.getX() + (cx - 6.0f) * z, top, 12.0f * z, h }, 12, lvl, z);
        rk::paintLabel (g, name, { r.getX() + (cx - 8.0f) * z, top + h + 4.0f * z, 16.0f * z, 11.0f * z }, z, 8.5f);
    }

    // 刻度：和 paintLadder 用同一条门限曲线（-48 dB · (1−t)^1.6），所以字对得上灯
    for (const float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
    {
        const float t    = 1.0f - std::pow (-db / 48.0f, 1.0f / 1.6f);
        const float cell = h / 12.0f;
        const int   i    = juce::jlimit (0, 11, juce::roundToInt (t * 11.0f));
        const float y    = top + h - cell * ((float) i + 0.5f);
        rk::paintLabel (g, db == 0.0f ? juce::String ("0") : juce::String (juce::roundToInt (db)),
                        { r.getX() + 38.0f * z, y - 5.0f * z, 29.0f * z, 10.0f * z }, z, 7.5f, juce::Justification::centred, false, true);
    }

    rk::paintLabel (g, "IN", { r.getCentreX() - 20.0f * z, r.getY() + 305.0f * z, 40.0f * z, 11.0f * z }, z, 9.0f);
    rk::paintJack (g, outNode, z);
    rk::paintBrand (g, r, "ROULETTE", z);
}

void RackChainRow::paintLimiter (juce::Graphics& g)
{
    const auto r = guardRect;
    const float z = zoom;
    const bool hov = hoverPart == Part::guardPower;

    rk::paintPanel (g, r, z);
    rk::paintScrews (g, r, z);
    rk::paintTitle (g, r, "LIMIT", z);

    // 开关：拨上 = 开
    rk::paintLabel (g, "保护"_u8, { r.getX() + 4.0f * z, r.getY() + 40.0f * z, 40.0f * z, 12.0f * z }, z, 9.5f);
    rk::paintSwitch (g, juce::Rectangle<float> (14.0f * z, 20.6f * z).withCentre ({ r.getX() + 24.0f * z, r.getY() + 66.0f * z }),
                     2, guard.on ? 0 : 1, true, hov);
    rk::paintLabel (g, "ON",  { r.getX() + 33.0f * z, r.getY() + 54.0f * z, 22.0f * z, 9.0f * z }, z, 7.5f, juce::Justification::centredLeft, false, true);
    rk::paintLabel (g, "OFF", { r.getX() + 33.0f * z, r.getY() + 70.0f * z, 22.0f * z, 9.0f * z }, z, 7.5f, juce::Justification::centredLeft, false, true);

    // 天花板：旋钮是子组件（editor 摆在 ceilingKnobArea），这里只印字和读数屏
    rk::paintLabel (g, "天花板"_u8, { r.getX() + 58.0f * z, r.getY() + 36.0f * z, 52.0f * z, 12.0f * z }, z, 9.5f);
    const auto lcd = juce::Rectangle<float> (r.getX() + 58.0f * z, r.getY() + 94.0f * z, 52.0f * z, 16.0f * z);
    rk::paintDisplay (g, lcd, z);
    g.setColour (rk::yellow);
    g.setFont (rk::displayFont ("-1.0 dB", 9.5f * z));
    g.drawText (juce::String (guard.ceilingDb, 1) + " dB", lcd, juce::Justification::centred, false);

    // 增益衰减灯：1 / 2 / 3 / 6 / 9 / 12 dB，越往上越红
    static const float steps[] = { 1.0f, 2.0f, 3.0f, 6.0f, 9.0f, 12.0f };
    for (int k = 0; k < 6; ++k)
    {
        const float y  = r.getY() + (238.0f - (float) k * 20.0f) * z;
        const float b  = guard.on ? juce::jlimit (0.0f, 1.0f, guard.grDb - steps[k] + 1.0f) : 0.0f;
        const auto  c  = k >= 4 ? rk::red : (k >= 3 ? rk::orange : rk::yellow);
        rk::paintLight (g, { r.getX() + 34.0f * z, y }, 3.4f * z, c, b);
        rk::paintLabel (g, juce::String (juce::roundToInt (steps[k])), { r.getX() + 8.0f * z, y - 5.0f * z, 18.0f * z, 10.0f * z },
                        z, 8.0f, juce::Justification::centredRight, false, true);
    }
    rk::paintLabel (g, "GR dB", { r.getX() + 8.0f * z, r.getY() + 250.0f * z, 46.0f * z, 11.0f * z }, z, 8.0f);

    // NaN：某个子插件吐了非法样本、已经清零
    rk::paintLight (g, { r.getX() + 86.0f * z, r.getY() + 150.0f * z }, 4.6f * z, rk::red, guard.nanWarn ? 1.0f : 0.0f);
    rk::paintLabel (g, "NaN", { r.getX() + 66.0f * z, r.getY() + 160.0f * z, 40.0f * z, 11.0f * z }, z, 8.5f);

    rk::paintLabel (g, "IN", { r.getX() + 10.0f * z, r.getY() + 305.0f * z, 40.0f * z, 11.0f * z }, z, 9.0f);
    rk::paintJack (g, { r.getX() + 30.0f * z, r.getY() + 334.0f * z }, z);
    const auto box = juce::Rectangle<float> (r.getRight() - 60.0f * z, r.getY() + 300.0f * z, 52.0f * z, 51.0f * z);
    rk::paintOutputBox (g, box, z);
    rk::paintLabel (g, "OUT", box.withHeight (16.0f * z).translated (0.0f, 2.0f * z), z, 9.0f, juce::Justification::centred, true);
    rk::paintJack (g, { r.getRight() - 34.0f * z, r.getY() + 334.0f * z }, z);

    rk::paintBrand (g, r, "ROULETTE", z);
}

void RackChainRow::paint (juce::Graphics& g)
{
    const float z = zoom;
    const auto disp = displayRects();

    paintInModule (g);

    for (int i = 0; i < kMaxChain; ++i)
    {
        if (dragActive && i == pressSlot)
            continue;

        const auto r = disp[(size_t) i];

        if (slots[(size_t) i].loaded)
        {
            paintHostModule (g, r, lookFor (i), z);
            continue;
        }

        // 空位：机架上什么都没有（导轨露出来），悬停 / 聚焦时才描一圈虚线、写上能干什么
        const bool hov = i == hoverIndex && ! dragActive;
        const bool foc = i == focused;

        g.setColour (rk::ui (0.32f));
        g.setFont (rk::titleFont (11.0f * z));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (r.getX() + 8.0f * z, r.getY() + 18.0f * z, 20.0f * z, 14.0f * z),
                    juce::Justification::centredLeft, false);

        if (hov || foc)
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r.reduced (4.0f * z, 18.0f * z), 4.0f * z);
            const float dashes[] = { 5.0f * z, 4.0f * z };
            juce::PathStrokeType (1.2f * z).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (foc ? juce::Colour (0xffff0000).withAlpha (0.6f) : rk::ui (0.55f));
            g.fillPath (dashed);

            const auto c = r.getCentre();
            theme::drawIcon (g, theme::Icon::plus, circle (c.translated (0.0f, -12.0f * z), 9.0f * z), rk::ui (0.75f), 1.8f * z);
            g.setColour (rk::ui (0.75f));
            g.setFont (rk::uiFont (11.5f * z));
            g.drawText ("添加插件"_u8, juce::Rectangle<float> (r.getX(), c.y + 4.0f * z, r.getWidth(), 16.0f * z),
                        juce::Justification::centred, false);
            g.setColour (rk::ui (0.45f));
            g.setFont (rk::uiFont (10.0f * z));
            g.drawText ("Enter / 点一下"_u8, juce::Rectangle<float> (r.getX(), c.y + 21.0f * z, r.getWidth(), 14.0f * z),
                        juce::Justification::centred, false);
        }
    }

    paintLimiter (g);
    paintOutModule (g);

    // 聚焦的模块：Rack 2 的选区样子
    if (juce::isPositiveAndBelow (focused, kMaxChain) && slots[(size_t) focused].loaded && ! (dragActive && focused == pressSlot))
        rk::paintSelection (g, disp[(size_t) focused], z);

    // 正在拖的那块最后画、浮起来一点
    if (dragActive && juce::isPositiveAndBelow (pressSlot, kMaxChain))
    {
        const auto r = disp[(size_t) pressSlot];
        juce::Path shape;
        shape.addRectangle (r);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::roundToInt (16.0f * z), { 0, juce::roundToInt (6.0f * z) })
            .drawForPath (g, shape);
        paintHostModule (g, r, lookFor (pressSlot), z);
        rk::paintSelection (g, r, z);

        // 落点：一道竖线
        const auto target = rects[(size_t) dropIndex];
        g.setColour (juce::Colour (0xffff0000).withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (target.getX() - 1.5f * z, target.getY() + 16.0f * z, 3.0f * z, target.getHeight() - 32.0f * z));
    }
}

//==============================================================================
std::vector<RackChainRow::Cable> RackChainRow::cables() const
{
    std::vector<Cable> v;
    const auto disp = displayRects();
    const float z = zoom;

    // 每个显示位置上是哪个链位（拖动时是预览之后的顺序）
    std::array<int, kMaxChain> slotAt;
    slotAt.fill (-1);
    for (int s = 0; s < kMaxChain; ++s)
        slotAt[(size_t) (dragActive ? chainPositionAfterMove (s, pressSlot, dropIndex) : s)] = s;

    auto prev       = inNode;
    int  prevColour = 0;
    float prevSig   = juce::jmax (inL, inR);
    int  prevSlot   = -1;

    for (int pos = 0; pos < kMaxChain; ++pos)
    {
        const int s = slotAt[(size_t) pos];
        if (s < 0 || ! slots[(size_t) s].loaded)
            continue;      // 空位：线从它上面跨过去（信号本来就直接穿过空位）

        const auto m = disp[(size_t) s];
        v.push_back ({ prev, hostIn (m), prevColour, prevSig, hoverIndex == s || (prevSlot >= 0 && hoverIndex == prevSlot) });

        prev       = hostOut (m);
        prevColour = pos + 1;      // 线的颜色跟着"从哪个位置出来"，装卸别的模块时不会整条链换色
        prevSig    = slotLevel[(size_t) s];
        prevSlot   = s;
    }

    const bool guardHover = hoverPart == Part::guardBody || hoverPart == Part::guardPower;
    const juce::Point<float> limIn  { guardRect.getX() + 30.0f * z, guardRect.getY() + 334.0f * z };
    const juce::Point<float> limOut { guardRect.getRight() - 34.0f * z, guardRect.getY() + 334.0f * z };

    v.push_back ({ prev, limIn, prevColour, prevSig, guardHover || (prevSlot >= 0 && hoverIndex == prevSlot) });
    v.push_back ({ limOut, outNode, 7, juce::jmax (outL, outR), guardHover });
    return v;
}

//==============================================================================
void RackChainRow::mouseMove (const juce::MouseEvent& e)
{
    const int  hi = hoverIndex;
    const auto hp = hoverPart;

    ChainStrip::mouseMove (e);

    // 模块本体上是"可以拖"的手，键上是指头
    if (hoverIndex >= 0 && hoverPart == Part::body && slots[(size_t) hoverIndex].loaded)
    {
        const auto& m = rects[(size_t) hoverIndex];
        const bool onKey = editRect (m).contains (e.position)
                        || (extras[(size_t) hoverIndex].hasPrograms && programRect (m).contains (e.position));
        setMouseCursor (onKey ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
    }

    if ((hi != hoverIndex || hp != hoverPart) && onCablesChanged != nullptr)
        onCablesChanged();
}

void RackChainRow::mouseExit (const juce::MouseEvent& e)
{
    ChainStrip::mouseExit (e);

    if (onCablesChanged != nullptr)
        onCablesChanged();
}

void RackChainRow::mouseDown (const juce::MouseEvent& e)
{
    pressSlot  = -1;
    dragActive = false;
    dragDx     = 0.0f;

    const auto p = e.position;

    if (! e.mods.isPopupMenu())
    {
        for (int i = 0; i < kMaxChain; ++i)
        {
            if (! slots[(size_t) i].loaded)
                continue;

            if (editRect (rects[(size_t) i]).contains (p))
            {
                if (onFocus != nullptr)      onFocus (i);
                if (onEditToggle != nullptr) onEditToggle (i);
                return;
            }

            if (extras[(size_t) i].hasPrograms && programRect (rects[(size_t) i]).contains (p))
            {
                if (onFocus != nullptr)       onFocus (i);
                if (onProgramMenu != nullptr) onProgramMenu (i);
                return;
            }
        }
    }

    ChainStrip::mouseDown (e);

    // 按在装了插件的模块本体上：可能是拖动的开始（真动了才算，见 mouseDrag）
    const auto [i, part] = hitTest (p);
    if (! e.mods.isPopupMenu() && i >= 0 && part == Part::body && slots[(size_t) i].loaded)
    {
        pressSlot = i;
        pressPos  = p;
        dropIndex = i;
    }
}

void RackChainRow::mouseDrag (const juce::MouseEvent& e)
{
    if (pressSlot < 0)
        return;

    if (! dragActive)
    {
        if (e.position.getDistanceFrom (pressPos) < 6.0f * zoom)
            return;

        dragActive = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    const float minDx = rects[0].getX() - rects[(size_t) pressSlot].getX();
    const float maxDx = rects[kMaxChain - 1].getX() - rects[(size_t) pressSlot].getX();
    dragDx = juce::jlimit (minDx, maxDx, e.position.x - pressPos.x);

    const float cx = rects[(size_t) pressSlot].getCentreX() + dragDx;
    int best = pressSlot;
    float bestD = 1.0e9f;
    for (int j = 0; j < kMaxChain; ++j)
        if (const float d = std::abs (rects[(size_t) j].getCentreX() - cx); d < bestD)
        {
            bestD = d;
            best  = j;
        }

    dropIndex = best;
    repaint();

    if (onCablesChanged != nullptr)
        onCablesChanged();
}

void RackChainRow::mouseUp (const juce::MouseEvent&)
{
    if (! dragActive)
    {
        pressSlot = -1;
        return;
    }

    const int from = pressSlot, to = dropIndex;
    dragActive = false;
    pressSlot  = -1;
    dragDx     = 0.0f;
    repaint();

    if (onCablesChanged != nullptr)
        onCablesChanged();

    if (from != to && onMove != nullptr)
        onMove (from, to);
}

juce::String RackChainRow::getTooltip()
{
    const auto p = getMouseXYRelative().toFloat();

    for (int i = 0; i < kMaxChain; ++i)
    {
        if (! slots[(size_t) i].loaded)
            continue;

        if (editRect (rects[(size_t) i]).contains (p))
            return "打开 / 关闭这个插件自己的界面（灯亮 = 开着）。双击模块也行"_u8;

        if (extras[(size_t) i].hasPrograms && programRect (rects[(size_t) i]).contains (p))
            return "子插件的 program。点一下换；换了会清空运动模式的基座"_u8;
    }

    if (juce::isPositiveAndBelow (hoverIndex, kMaxChain) && slots[(size_t) hoverIndex].loaded)
    {
        const auto& s = slots[(size_t) hoverIndex];

        if (hoverPart == Part::power)
            return s.bypassed ? "ON 灯灭着 = 已旁通。点一下恢复（Ctrl+E）"_u8
                              : "ON：灯亮 = 在工作。点一下旁通（Ctrl+E），信号直接穿过它"_u8;

        if (hoverPart == Part::body)
            return s.name + "\n"_u8 + (s.tag.isNotEmpty() ? s.tag + " · "_u8 : juce::String()) + s.detail
                   + "\n\n单击：聚焦（随机化、参数、快照都作用在它上面）\n拖动：调整串联顺序\n"_u8
                     "双击：打开插件界面\n右键：更多操作\n\n参数图：每根柱子是一个参数的当前值，黄的 = 刚改过"_u8;
    }

    if (hoverPart == Part::guardBody && ceilingKnobArea().toFloat().contains (p))
        return {};

    return ChainStrip::getTooltip();
}

//==============================================================================
void RackCableLayer::paint (juce::Graphics& g)
{
    const auto off = (row.getPosition() - getPosition()).toFloat();
    const auto cs  = row.cables();

    // 先画全部线，再画全部插头（插头压在线上面，和 Rack 一样）
    for (const auto& c : cs)
        rk::paintCable (g, c.out + off, c.in + off, rk::cableColour (c.colourIndex), zoom, c.highlight ? 1.0f : -1.0f, false);

    for (const auto& c : cs)
    {
        const auto a = c.out + off, b = c.in + off;
        const auto slump = rk::slumpPoint (a, b, zoom);
        const float sig  = rk::levelToBrightness (c.signal);
        rk::paintPlug (g, a, slump, rk::cableColour (c.colourIndex), zoom, sig);
        rk::paintPlug (g, b, slump, rk::cableColour (c.colourIndex), zoom, sig);
    }
}

void RackCableLayer::repaintPlugs()
{
    const auto off = (row.getPosition() - getPosition()).toFloat();
    const float r  = 14.0f * zoom;

    for (const auto& c : row.cables())
    {
        repaint (circle (c.out + off, r).getSmallestIntegerContainer());
        repaint (circle (c.in + off, r).getSmallestIntegerContainer());
    }
}

//==============================================================================
RackParamList::RackParamList()
{
    setRowHeights (22, 22);
}

void RackParamList::setZoom (float z)
{
    zoom = z;
    setRowHeights (juce::roundToInt (22.0f * juce::jmax (0.9f, z)), juce::roundToInt (22.0f * juce::jmax (0.9f, z)));
}

void RackParamList::paint (juce::Graphics& g)
{
    rk::paintDisplay (g, getLocalBounds().toFloat(), zoom);
    paintHeader (g, getLocalBounds().removeFromTop (headerH));

    if (! visible.isEmpty())
        return;

    const auto area = getLocalBounds().withTrimmedTop (headerH).toFloat();
    const bool noPlugin = model == nullptr || model->isEmpty();
    const juce::String big = noPlugin ? "NO PLUGIN" : "NO MATCH";

    g.setColour (rk::yellow);
    g.setFont (rk::displayFont (big, 18.0f * zoom));
    g.drawText (big, area.withHeight (26.0f * zoom).withCentre ({ area.getCentreX(), area.getCentreY() - 14.0f * zoom }),
                juce::Justification::centred, false);

    g.setColour (rk::ui (0.55f));
    g.setFont (rk::uiFont (11.5f * zoom));
    g.drawText (noPlugin ? "这个链位没有插件——点上面机架里的空位装一个"_u8 : "没有符合筛选的参数，换个关键词或者切回「全部」"_u8,
                area.withHeight (18.0f * zoom).withCentre ({ area.getCentreX(), area.getCentreY() + 12.0f * zoom }),
                juce::Justification::centred, true);
}

void RackParamList::paintHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto cols = layoutCols (area.withWidth (viewport.getMaximumVisibleWidth()));

    // 全选：一颗灯。全勾 = 亮，部分 = 半亮，全没勾 = 灭
    {
        const int nChecked = getNumChecked();
        const int nVisible = visible.size();
        const float b = nVisible > 0 && nChecked == nVisible ? 1.0f : (nChecked > 0 ? 0.35f : 0.0f);
        rk::paintLight (g, cols.check.toFloat().getCentre(), 3.6f * zoom, rk::green, b, true);
    }

    g.setFont (rk::uiFont (10.0f * zoom));
    g.setColour (rk::ui (0.5f));
    g.drawText ("#",             cols.index, juce::Justification::centredRight, false);
    g.drawText ("参数"_u8,       cols.name,  juce::Justification::centredLeft,  false);
    g.drawText ("类型"_u8,       cols.kind,  juce::Justification::centredLeft,  false);
    g.drawText ("旧值"_u8,       cols.was,   juce::Justification::centredLeft,  false);
    g.drawText ("当前值"_u8,     cols.value, juce::Justification::centredLeft,  false);
    g.drawText ("幅度"_u8,       cols.span,  juce::Justification::centred,      false);
    g.drawText ("备注"_u8,       cols.note.withTrimmedLeft (8), juce::Justification::centredLeft, false);

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRect (area.getX() + 4, area.getBottom() - 1, area.getWidth() - 8, 1);
}

void RackParamList::paintRow (juce::Graphics& g, int displayRow, juce::Rectangle<int> area)
{
    if (model == nullptr)
        return;

    const int mi = visible.getUnchecked (displayRow);
    if (! juce::isPositiveAndBelow (mi, model->size()))
        return;

    const auto& p    = model->getReference (mi);
    const bool  on   = isChecked (mi);
    const auto  cols = layoutCols (area);
    const float z    = zoom;

    if (displayRow == hoverRow)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (area.reduced (2, 0));
    }

    // 本次改动过：最左边一个黄色的 ▸
    if (p.changed)
    {
        juce::Path tri;
        const float cy = (float) area.getCentreY(), x = (float) area.getX() + 2.0f;
        tri.addTriangle (x, cy - 3.5f * z, x, cy + 3.5f * z, x + 4.5f * z, cy);
        g.setColour (rk::yellow);
        g.fillPath (tri);
    }

    // 勾选：一颗绿灯
    rk::paintLight (g, cols.check.toFloat().getCentre(), 3.6f * z, rk::green, on ? 1.0f : 0.0f, true);

    g.setFont (rk::displayFont ("0", 10.5f * z));
    g.setColour (rk::ui (0.42f));
    g.drawText (juce::String (p.index), cols.index, juce::Justification::centredRight, false);

    const bool inert = p.health == Health::Inert;
    g.setFont (rk::displayFont (p.name, 11.0f * z));
    g.setColour (inert ? rk::red : (on ? juce::Colour (0xffe6e6e6) : rk::ui (0.45f)));
    g.drawText (p.name, cols.name, juce::Justification::centredLeft, true);

    if (! cols.kind.isEmpty())
    {
        juce::String label = kindLabel (p.kind);
        if (p.kind == Kind::Stepped)
            label << " " << p.numSteps;

        g.setColour (p.kind == Kind::Toggle ? rk::green : p.kind == Kind::Stepped ? rk::blue : rk::ui (0.5f));
        g.setFont (rk::uiFont (10.0f * z));
        g.drawText (label, cols.kind, juce::Justification::centredLeft, true);
    }

    if (p.changed && ! cols.was.isEmpty())
    {
        g.setFont (rk::displayFont (p.prevDisplay, 10.5f * z));
        g.setColour (rk::ui (0.5f));
        g.drawText (p.prevDisplay, cols.was.withTrimmedRight (6), juce::Justification::centredLeft, true);
    }

    // 当前值（黄字）+ 下面一道细条；改动过的旧值位置有个橙色小刻度
    {
        const auto vr = cols.value;
        g.setFont (rk::displayFont (p.display, 11.0f * z));
        g.setColour (p.blob ? rk::orange : (p.changed ? rk::yellow : rk::yellow.withAlpha (0.82f)));
        g.drawText (p.display, vr.withTrimmedBottom (4).withTrimmedRight (8), juce::Justification::centredLeft, true);

        if (! p.blob)
        {
            const auto bar = juce::Rectangle<float> ((float) vr.getX(), (float) vr.getBottom() - 4.0f * z, (float) vr.getWidth() - 10.0f, 2.0f * z);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRect (bar);
            g.setColour (rk::yellow.withAlpha (p.changed ? 0.95f : 0.6f));
            g.fillRect (bar.withWidth (juce::jmax (1.5f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, p.value))));

            if (p.changed)
            {
                const float px = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, p.prevValue);
                g.setColour (rk::orange);
                g.fillRect (juce::Rectangle<float> (px - 0.75f, bar.getY() - 2.0f * z, 1.5f, bar.getHeight() + 4.0f * z));
            }
        }
    }

    {
        const bool hov = displayRow == hoverRow && hoverSpan;

        if (p.hasCustomSpan())
        {
            const auto label = juce::String::fromUTF8 ("\xc2\xb1") + juce::String (p.span, 3);
            g.setFont (rk::displayFont ("0", 10.5f * z));
            g.setColour (hov ? rk::cyan.brighter (0.2f) : rk::cyan);
            g.drawFittedText (label, cols.span, juce::Justification::centred, 1, 0.8f);
        }
        else
        {
            g.setFont (rk::uiFont (12.0f * z, true));
            g.setColour (hov ? rk::cyan : rk::ui (0.35f));
            g.drawText (hov ? "+" : juce::String::fromUTF8 ("\xc2\xb7"), cols.span, juce::Justification::centred, false);
        }
    }

    if (! cols.note.isEmpty())
    {
        juce::String noteText;
        juce::Colour dot = juce::Colours::transparentBlack;

        if (p.health == Health::Inert)             { noteText = healthLabel (p.health); dot = rk::red; }
        else if (p.health == Health::EndpointOnly) { noteText = healthLabel (p.health); dot = rk::orange; }
        else if (p.defaultSkip())                  { noteText = skipReasonLabel (p.skipReason); }
        else if (p.health == Health::Ok)           { noteText = healthLabel (p.health); dot = rk::green; }

        auto nr = cols.note.withTrimmedLeft (8);

        if (! dot.isTransparent())
        {
            rk::paintLight (g, nr.removeFromLeft (8).toFloat().getCentre(), 2.6f * z, dot, 1.0f, true);
            nr.removeFromLeft (4);
        }

        g.setFont (rk::uiFont (10.5f * z));
        g.setColour (dot.isTransparent() ? rk::ui (0.45f) : dot);
        g.drawText (noteText, nr, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
RackScope::RackScope (const SnapshotBank& b)
    : MorphPad (b)
{
}

juce::Rectangle<float> RackScope::padArea() const
{
    auto r = getLocalBounds().toFloat().reduced (14.0f * zoom);

    if (mode == MorphMode::TwoD)
    {
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        return r.withSizeKeepingCentre (s, s);
    }

    return r.reduced (6.0f * zoom, 0.0f);
}

void RackScope::paintSpot (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour col, float k)
{
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.5f * k), c.x, c.y, col.withAlpha (0.0f), c.x + radius * 3.2f, c.y, true));
    g.fillEllipse (circle (c, radius * 3.2f));
    g.setColour (col.withAlpha (k));
    g.fillEllipse (circle (c, radius));
    g.setColour (juce::Colours::white.withAlpha (0.75f * k));
    g.fillEllipse (circle (c, radius * 0.45f));
}

void RackScope::paint (juce::Graphics& g)
{
    const float z = zoom;
    const auto  b = getLocalBounds().toFloat();
    rk::paintDisplay (g, b, z);

    const auto r = padArea();
    if (r.getWidth() < 8.0f || r.getHeight() < 8.0f)
        return;

    // Scope 的刻度：8 × 8 格 + 中间十字
    const auto grid = mode == MorphMode::TwoD ? r : b.reduced (8.0f * z);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (int k = 1; k < 8; ++k)
    {
        g.fillRect (juce::Rectangle<float> (grid.getX() + grid.getWidth() * (float) k / 8.0f - 0.5f, grid.getY(), 1.0f, grid.getHeight()));
        g.fillRect (juce::Rectangle<float> (grid.getX(), grid.getY() + grid.getHeight() * (float) k / 8.0f - 0.5f, grid.getWidth(), 1.0f));
    }
    g.setColour (juce::Colours::white.withAlpha (0.13f));
    g.fillRect (juce::Rectangle<float> (grid.getCentreX() - 0.5f, grid.getY(), 1.0f, grid.getHeight()));
    g.fillRect (juce::Rectangle<float> (grid.getX(), grid.getCentreY() - 0.5f, grid.getWidth(), 1.0f));

    const int nUsed = bank.numUsed();
    const auto slotColour = [] (int s) { return rk::cableColour (s); };
    juce::Point<float> handle;

    if (mode == MorphMode::TwoD)
    {
        const juce::Point<float> corner[kNumSlots] = { { r.getX(), r.getY() }, { r.getRight(), r.getY() },
                                                       { r.getX(), r.getBottom() }, { r.getRight(), r.getBottom() } };
        handle = { r.getX() + posX * r.getWidth(), r.getY() + posY * r.getHeight() };
        const auto w = bank.computeWeights (MorphMode::TwoD, posX, posY);

        for (int s = 0; s < kNumSlots; ++s)
        {
            const float weight = w.weightOf (s);
            if (weight <= 0.001f)
                continue;

            g.setColour (slotColour (s).withAlpha (0.15f + 0.25f * weight));
            g.drawLine ({ handle, corner[s] }, (2.0f + 5.0f * weight) * z);
            g.setColour (slotColour (s).withAlpha (0.45f + 0.5f * weight));
            g.drawLine ({ handle, corner[s] }, (0.8f + 1.4f * weight) * z);
        }

        for (int s = 0; s < kNumSlots; ++s)
        {
            const bool used = bank.isUsed (s);
            const bool win  = used && bank.getWinner() == s;

            if (used)
                paintSpot (g, corner[s], (win ? 4.6f : 3.6f) * z, slotColour (s), win ? 1.0f : 0.75f);
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.2f));
                g.drawRect (juce::Rectangle<float> (7.0f * z, 7.0f * z).withCentre (corner[s]), 1.0f);
            }

            const bool left = (s == 0 || s == 2), top = (s == 0 || s == 1);
            g.setFont (rk::displayFont ("1", 10.0f * z));
            g.setColour (slotColour (s).withAlpha (used ? 0.95f : 0.35f));
            g.drawText (juce::String (s + 1),
                        juce::Rectangle<float> (left ? r.getX() + 7.0f * z : r.getRight() - 21.0f * z,
                                                top ? r.getY() + 3.0f * z : r.getBottom() - 15.0f * z, 14.0f * z, 12.0f * z),
                        left ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
        }
    }
    else
    {
        const float trackY = r.getBottom() - 16.0f * z;
        const auto  plot   = juce::Rectangle<float> (r.getX(), r.getY() + 12.0f * z, r.getWidth(), juce::jmax (0.0f, trackY - 26.0f * z - r.getY()));
        handle = { r.getX() + posX * r.getWidth(), trackY };

        int used[kNumSlots];
        int n = 0;
        for (int i = 0; i < kNumSlots; ++i)
            if (bank.isUsed (i))
                used[n++] = i;

        // 每个快照一条权重曲线——和 computeWeights 的 1D 分段线性同源
        if (n >= 2 && plot.getHeight() > 20.0f)
        {
            constexpr int kSteps = 96;
            for (int k = 0; k < n; ++k)
            {
                const int s = used[k];
                juce::Path curve;
                for (int i = 0; i <= kSteps; ++i)
                {
                    const float x = (float) i / (float) kSteps;
                    const float w = bank.computeWeights (MorphMode::OneD, x, 0.0f).weightOf (s);
                    const juce::Point<float> p { r.getX() + x * r.getWidth(), plot.getBottom() - w * plot.getHeight() };
                    if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
                }

                g.setColour (slotColour (s).withAlpha (0.18f));
                g.strokePath (curve, juce::PathStrokeType (4.0f * z, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (slotColour (s).withAlpha (0.95f));
                g.strokePath (curve, juce::PathStrokeType (1.5f * z, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (handle.x - 0.5f, plot.getY(), 1.0f, trackY - plot.getY()));
        }

        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.fillRect (juce::Rectangle<float> (r.getX(), trackY - 0.5f, r.getWidth(), 1.0f));

        for (int i = 0; i < n; ++i)
        {
            const float t = (n == 1) ? 0.5f : (float) i / (float) (n - 1);
            const juce::Point<float> c { r.getX() + t * r.getWidth(), trackY };
            g.setColour (slotColour (used[i]));
            g.fillRect (juce::Rectangle<float> (c.x - 0.75f, c.y - 5.0f * z, 1.5f, 10.0f * z));
            g.setFont (rk::displayFont ("1", 10.0f * z));
            g.drawText (juce::String (used[i] + 1), juce::Rectangle<float> (16.0f * z, 12.0f * z).withCentre (c.translated (0.0f, 11.0f * z)),
                        juce::Justification::centred, false);
        }
    }

    if (nUsed < 2)
    {
        const auto area = b.reduced (10.0f * z);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRect (area);

        const juce::String big = nUsed == 0 ? "NO SNAPSHOTS" : "1 SNAPSHOT";
        g.setColour (rk::yellow);
        g.setFont (rk::displayFont (big, 13.0f * z));
        g.drawText (big, area.withHeight (20.0f * z).withCentre ({ area.getCentreX(), area.getCentreY() - 12.0f * z }),
                    juce::Justification::centred, false);
        g.setColour (rk::ui (0.6f));
        g.setFont (rk::uiFont (11.0f * z));
        g.drawFittedText (nUsed == 0 ? "随机到喜欢的声音，按左边的灯键存起来"_u8 : "再存一个才能在它们之间插值"_u8,
                          area.withHeight (32.0f * z).withCentre ({ area.getCentreX(), area.getCentreY() + 12.0f * z }).toNearestInt(),
                          juce::Justification::centred, 2, 0.85f);
    }

    paintSpot (g, handle, (dragging ? 5.5f : 4.5f) * z, juce::Colours::white, nUsed >= 2 ? 1.0f : 0.35f);
}

} // namespace pr
