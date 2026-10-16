#include "RackTheme.h"

namespace pr::rk
{

namespace
{
    juce::Rectangle<float> circle (juce::Point<float> c, float r)
    {
        return { c.x - r, c.y - r, r * 2.0f, r * 2.0f };
    }

    /** Rack 的 `color::mult`：RGB 乘系数、alpha 不动（不是 HSB 的亮度）。 */
    juce::Colour mult (juce::Colour c, float k)
    {
        return juce::Colour::fromFloatRGBA (c.getFloatRed() * k, c.getFloatGreen() * k,
                                            c.getFloatBlue() * k, c.getFloatAlpha());
    }

    juce::String pickFamily (std::initializer_list<const char*> candidates, const juce::String& fallback)
    {
        const auto names = juce::Font::findAllTypefaceNames();
        for (const auto* c : candidates)
            if (names.contains (c))
                return c;
        return fallback;
    }

    // 浅色面板照着 Rack 2 Fundamental 的截图取样（面板 #E8E8E8、字 #1F1F1F、输出框 #1F1F1F）；
    // 深色面板照着 Fundamental 的 *-dark.svg（字 #EBEBEB，输出框反成浅色）。
    const PanelTheme lightPanel { juce::Colour (0xffe8e8e8), juce::Colour (0xffb6b6b6), juce::Colour (0xff252525),
                                  juce::Colour (0xff727272), juce::Colour (0xff1c1c1c), juce::Colour (0xff1f1f1f),
                                  juce::Colour (0xfff0f0f0), juce::Colour (0xffbdbdbd) };

    const PanelTheme darkPanel  { juce::Colour (0xff262626), juce::Colour (0xff090909), juce::Colour (0xffe6e6e6),
                                  juce::Colour (0xff8e8e8e), juce::Colour (0xfff2f2f2), juce::Colour (0xffd9d9d9),
                                  juce::Colour (0xff1f1f1f), juce::Colour (0xff474747) };

