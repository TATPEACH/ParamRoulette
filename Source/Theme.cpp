#include "Theme.h"
#include "PluginScanner.h"   // FnTag
#include "MotionEngine.h"    // MotionSemantic

namespace pr::theme
{

//==============================================================================
const juce::String& family()
{
    // 枚举系统字体有点慢，只查一次。
    static const juce::String f = []
    {
        const auto names = juce::Font::findAllTypefaceNames();

        for (const auto* candidate : { "Noto Sans SC", "Microsoft YaHei UI" })
            if (names.contains (candidate))
                return juce::String (candidate);

        return juce::Font::getDefaultSansSerifFontName();
    }();

    return f;
}

juce::Font font (float pointHeight, Weight w)
{
    // Noto Sans SC 有 Medium；YaHei 只有 Light/Regular/Bold，Medium 退成 Regular
    // （直接用 Bold 太重，小字号会糊成一团）。
    const bool noto = family().startsWith ("Noto");

    const char* style = "Regular";
    if (w == Weight::bold)                style = "Bold";
    else if (w == Weight::medium && noto) style = "Medium";

    return juce::Font (juce::FontOptions (family(), style, pointHeight).withPointHeight (pointHeight));
}

//==============================================================================
juce::Colour tagColour (FnTag t)
{
    // Grid 的惯例尽量保留：紫色 = 相位、红色 = 音频/失真、黄色 = 逻辑/调制。
    switch (t)
    {
        case FnTag::EQ:         return blue;
        case FnTag::Filter:     return cyan;
        case FnTag::Allpass:    return lavender;
        case FnTag::Phase:      return purple;
        case FnTag::Delay:      return green;
        case FnTag::Reverb:     return teal;
        case FnTag::Modulation: return yellow;
        case FnTag::Dynamics:   return orange;
        case FnTag::Distortion: return red;
        case FnTag::Pitch:      return lime;
        case FnTag::Granular:   return pink;
        case FnTag::Utility:    return juce::Colour (0xffd0d0d0);
        case FnTag::Unknown:    break;
    }

    return juce::Colour (0xffb4b4b4);
}

juce::Colour slotColour (int slot)
{
    static const juce::Colour c[] = { blue, green, yellow, purple };
    return c[(size_t) juce::jlimit (0, 3, slot)];
}

//==============================================================================
void fillDotGrid (juce::Graphics& g, juce::Rectangle<int> area, int spacing)
{
    const auto clip = g.getClipBounds().getIntersection (area);
    if (clip.isEmpty() || spacing <= 2)
        return;

    g.setColour (canvasDot);

    // 点阵对齐到 area 的原点，而不是 clip 的原点——否则局部重画时点会错位。
    const int x0 = area.getX() + ((clip.getX() - area.getX() + spacing - 1) / spacing) * spacing;
    const int y0 = area.getY() + ((clip.getY() - area.getY() + spacing - 1) / spacing) * spacing;

    for (int y = y0; y < clip.getBottom(); y += spacing)
        for (int x = x0; x < clip.getRight(); x += spacing)
            g.fillEllipse ((float) x - 1.0f, (float) y - 1.0f, 2.0f, 2.0f);
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill, bool shadow)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);

    if (shadow)
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 18, { 0, 5 }).drawForPath (g, p);

    g.setColour (fill);
    g.fillPath (p);

    // 顶边一道很淡的高光：让卡片"浮"起来，又不至于像 Win95 那种立体边框。
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

juce::Rectangle<int> drawCardTitle (juce::Graphics& g, juce::Rectangle<int> row,
                                    const juce::String& title, juce::Colour colour)
{
    auto r = row;
    auto dot = r.removeFromLeft (14).toFloat();
    drawPort (g, dot.getCentre(), colour, 4.0f);
    r.removeFromLeft (6);

    g.setColour (colour);
    g.setFont (font (13.0f, Weight::bold));
    const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 2;
    g.drawText (title, r.removeFromLeft (w), juce::Justification::centredLeft, false);

    return r;
}

void drawPort (juce::Graphics& g, juce::Point<float> c, juce::Colour colour, float radius)
{
    const auto box = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    g.setColour (well);
    g.fillEllipse (box);
    g.setColour (colour);
    g.drawEllipse (box, 1.8f);
}

void drawCable (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to,
                juce::Colour colour, float sag, float thickness)
{
    const float dx = juce::jmax (10.0f, std::abs (to.x - from.x) * 0.45f);

    juce::Path p;
    p.startNewSubPath (from);
    p.cubicTo (from.x + dx, from.y + sag, to.x - dx, to.y + sag, to.x, to.y);

    const juce::PathStrokeType outline (thickness + 2.2f, juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded);
    const juce::PathStrokeType core (thickness, juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (p, outline);
    g.setColour (colour);
    g.strokePath (p, core);
}

//==============================================================================
namespace
{
    /** 单位正方形 [0,1]² 里的路径 → 目标矩形。图标都在单位坐标里画。 */
    juce::AffineTransform unitTo (juce::Rectangle<float> r)
    {
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        const auto  b = r.withSizeKeepingCentre (s, s);
        return juce::AffineTransform::scale (s).translated (b.getX(), b.getY());
    }

    void strokeUnit (juce::Graphics& g, const juce::Path& p, juce::Rectangle<float> r,
                     juce::Colour c, float w)
    {
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded),
                      unitTo (r));
    }

    void fillUnit (juce::Graphics& g, const juce::Path& p, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        g.fillPath (p, unitTo (r));
    }
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c, float sw)
{
    // 线宽按**目标像素**给：strokePath 带 transform 时是先变换点、再按线宽描边，
    // 所以单位坐标下画、线宽不用跟着缩放。
    juce::Path p;

    switch (icon)
    {
        case Icon::dice:
        {
            p.addRoundedRectangle (0.12f, 0.12f, 0.76f, 0.76f, 0.2f);
            strokeUnit (g, p, r, c, sw);
            juce::Path pips;
            for (auto pt : { juce::Point<float> { 0.34f, 0.34f }, { 0.5f, 0.5f }, { 0.66f, 0.66f } })
                pips.addEllipse (pt.x - 0.065f, pt.y - 0.065f, 0.13f, 0.13f);
            fillUnit (g, pips, r, c);
            return;
        }

        case Icon::prev:
            p.startNewSubPath (0.62f, 0.2f); p.lineTo (0.32f, 0.5f); p.lineTo (0.62f, 0.8f);
            break;

        case Icon::next:
            p.startNewSubPath (0.38f, 0.2f); p.lineTo (0.68f, 0.5f); p.lineTo (0.38f, 0.8f);
            break;

        case Icon::chevronDown:
            p.startNewSubPath (0.22f, 0.38f); p.lineTo (0.5f, 0.64f); p.lineTo (0.78f, 0.38f);
            break;

        case Icon::chevronRight:
            p.startNewSubPath (0.4f, 0.22f); p.lineTo (0.66f, 0.5f); p.lineTo (0.4f, 0.78f);
            break;

        case Icon::power:
        {
            // Bitwig 设备面板左上角那个电源键
            p.addCentredArc (0.5f, 0.54f, 0.32f, 0.32f, 0.0f,
                             juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
            p.startNewSubPath (0.5f, 0.12f); p.lineTo (0.5f, 0.48f);
            break;
        }

        case Icon::plus:
            p.startNewSubPath (0.5f, 0.18f); p.lineTo (0.5f, 0.82f);
            p.startNewSubPath (0.18f, 0.5f); p.lineTo (0.82f, 0.5f);
            break;

        case Icon::close:
            p.startNewSubPath (0.24f, 0.24f); p.lineTo (0.76f, 0.76f);
            p.startNewSubPath (0.76f, 0.24f); p.lineTo (0.24f, 0.76f);
            break;

        case Icon::window:
            p.addRoundedRectangle (0.12f, 0.18f, 0.76f, 0.64f, 0.12f);
            p.startNewSubPath (0.12f, 0.36f); p.lineTo (0.88f, 0.36f);
            break;

        case Icon::eject:
        {
            juce::Path tri;
            tri.addTriangle (0.5f, 0.16f, 0.84f, 0.56f, 0.16f, 0.56f);
            strokeUnit (g, tri, r, c, sw);
            p.startNewSubPath (0.18f, 0.78f); p.lineTo (0.82f, 0.78f);
            break;
        }

        case Icon::search:
            p.addEllipse (0.14f, 0.14f, 0.5f, 0.5f);
            p.startNewSubPath (0.58f, 0.58f); p.lineTo (0.86f, 0.86f);
            break;

        case Icon::scan:
            // 两段带箭头的圆弧 = 刷新 / 重新扫描
            p.addCentredArc (0.5f, 0.5f, 0.32f, 0.32f, 0.0f,
                             juce::degreesToRadians (20.0f), juce::degreesToRadians (160.0f), true);
            p.addCentredArc (0.5f, 0.5f, 0.32f, 0.32f, 0.0f,
                             juce::degreesToRadians (200.0f), juce::degreesToRadians (340.0f), true);
            p.startNewSubPath (0.86f, 0.2f); p.lineTo (0.81f, 0.4f); p.lineTo (0.62f, 0.33f);
            p.startNewSubPath (0.14f, 0.8f); p.lineTo (0.19f, 0.6f); p.lineTo (0.38f, 0.67f);
            break;

        case Icon::tag:
            p.startNewSubPath (0.14f, 0.2f); p.lineTo (0.52f, 0.2f); p.lineTo (0.86f, 0.54f);
            p.lineTo (0.54f, 0.86f); p.lineTo (0.14f, 0.48f); p.closeSubPath();
            p.addEllipse (0.29f, 0.32f, 0.1f, 0.1f);
            break;

        case Icon::save:
            p.startNewSubPath (0.5f, 0.14f); p.lineTo (0.5f, 0.6f);
            p.startNewSubPath (0.3f, 0.42f); p.lineTo (0.5f, 0.62f); p.lineTo (0.7f, 0.42f);
            p.startNewSubPath (0.16f, 0.66f); p.lineTo (0.16f, 0.84f); p.lineTo (0.84f, 0.84f); p.lineTo (0.84f, 0.66f);
            break;

        case Icon::open:
            p.startNewSubPath (0.12f, 0.8f); p.lineTo (0.12f, 0.22f); p.lineTo (0.38f, 0.22f);
            p.lineTo (0.46f, 0.32f); p.lineTo (0.86f, 0.32f); p.lineTo (0.86f, 0.8f); p.closeSubPath();
            break;

        case Icon::probe:
            // 心电图一样的脉冲线 = 体检
            p.startNewSubPath (0.08f, 0.56f); p.lineTo (0.3f, 0.56f); p.lineTo (0.4f, 0.24f);
            p.lineTo (0.56f, 0.8f); p.lineTo (0.66f, 0.48f); p.lineTo (0.92f, 0.48f);
            break;

        case Icon::pin:
            // 锚 = 设为基座
            p.addEllipse (0.4f, 0.1f, 0.2f, 0.2f);
            p.startNewSubPath (0.5f, 0.3f); p.lineTo (0.5f, 0.88f);
            p.startNewSubPath (0.32f, 0.44f); p.lineTo (0.68f, 0.44f);
            p.addCentredArc (0.5f, 0.6f, 0.3f, 0.28f, 0.0f,
                             juce::degreesToRadians (100.0f), juce::degreesToRadians (260.0f), true);
            break;

        case Icon::restore:
            p.addCentredArc (0.52f, 0.52f, 0.32f, 0.32f, 0.0f,
                             juce::degreesToRadians (-60.0f), juce::degreesToRadians (225.0f), true);
            p.startNewSubPath (0.14f, 0.16f); p.lineTo (0.2f, 0.38f); p.lineTo (0.42f, 0.32f);
            break;

        case Icon::more:
        {
            juce::Path dots;
            for (float x : { 0.22f, 0.5f, 0.78f })
                dots.addEllipse (x - 0.075f, 0.425f, 0.15f, 0.15f);
            fillUnit (g, dots, r, c);
            return;
        }

        case Icon::check:
            p.startNewSubPath (0.2f, 0.52f); p.lineTo (0.42f, 0.73f); p.lineTo (0.8f, 0.3f);
            break;

        case Icon::warning:
        {
            p.startNewSubPath (0.5f, 0.12f); p.lineTo (0.9f, 0.84f); p.lineTo (0.1f, 0.84f); p.closeSubPath();
            p.startNewSubPath (0.5f, 0.38f); p.lineTo (0.5f, 0.6f);
            strokeUnit (g, p, r, c, sw);
            juce::Path dot;
            dot.addEllipse (0.455f, 0.68f, 0.09f, 0.09f);
            fillUnit (g, dot, r, c);
            return;
        }

        case Icon::link:
            p.addRoundedRectangle (0.08f, 0.36f, 0.48f, 0.28f, 0.14f);
            p.addRoundedRectangle (0.44f, 0.36f, 0.48f, 0.28f, 0.14f);
            break;

        case Icon::knob:
        {
            // 旋钮 = 切换外观（Grid ↔ 硬件）。一圈刻度点 + 带指针的圆
            p.addEllipse (0.26f, 0.26f, 0.48f, 0.48f);
            p.startNewSubPath (0.5f, 0.5f); p.lineTo (0.66f, 0.34f);
            strokeUnit (g, p, r, c, sw);

            juce::Path ticks;
            for (float deg : { -135.0f, -67.5f, 0.0f, 67.5f, 135.0f })
            {
                const auto pt = juce::Point<float> (0.5f, 0.5f).getPointOnCircumference (0.42f, juce::degreesToRadians (deg));
                ticks.addEllipse (pt.x - 0.045f, pt.y - 0.045f, 0.09f, 0.09f);
            }
            fillUnit (g, ticks, r, c);
            return;
        }

        case Icon::none:
            return;
    }

    strokeUnit (g, p, r, c, sw);
}

void drawSemanticGlyph (juce::Graphics& g, MotionSemantic sem, juce::Rectangle<float> r,
                        juce::Colour c, float sw)
{
    // 单位坐标，y 向下。曲线形状和 ContourIO 里生成的 ramp / peak 一致。
    juce::Path p;

    switch (sem)
    {
        case MotionSemantic::Preserve:
            p.startNewSubPath (0.1f, 0.5f); p.lineTo (0.9f, 0.5f);
            break;

        case MotionSemantic::Rise:
            p.startNewSubPath (0.1f, 0.84f);
            p.cubicTo (0.38f, 0.84f, 0.52f, 0.16f, 0.9f, 0.16f);
            break;

        case MotionSemantic::Fall:
            p.startNewSubPath (0.1f, 0.16f);
            p.cubicTo (0.38f, 0.16f, 0.52f, 0.84f, 0.9f, 0.84f);
            break;

        case MotionSemantic::RiseFall:
            p.startNewSubPath (0.1f, 0.84f);
            p.cubicTo (0.26f, 0.84f, 0.32f, 0.16f, 0.46f, 0.16f);
            p.cubicTo (0.6f, 0.16f, 0.68f, 0.84f, 0.9f, 0.84f);
            break;

        case MotionSemantic::FallRise:
            p.startNewSubPath (0.1f, 0.16f);
            p.cubicTo (0.26f, 0.16f, 0.32f, 0.84f, 0.46f, 0.84f);
            p.cubicTo (0.6f, 0.84f, 0.68f, 0.16f, 0.9f, 0.16f);
            break;

        case MotionSemantic::Wander:
        default:
            p.startNewSubPath (0.08f, 0.55f);
            p.cubicTo (0.2f, 0.12f, 0.3f, 0.12f, 0.4f, 0.48f);
            p.cubicTo (0.5f, 0.86f, 0.62f, 0.86f, 0.7f, 0.42f);
            p.cubicTo (0.76f, 0.2f, 0.86f, 0.24f, 0.92f, 0.4f);
            break;
    }

    strokeUnit (g, p, r, c, sw);
}

} // namespace pr::theme