    /** 光晕在这块底色上能显出几成。Rack 用滤色混合：叠上去的量 = 光 × (1 − 底)，底越亮越看不见。
        JUCE 没有混合模式，就按底色亮度把光晕的不透明度压下去——效果一样，浅色面板上剩一点点。 */
    float glowOver (bool onDark)
    {
        const auto under = onDark ? juce::Colour (0xff121212) : shade (panel().bg);
        return juce::jmax (0.12f, 1.0f - under.getPerceivedBrightness());
    }
}

//==============================================================================
juce::Colour cableColour (int index)
{
    static const juce::Colour c[] = { juce::Colour (0xfff3374b), juce::Colour (0xffffb437), juce::Colour (0xff00b56e),
                                      juce::Colour (0xff3695ef), juce::Colour (0xff8b4ade) };
    return c[(size_t) (((index % 5) + 5) % 5)];
}

const PanelTheme& panel()
{
    return view().darkPanels ? darkPanel : lightPanel;
}

View& view()
{
    static View v;
    return v;
}

void loadView (const juce::File& f)
{
    if (! f.existsAsFile())
        return;

    auto& v = view();

    for (const auto& raw : juce::StringArray::fromLines (f.loadFileAsString()))
    {
        const auto key = raw.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto val = raw.fromFirstOccurrenceOf ("=", false, false).trim();

        if (key == "cableOpacity")   v.cableOpacity   = juce::jlimit (0.0f, 1.0f, val.getFloatValue());
        if (key == "cableTension")   v.cableTension   = juce::jlimit (0.0f, 1.0f, val.getFloatValue());
        if (key == "roomBrightness") v.roomBrightness = juce::jlimit (0.0f, 1.0f, val.getFloatValue());
        if (key == "halo")           v.halo           = juce::jlimit (0.0f, 1.0f, val.getFloatValue());
        if (key == "darkPanels")     v.darkPanels     = val.getIntValue() != 0;
    }
}

void saveView (const juce::File& f)
{
    if (f == juce::File())
        return;

    const auto& v = view();
    juce::String s;
    s << "cableOpacity="   << v.cableOpacity   << "\n"
      << "cableTension="   << v.cableTension   << "\n"
      << "roomBrightness=" << v.roomBrightness << "\n"
      << "halo="           << v.halo           << "\n"
      << "darkPanels="     << (v.darkPanels ? 1 : 0) << "\n";
    f.replaceWithText (s);
}

juce::Colour shade (juce::Colour c)
{
    const float b = juce::jlimit (0.0f, 1.0f, view().roomBrightness);
    if (b >= 0.999f)
        return c;

    // 最暗也留 10%：Rack 在全暗时能靠鼠标的"手电筒"找东西，这里没有手电筒
    return mult (c, 0.10f + 0.90f * b);
}

//==============================================================================
bool isAscii (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
        if (*p > 127)
            return false;
    return true;
}

juce::Font titleFont (float px)
{
    static const juce::String fam = pickFamily ({ "Century Gothic", "Bahnschrift", "Segoe UI" }, theme::family());
    const char* style = fam == "Bahnschrift" ? "SemiBold" : "Bold";
    return juce::Font (juce::FontOptions (fam, style, px).withPointHeight (px));
}

juce::Font labelFont (const juce::String& s, float px)
{
    return isAscii (s) ? titleFont (px) : theme::font (px * 1.04f, theme::Weight::bold);
}

juce::Font displayFont (const juce::String& s, float px)
{
    static const juce::String mono = pickFamily ({ "Cascadia Mono", "Consolas" }, theme::family());

    if (isAscii (s))
        return juce::Font (juce::FontOptions (mono, "Regular", px).withPointHeight (px));

    return theme::font (px, theme::Weight::medium);
}

//==============================================================================
void paintRails (juce::Graphics& g, juce::Rectangle<float> row, float z)
{
    g.setColour (shade (rackBg));
    g.fillRect (row);

    // Rail.svg 的七层（每条 15 px 高），上下各一条
    struct Band { float y, h; juce::uint32 argb; };
    static const Band bands[] = { { 0.0f,  14.93f, 0xffdedede }, { 1.65f, 13.28f, 0xff9d9fa2 },
                                  { 1.20f, 12.53f, 0xffc9c9c9 }, { 2.85f, 9.23f,  0xff7e8082 },
                                  { 4.13f, 7.95f,  0xffdedede }, { 3.83f, 7.28f,  0xffadadad },
                                  { 3.83f, 1.88f,  0x26000000 } };

    for (const float top : { row.getY(), row.getBottom() - 15.0f * z })
    {
        for (const auto& b : bands)
        {
            g.setColour (shade (juce::Colour (b.argb)));
            g.fillRect (juce::Rectangle<float> (row.getX(), top + b.y * z, row.getWidth(), b.h * z));
        }

        // 安装孔：每 HP 一个（只画在可见的那一段里）
        g.setColour (shade (rackBg));
        const auto clip = g.getClipBounds().toFloat();
        const float step = 15.0f * z;
        float x = row.getX() + 7.5f * z;
        if (clip.getX() > x)
            x += std::floor ((clip.getX() - x) / step) * step;

        for (; x < juce::jmin (row.getRight(), clip.getRight() + step); x += step)
            g.fillEllipse (circle ({ x, top + 7.7f * z }, 2.6f * z));
    }
}

void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, float z)
{
    const auto& p = panel();
    g.setColour (shade (p.bg));
    g.fillRect (r);

    g.setColour (shade (p.edge));
    g.drawRect (r, juce::jmax (1.0f, z));
}

void paintScrew (juce::Graphics& g, juce::Point<float> c, float z)
{
    // ScrewSilver.svg：外圈 #ABA9A9→#8F8F8F、里圈 #F5F5F5→#C2C2C2、十字槽
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xffaba9a9)), c.x, c.y - 6.5f * z,
                                             shade (juce::Colour (0xff8f8f8f)), c.x, c.y + 6.5f * z, false));
    g.fillEllipse (circle (c, 6.5f * z));
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xfff5f5f5)), c.x, c.y - 5.8f * z,
                                             shade (juce::Colour (0xffc2c2c2)), c.x, c.y + 5.8f * z, false));
    g.fillEllipse (circle (c, 5.8f * z));

    g.setColour (shade (juce::Colour (0xff8c8c8c)));
    g.fillRect (juce::Rectangle<float> (c.x - 3.6f * z, c.y - 0.55f * z, 7.2f * z, 1.1f * z));
    g.fillRect (juce::Rectangle<float> (c.x - 0.55f * z, c.y - 3.6f * z, 1.1f * z, 7.2f * z));
}

void paintScrews (juce::Graphics& g, juce::Rectangle<float> r, float z)
{
    const float top = r.getY() + 7.5f * z, bottom = r.getBottom() - 7.5f * z;

    // 和 Rack 一样：宽的模块四颗（距左右 1.5 HP），窄的上下居中各一颗
    if (r.getWidth() >= 89.0f * z)
    {
        for (const float x : { r.getX() + 22.5f * z, r.getRight() - 22.5f * z })
        {
            paintScrew (g, { x, top }, z);
            paintScrew (g, { x, bottom }, z);
        }
    }
    else
    {
        paintScrew (g, { r.getCentreX(), top }, z);
        paintScrew (g, { r.getCentreX(), bottom }, z);
    }
}

void paintTitle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float z)
{
    g.setColour (shade (panel().title));
    g.setFont (isAscii (t) ? titleFont (15.0f * z) : theme::font (14.0f * z, theme::Weight::bold));
    g.drawFittedText (t, juce::Rectangle<float> (r.getX() + 4.0f * z, r.getY() + 15.0f * z, r.getWidth() - 8.0f * z, 22.0f * z)
                             .toNearestInt(),
                      juce::Justification::centred, 1, 0.75f);
}

void paintLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> area, float z,
                 float px, juce::Justification j, bool onBox, bool dim)
{
    const auto& p = panel();
    g.setColour (shade (onBox ? p.outText : (dim ? p.textDim : p.text)));
    g.setFont (labelFont (t, px * z));
    g.drawFittedText (t, area.toNearestInt(), j, 1, 0.75f);
}

void paintOutputBox (juce::Graphics& g, juce::Rectangle<float> r, float z)
{
    g.setColour (shade (panel().outBox));
    g.fillRoundedRectangle (r, 3.0f * z);
}

void paintBrand (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& brand, float z)
{
    // Rack 的模块底下印着厂牌 logo（Fundamental 的是那个粗体 VCV）。
    const auto area = juce::Rectangle<float> (r.getX() + 6.0f * z, r.getBottom() - 30.0f * z, r.getWidth() - 12.0f * z, 14.0f * z);
    g.setColour (shade (panel().title));
    g.setFont ((isAscii (brand) ? titleFont (10.5f * z) : theme::font (10.0f * z, theme::Weight::bold))
                   .withExtraKerningFactor (0.14f));
    g.drawFittedText (brand, area.toNearestInt(), juce::Justification::centred, 1, 0.6f);
}

void paintRule (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float z)
{
    g.setColour (shade (panel().line));
    g.drawLine ({ a, b }, juce::jmax (1.0f, z));
}

//==============================================================================
void paintJack (juce::Graphics& g, juce::Point<float> c, float z)
{
    // PJ301M：亮的螺母（上亮下暗）→ 反向渐变的斜面 → 深色套筒 → 黑洞
    const float r = 11.85f * z;

    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xfff0f0f0)), c.x, c.y - r,
                                             shade (juce::Colour (0xff545454)), c.x, c.y + r, false));
    g.fillEllipse (circle (c, r));
    g.setColour (shade (juce::Colour (0xff2e2e2e)).withAlpha (0.5f));
    g.drawEllipse (circle (c, r - 0.4f * z), 0.8f * z);

    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xff5c5c5c)), c.x, c.y - r * 0.8f,
                                             shade (juce::Colour (0xffdcdcdc)), c.x, c.y + r * 0.8f, false));
    g.fillEllipse (circle (c, r * 0.8f));

    g.setColour (shade (juce::Colour (0xff3a3a3a)));
    g.fillEllipse (circle (c, r * 0.6f));
    g.setColour (shade (juce::Colour (0xff0f0f0f)));
    g.fillEllipse (circle (c, r * 0.44f));
}

float knobDiameter (Knob k)
{
    switch (k)
    {
        case Knob::roundSmall:  return 24.0f;
        case Knob::round:       return 30.0f;
        case Knob::roundLarge:  return 38.0f;
        case Knob::roundHuge:   return 56.0f;
        case Knob::daviesWhite:
        case Knob::daviesRed:   return 36.0f;
        case Knob::trimpot:     return 18.0f;
    }
    return 30.0f;
}

float knobMaxAngle (Knob k)
{
    return (k == Knob::trimpot ? 0.75f : 0.83f) * juce::MathConstants<float>::pi;
}

void paintKnob (juce::Graphics& g, juce::Point<float> c, float D, float angle, Knob k, bool enabled, bool hover)
{
    juce::ignoreUnused (hover);     // Rack 的旋钮没有悬停反馈；数值靠提示气泡

    const float R  = D * 0.5f;
    const float pi = juce::MathConstants<float>::pi;
    const auto  rot = juce::AffineTransform::rotation (angle).translated (c.x, c.y);

    // CircularShadow：往下偏一点的柔边影子
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.38f), c.x, c.y + R * 0.18f,
                                             juce::Colours::transparentBlack, c.x + R * 1.3f, c.y + R * 0.18f, true));
    g.fillEllipse (circle (c.translated (0.0f, R * 0.18f), R * 1.3f));

    auto pointer = [&] (float from, float to, float width, juce::Colour col)
    {
        juce::Path p;
        p.addRoundedRectangle (-width * 0.5f, -R * to, width, R * (to - from), width * 0.5f);
        g.setColour (col);
        g.fillPath (p, rot);
    };

    switch (k)
    {
        case Knob::roundSmall:
        case Knob::round:
        case Knob::roundLarge:
        case Knob::roundHuge:
        {
            // RoundBlackKnob_bg.svg：外圈 #B0ACAE→黑 的竖向渐变 + 左上一块白、右下一块淡白的固定反光
            g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xffb0acae)), c.x, c.y - R,
                                                     shade (juce::Colours::black), c.x, c.y + R, false));
            g.fillEllipse (circle (c, R));

            juce::Path sheen, sheen2;
            sheen.addPieSegment (circle (c, R), -0.42f * pi, -0.12f * pi, 0.0f);
            sheen2.addPieSegment (circle (c, R), 0.58f * pi, 0.88f * pi, 0.0f);
            g.setColour (shade (juce::Colours::white).withAlpha (0.55f));
            g.fillPath (sheen);
            g.setColour (shade (juce::Colours::white).withAlpha (0.16f));
            g.fillPath (sheen2);

            // 盖子（82% 不透明，所以反光隐约透出来）+ 指示线
            g.setColour (shade (juce::Colour (0xff1e1d1e)).withAlpha (0.82f));
            g.fillEllipse (circle (c, R * 0.912f));
            g.setColour (shade (juce::Colour (0xff232223)));
            g.fillEllipse (circle (c, R * 0.80f));

            pointer (0.0f, 0.79f, juce::jmax (1.6f, D * 0.0625f), shade (juce::Colour (0xffededed)));
            break;
        }

        case Knob::daviesWhite:
        case Knob::daviesRed:
        {
            const bool isRed = k == Knob::daviesRed;
            const auto skirtTop = isRed ? juce::Colour (0xffdb0000) : juce::Colour (0xffc4c4c4);
            const auto skirtBot = isRed ? juce::Colour (0xff750000) : juce::Colour (0xff8a8a8a);
            const auto capTop   = isRed ? juce::Colour (0xfff04545) : juce::Colours::white;
            const auto capBot   = isRed ? juce::Colour (0xffc81e1e) : juce::Colour (0xffe2e2e2);
            const auto ink      = isRed ? juce::Colour (0xffe3e3e3) : juce::Colour (0xff383838);

            g.setGradientFill (juce::ColourGradient (shade (skirtTop), c.x, c.y - R, shade (skirtBot), c.x, c.y + R, false));
            g.fillEllipse (circle (c, R));

            // 裙边的防滑槽（对称的，不转也看不出来，转起来反而闪）
            g.setColour (shade (juce::Colours::black).withAlpha (0.18f));
            for (int i = 0; i < 28; ++i)
            {
                const float a = (float) i * 2.0f * pi / 28.0f;
                g.drawLine ({ c.getPointOnCircumference (R * 0.80f, a), c.getPointOnCircumference (R * 0.98f, a) },
                            juce::jmax (0.8f, D * 0.025f));
            }

            g.setGradientFill (juce::ColourGradient (shade (capTop), c.x, c.y - R * 0.74f, shade (capBot), c.x, c.y + R * 0.74f, false));
            g.fillEllipse (circle (c, R * 0.74f));
            g.setColour (shade (skirtBot).withAlpha (0.6f));
            g.drawEllipse (circle (c, R * 0.74f), juce::jmax (0.6f, D * 0.02f));

            pointer (0.12f, 0.97f, juce::jmax (1.6f, D * 0.07f), shade (ink));
            break;
        }

        case Knob::trimpot:
        {
            g.setColour (shade (juce::Colour (0xff0a0a0a)));
            g.fillEllipse (circle (c, R));
            g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xfff0f0f0)), c.x, c.y - R * 0.8f,
                                                     shade (juce::Colour (0xffb4b4b4)), c.x, c.y + R * 0.8f, false));
            g.fillEllipse (circle (c, R * 0.8f));
            pointer (0.0f, 0.8f, juce::jmax (1.4f, D * 0.11f), shade (juce::Colour (0xff2a2a2a)));
            break;
        }
    }

    if (! enabled)
    {
        g.setColour (shade (panel().bg).withAlpha (0.55f));
        g.fillEllipse (circle (c, R + 1.0f));
    }
}

void paintLight (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float b, bool onDark)
{
    b = juce::jlimit (0.0f, 1.0f, b);

    // 光晕（Rack 的 drawHalo：半径 + min (4r, 15)）。灯是发光体，不过 shade()——但浓淡看底色
    if (b > 0.01f && view().halo > 0.0f)
    {
        const float o = r + juce::jmin (r * 4.0f, r * 2.6f + 6.0f);
        const float a = juce::jmin (1.0f, view().halo * 1.5f * b * glowOver (onDark));
        g.setGradientFill (juce::ColourGradient (col.withAlpha (a), c.x, c.y,
                                                 col.withAlpha (0.0f), c.x + o, c.y, true));
        g.fillEllipse (circle (c, o));
    }

    g.setColour (shade (juce::Colour (0xff333333)));
    g.fillEllipse (circle (c, r));

    if (b > 0.004f)
    {
        g.setColour (col.withAlpha (b));
        g.fillEllipse (circle (c, r));
        g.setColour (juce::Colours::white.withAlpha (0.5f * b * b));
        g.fillEllipse (circle (c, r * 0.42f));
    }

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.drawEllipse (circle (c, r), juce::jmax (0.5f, r * 0.12f));
}

void paintBezel (juce::Graphics& g, juce::Point<float> c, float D, juce::Colour light, float b, bool pressed, bool hover)
{
    const float R = D * 0.5f;
    b = juce::jlimit (0.0f, 1.0f, b);

    if (b > 0.01f && view().halo > 0.0f)
    {
        const float a = juce::jmin (1.0f, view().halo * 1.4f * b * glowOver (false));
        g.setGradientFill (juce::ColourGradient (light.withAlpha (a), c.x, c.y,
                                                 light.withAlpha (0.0f), c.x + R * 2.1f, c.y, true));
        g.fillEllipse (circle (c, R * 2.1f));
    }

    // VCVBezel.svg：#787878→#474747 的金属圈
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (circle (c.translated (0.0f, R * 0.08f), R * 1.04f));
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (hover ? 0xff8a8a8a : 0xff787878)), c.x, c.y - R,
                                             shade (juce::Colour (0xff474747)), c.x, c.y + R, false));
    g.fillEllipse (circle (c, R));

    const auto inner = circle (c.translated (0.0f, pressed ? R * 0.04f : 0.0f), R * 0.80f);
    g.setColour (shade (juce::Colour (0xff1c1c1c)));
    g.fillEllipse (inner);

    if (b > 0.004f)
    {
        const auto ic = inner.getCentre();
        g.setGradientFill (juce::ColourGradient (light.brighter (0.35f).withAlpha (b), ic.x, ic.y,
                                                 light.withAlpha (0.75f * b), ic.x + inner.getWidth() * 0.5f, ic.y, true));
        g.fillEllipse (inner);
    }

    // 透明盖子的反光（21% 白）
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (pressed ? 0.08f : 0.21f), c.x, inner.getY(),
                                             juce::Colours::white.withAlpha (0.0f), c.x, inner.getCentreY(), false));
    g.fillEllipse (inner.reduced (inner.getWidth() * 0.08f));
}

void paintTL1105 (juce::Graphics& g, juce::Point<float> c, float D, bool pressed, bool hover, bool enabled)
{
    const float R = D * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (circle (c.translated (0.0f, R * 0.1f), R * 1.05f));
    g.setColour (shade (juce::Colour (0xff121212)));
    g.fillEllipse (circle (c, R));

    if (pressed)
    {
        g.setColour (shade (juce::Colour (0xff2b2b2b)));
        g.fillEllipse (circle (c, R * 0.86f));
    }
    else
    {
        // TL1105_0.svg：#8D8B8C→#0D0C0C
        g.setGradientFill (juce::ColourGradient (shade (juce::Colour (hover && enabled ? 0xffa5a3a4 : 0xff8d8b8c)), c.x, c.y - R,
                                                 shade (juce::Colour (0xff0d0c0c)), c.x, c.y + R * 0.6f, false));
        g.fillEllipse (circle (c, R * 0.86f));
    }

    if (! enabled)
    {
        g.setColour (shade (panel().bg).withAlpha (0.5f));
        g.fillEllipse (circle (c, R + 0.5f));
    }
}

void paintSwitch (juce::Graphics& g, juce::Rectangle<float> r, int positions, int position, bool enabled, bool hover)
{
    positions = juce::jmax (2, positions);
    position  = juce::jlimit (0, positions - 1, position);

    // CKSS.svg：#5E5E5E 的外壳、#3B3B3B→#242424 的槽、浅灰的拨块
    const float rad = r.getWidth() * 0.1f;
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (r.translated (0.0f, r.getWidth() * 0.06f), rad);
    g.setColour (shade (juce::Colour (0xff5e5e5e)));
    g.fillRoundedRectangle (r, rad);

    const auto slot = r.reduced (r.getWidth() * 0.12f);
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xff3b3b3b)), slot.getX(), slot.getY(),
                                             shade (juce::Colour (0xff242424)), slot.getX(), slot.getBottom(), false));
    g.fillRoundedRectangle (slot, rad * 0.6f);

    const float h = slot.getHeight() / (float) positions;
    const auto lever = juce::Rectangle<float> (slot.getX(), slot.getY() + h * (float) position, slot.getWidth(), h).reduced (0.6f);
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (hover && enabled ? 0xfff6f6f6 : 0xffe6e6e6)), lever.getX(), lever.getY(),
                                             shade (juce::Colour (0xff9a9a9a)), lever.getX(), lever.getBottom(), false));
    g.fillRoundedRectangle (lever, rad * 0.5f);

    g.setColour (shade (juce::Colour (0xff6a6a6a)));
    for (int i = 1; i <= 3; ++i)
    {
        const float y = lever.getY() + lever.getHeight() * (float) i / 4.0f;
        g.drawLine (lever.getX() + lever.getWidth() * 0.2f, y, lever.getRight() - lever.getWidth() * 0.2f, y, 0.7f);
    }

    if (! enabled)
    {
        g.setColour (shade (panel().bg).withAlpha (0.5f));
        g.fillRoundedRectangle (r.expanded (0.5f), rad);
    }
}

void paintDisplay (juce::Graphics& g, juce::Rectangle<float> r, float z)
{
    // LedDisplay.cpp：#222→#121212（只渐变最上面 25 px）、外沿一圈暗线、下沿一道亮线
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222222), 0.0f, r.getY(),
                                             juce::Colour (0xff121212), 0.0f, r.getY() + 25.0f * z, false));
    g.fillRect (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRect (r, juce::jmax (1.0f, z));
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom(), r.getWidth(), juce::jmax (1.0f, z)));
}

void paintLadder (juce::Graphics& g, juce::Rectangle<float> col, int n, float level, float z, bool horizontal)
{
    juce::ignoreUnused (z);

    // 门限：最上一颗 0 dB，往下越来越稀（-48 dB 起亮）
    const float db = 20.0f * std::log10 (juce::jmax (1.0e-6f, level));

    for (int i = 0; i < n; ++i)
    {
        const float t  = (float) i / (float) juce::jmax (1, n - 1);              // 0 = 最底下
        const float th = -48.0f * std::pow (1.0f - t, 1.6f);
        const float b  = juce::jlimit (0.0f, 1.0f, (db - th) / 3.0f + 1.0f);
        const auto  c  = i == n - 1 ? red : (t >= 0.72f ? yellow : green);

        const float cell = (horizontal ? col.getWidth() : col.getHeight()) / (float) n;
        const auto  p = horizontal ? juce::Point<float> (col.getX() + cell * ((float) i + 0.5f), col.getCentreY())
                                   : juce::Point<float> (col.getCentreX(), col.getBottom() - cell * ((float) i + 0.5f));

        paintLight (g, p, juce::jmin (cell * 0.36f, (horizontal ? col.getHeight() : col.getWidth()) * 0.4f), c, b);
    }
}

//==============================================================================
juce::Point<float> slumpPoint (juce::Point<float> out, juce::Point<float> in, float z)
{
    const float dist = juce::jmin (320.0f, out.getDistanceFrom (in) / juce::jmax (0.01f, z));
    auto avg = (out + in) * 0.5f;
    avg.y += (1.0f - view().cableTension) * (150.0f + dist) * z;
    return avg;
}

void paintCable (juce::Graphics& g, juce::Point<float> out, juce::Point<float> in, juce::Colour col,
                 float z, float opacity, bool thick)
{
    const float opa = opacity < 0.0f ? view().cableOpacity : opacity;
    if (opa <= 0.0f)
        return;

    const float alpha = std::pow (opa, 1.5f);
    const auto  slump = slumpPoint (out, in, z);

    // 端点从插孔中心往坠点方向让 14 px：线是从插头的"尾巴"里出来的
    auto offset = [z, slump] (juce::Point<float> p)
    {
        const auto d = slump - p;
        const float len = d.getDistanceFromOrigin();
        return len > 0.001f ? p + d * (14.0f * z / len) : p;
    };

    const auto a = offset (out), b = offset (in);
    const float th = (thick ? 9.0f : 6.0f) * z;

    juce::Path shadowPath, path;
    shadowPath.startNewSubPath (a);
    shadowPath.quadraticTo (slump.translated (0.0f, 30.0f * z), b);
    path.startNewSubPath (a);
    path.quadraticTo (slump, b);

    const juce::PathStrokeType::JointStyle j = juce::PathStrokeType::curved;
    const juce::PathStrokeType::EndCapStyle e = juce::PathStrokeType::rounded;

    g.setColour (juce::Colours::black.withAlpha (0.10f * alpha));
    g.strokePath (shadowPath, juce::PathStrokeType (th - z, j, e));
    g.setColour (shade (mult (col, 0.8f)).withAlpha (alpha));
    g.strokePath (path, juce::PathStrokeType (th, j, e));
    g.setColour (shade (mult (col, 0.95f)).withAlpha (alpha));
    g.strokePath (path, juce::PathStrokeType (th - z, j, e));
}

void paintPlug (juce::Graphics& g, juce::Point<float> pos, juce::Point<float> towards, juce::Colour col,
                float z, float signal)
{
    auto dir = towards - pos;
    const float len = dir.getDistanceFromOrigin();
    dir = len > 0.001f ? dir / len : juce::Point<float> (0.0f, 1.0f);
    const juce::Point<float> side (-dir.y, dir.x);

    // 插头的塑料头：圆身 + 朝线出去方向的一截"尾巴"（Plug.svg 的形状），染成线的颜色
    const float R = 10.8f * z;
    juce::Path body;
    body.addEllipse (circle (pos, R));
    juce::Path tail;
    tail.startNewSubPath (pos + side * (R * 0.78f));
    tail.lineTo (pos + dir * (R * 1.45f) + side * (R * 0.42f));
    tail.lineTo (pos + dir * (R * 1.45f) - side * (R * 0.42f));
    tail.lineTo (pos - side * (R * 0.78f));
    tail.closeSubPath();
    body.addPath (tail);

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (body, juce::AffineTransform::translation (0.0f, 1.6f * z));
    g.setColour (shade (col));
    g.fillPath (body);

    // 20% 黑的阴影面（光从上面来，下半边暗）
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), pos.x, pos.y - R,
                                             juce::Colours::black.withAlpha (0.22f), pos.x, pos.y + R, false));
    g.fillPath (body);
    g.setColour (shade (mult (col, 0.6f)));
    g.strokePath (body, juce::PathStrokeType (juce::jmax (0.8f, z)));

    // 金属插孔（PlugPort.svg）+ 信号灯
    const float pr = 7.9f * z;
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xfff2f2f2)), pos.x, pos.y - pr,
                                             shade (juce::Colour (0xff5a5a5a)), pos.x, pos.y + pr, false));
    g.fillEllipse (circle (pos, pr));
    g.setColour (shade (juce::Colour (0xff161616)));
    g.fillEllipse (circle (pos, pr * 0.58f));

    paintLight (g, pos, 2.4f * z, green, signal, true);
}

//==============================================================================
void paintSelection (juce::Graphics& g, juce::Rectangle<float> r, float z)
{
    // Rack 2 的选区：半透明红 + 红边（RackWidget.cpp 里 0.25 / 0.5）。这里底色淡一点，字要能读
    g.setColour (juce::Colour (0xffff0000).withAlpha (0.13f));
    g.fillRect (r);
    g.setColour (juce::Colour (0xffff0000).withAlpha (0.6f));
    g.drawRect (r, juce::jmax (1.5f, 1.8f * z));
}

float levelToBrightness (float linear)
{
    const float db = 20.0f * std::log10 (juce::jmax (1.0e-6f, linear));
    return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
}

} // namespace pr::rk
