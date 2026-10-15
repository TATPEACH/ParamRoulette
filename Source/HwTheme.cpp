#include "HwTheme.h"

namespace pr::hw
{

namespace
{
    //==============================================================================
    // 程序化纹理用的值噪声。确定性（固定种子）：同一份代码画出来的木纹每次一样，
    // 截图工具才能逐像素比对。
    juce::uint32 hash2 (int x, int y, juce::uint32 seed) noexcept
    {
        auto h = (juce::uint32) x * 374761393u + (juce::uint32) y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    float rnd2 (int x, int y, juce::uint32 seed) noexcept
    {
        return (float) (hash2 (x, y, seed) & 0xffffffu) / (float) 0xffffff;
    }

    float valueNoise (float x, float y, juce::uint32 seed) noexcept
    {
        const int   xi = (int) std::floor (x), yi = (int) std::floor (y);
        const float tx = x - (float) xi,      ty = y - (float) yi;
        const float sx = tx * tx * (3.0f - 2.0f * tx);
        const float sy = ty * ty * (3.0f - 2.0f * ty);

        const float a = rnd2 (xi, yi, seed),     b = rnd2 (xi + 1, yi, seed);
        const float c = rnd2 (xi, yi + 1, seed), d = rnd2 (xi + 1, yi + 1, seed);

        return juce::jmap (sy, juce::jmap (sx, a, b), juce::jmap (sx, c, d));
    }

    float fbm (float x, float y, juce::uint32 seed, int octaves) noexcept
    {
        float sum = 0.0f, amp = 0.5f, norm = 0.0f;
        for (int o = 0; o < octaves; ++o)
        {
            sum  += amp * valueNoise (x, y, seed + (juce::uint32) o * 1013u);
            norm += amp;
            x *= 2.03f; y *= 2.03f; amp *= 0.5f;
        }
        return sum / norm;
    }

    /** 一条木板（2× 分辨率，HiDPI 下也不糊）。纹理顺着板子的长边走——
        真的框子就是这么下料的。 */
    juce::Image makeWoodStrip (int length, int thickness, bool vertical, juce::uint32 seed)
    {
        const int L = length * 2, T = thickness * 2;
        const int w = vertical ? T : L, h = vertical ? L : T;

        juce::Image img (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

        for (int j = 0; j < h; ++j)
        {
            for (int i = 0; i < w; ++i)
            {
                const float along  = (float) (vertical ? j : i) * 0.5f;
                const float across = (float) (vertical ? i : j) * 0.5f;

                // 年轮条纹沿着板长方向蜿蜒；细纤维是高频、顺纹拉长的噪声
                const float warp  = (fbm (along * 0.0032f, across * 0.03f, seed, 3) - 0.5f) * 46.0f;
                const float ring  = std::sin ((across + warp) * 0.66f);
                const float fiber = fbm (along * 0.05f, across * 0.9f, seed + 101u, 2);
                const float grain = std::pow (juce::jmax (0.0f, ring), 14.0f);    // 偶尔一道深色纹线

                float t = 0.56f + 0.2f * ring + 0.38f * (fiber - 0.5f) - 0.3f * grain;
                t = juce::jlimit (0.0f, 1.0f, t);

                bd.setPixelColour (i, j, woodDark.interpolatedWith (woodLight, t));
            }
        }

        return img;
    }

    const juce::Image& woodImage (bool vertical)
    {
        // 生成一次就够：长边留到超过最大窗口，任何尺寸都只是从里面裁一段
        static const juce::Image horizontal = makeWoodStrip (2600, 24, false, 0x5eed1234u);
        static const juce::Image verticalImg = makeWoodStrip (1800, 24, true, 0x0badc0deu);
        return vertical ? verticalImg : horizontal;
    }

    const juce::Image& grainTile()
    {
        static const juce::Image tile = []
        {
            constexpr int n = 256;
            juce::Image img (juce::Image::ARGB, n, n, true, juce::SoftwareImageType());
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < n; ++y)
            {
                for (int x = 0; x < n; ++x)
                {
                    // 细颗粒 + 很淡的大块斑驳（旧烤漆不是均匀的）。平铺接缝处用取模保证连续。
                    const float speck  = rnd2 (x, y, 77u);
                    const float blotch = fbm ((float) x / 64.0f, (float) y / 64.0f, 31u, 2);
                    const float v = (speck - 0.5f) * 0.9f + (blotch - 0.5f) * 0.7f;

                    const auto c = v > 0 ? juce::Colours::white.withAlpha (juce::jmin (1.0f, v) * 0.055f)
                                         : juce::Colours::black.withAlpha (juce::jmin (1.0f, -v) * 0.085f);
                    bd.setPixelColour (x, y, c);
                }
            }
            return img;
        }();

        return tile;
    }

    juce::Rectangle<float> circle (juce::Point<float> c, float r)
    {
        return { c.x - r, c.y - r, r * 2.0f, r * 2.0f };
    }

    juce::Path hexagon (juce::Point<float> c, float r, float rotation = 0.0f)
    {
        juce::Path p;
        for (int i = 0; i < 6; ++i)
        {
            const float a = rotation + juce::MathConstants<float>::pi / 6.0f + (float) i * juce::MathConstants<float>::pi / 3.0f;
            const juce::Point<float> pt { c.x + r * std::cos (a), c.y + r * std::sin (a) };
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        p.closeSubPath();
        return p;
    }

    juce::String pickFamily (std::initializer_list<const char*> candidates)
    {
        const auto names = juce::Font::findAllTypefaceNames();
        for (const auto* c : candidates)
            if (names.contains (c))
                return c;
        return theme::family();
    }
}

//==============================================================================
juce::Font legendFont (float pt, bool bold)
{
    static const juce::String fam = pickFamily ({ "Bahnschrift" });
    const bool din = fam == "Bahnschrift";
    return juce::Font (juce::FontOptions (fam, bold ? (din ? "SemiBold" : "Bold") : "Regular", pt).withPointHeight (pt));
}

juce::Font scriptFont (float pt)
{
    static const juce::String fam = pickFamily ({ "Segoe Print", "Segoe Script" });
    return juce::Font (juce::FontOptions (fam, "Regular", pt).withPointHeight (pt));
}

juce::Font logoFont (float pt)
{
    static const juce::String fam = pickFamily ({ "Segoe Script", "Segoe Print" });
    return juce::Font (juce::FontOptions (fam, "Bold", pt).withPointHeight (pt));
}

juce::Font monoFont (float pt)
{
    static const juce::String fam = pickFamily ({ "Bahnschrift", "Consolas" });
    return juce::Font (juce::FontOptions (fam, "Regular", pt).withPointHeight (pt));
}

bool isAscii (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
        if (*p > 127)
            return false;
    return true;
}

//==============================================================================
void fillWood (juce::Graphics& g, juce::Rectangle<int> area, bool verticalGrain)
{
    if (area.isEmpty())
        return;

    const auto& img = woodImage (verticalGrain);

    // 从纹理里按区域坐标裁一段（2× 图）。不同位置的板子取不同的段，免得四根一模一样。
    const int len   = verticalGrain ? area.getHeight() : area.getWidth();
    const int thick = verticalGrain ? area.getWidth()  : area.getHeight();
    const int offs  = ((verticalGrain ? area.getX() : area.getY()) * 7) % 300;

    const int sw = verticalGrain ? thick * 2 : len * 2;
    const int sh = verticalGrain ? len * 2   : thick * 2;
    const int sx = verticalGrain ? 0 : juce::jmin (offs * 2, juce::jmax (0, img.getWidth()  - sw));
    const int sy = verticalGrain ? juce::jmin (offs * 2, juce::jmax (0, img.getHeight() - sh)) : 0;

    g.drawImage (img, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                 sx, sy, juce::jmin (sw, img.getWidth()), juce::jmin (sh, img.getHeight()));
}

void fillPanelGrain (juce::Graphics& g, juce::Rectangle<int> area, float alpha)
{
    juce::Graphics::ScopedSaveState s (g);
    g.setTiledImageFill (grainTile(), 0, 0, alpha);
    g.fillRect (area);
}

void drawFaceplate (juce::Graphics& g, juce::Rectangle<float> r)
{
    // 只拉开 ~15% 亮度：正上方一盏灯，下沿暗一点点
    g.setGradientFill (juce::ColourGradient (panel.brighter (0.07f), r.getX(), r.getY(),
                                             panel.darker (0.14f), r.getX(), r.getBottom(), false));
    g.fillRect (r);

    // 中间略亮、四角略暗：灯光是一盏，不是均匀的面光
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), r.getCentreX(), r.getY() + r.getHeight() * 0.3f,
                                             juce::Colours::transparentBlack, r.getX(), r.getBottom(), true));
    g.fillRect (r);

    fillPanelGrain (g, r.toNearestInt());

    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.fillRect (r.withHeight (1.0f));
}

void drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    // 沉头孔
    g.setColour (seam.withAlpha (0.85f));
    g.fillEllipse (circle (c, r + 1.5f));
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (circle (c.translated (0.0f, 0.6f), r + 1.5f), 0.8f);

    // 钉头：光从上面来，左上亮、右下暗
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8dce0), c.x - r * 0.45f, c.y - r * 0.55f,
                                             juce::Colour (0xff59616a), c.x + r * 0.6f, c.y + r * 0.85f, true));
    g.fillEllipse (circle (c, r));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawEllipse (circle (c, r - 0.4f), 0.8f);

    // 十字槽：先画一道下移的亮边（槽的下沿受光），再画深色的槽
    juce::Path slot;
    slot.addRoundedRectangle (-r * 0.64f, -r * 0.13f, r * 1.28f, r * 0.26f, r * 0.08f);
    slot.addRoundedRectangle (-r * 0.13f, -r * 0.64f, r * 0.26f, r * 1.28f, r * 0.08f);

    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c.x, c.y + 0.7f));
    g.setColour (juce::Colour (0xff272c31));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

juce::Rectangle<float> drawPlate (juce::Graphics& g, juce::Point<float> tl, const juce::String& text,
                                  float pt, const juce::String& english)
{
    const auto f  = font (pt, Weight::bold);
    const auto ef = legendFont (pt * 0.74f, true);
    const float tw = juce::GlyphArrangement::getStringWidth (f, text);
    const float ew = english.isNotEmpty() ? juce::GlyphArrangement::getStringWidth (ef, english) + 9.0f : 0.0f;
    const float h  = std::round (pt * 1.72f);

    const juce::Rectangle<float> r (tl.x, tl.y, std::round (tw + ew + 18.0f), h);

    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.3f), 2.5f);

    g.setGradientFill (juce::ColourGradient (plate.brighter (0.05f), r.getX(), r.getY(),
                                             plate.darker (0.09f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRect (r.reduced (2.0f, 0.0f).withHeight (1.0f));

    g.setColour (plateInk);
    g.setFont (f);
    g.drawText (text, r.withTrimmedLeft (9.0f).withWidth (tw + 2.0f), juce::Justification::centredLeft, false);

    if (english.isNotEmpty())
    {
        g.setColour (plateInk.withAlpha (0.55f));
        g.setFont (ef);
        g.drawText (english, r.withTrimmedLeft (9.0f + tw + 8.0f), juce::Justification::centredLeft, false);
    }

    return r;
}

juce::Rectangle<int> drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                  const juce::String& english)
{
    constexpr float pt = 11.5f;
    const float plateH = std::round (pt * 1.72f);

    const auto r   = area.toFloat().reduced (0.5f);
    const auto box = r.withTrimmedTop (plateH * 0.5f);

    juce::Path outline;
    outline.addRoundedRectangle (box, 8.0f);

    // 丝印框线：米白，很淡；下面再压一道更暗的，像油墨印在略有凹凸的漆面上
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.strokePath (outline, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (legend.withAlpha (0.30f));
    g.strokePath (outline, juce::PathStrokeType (1.2f));

    drawPlate (g, { box.getX() + 14.0f, r.getY() }, title, pt, english);

    return area.withTrimmedTop ((int) plateH + 8).reduced (14, 0).withTrimmedBottom (12);
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill)
{
    // 下沿受光的一道细亮边 → 压进面板的感觉
    g.setColour (juce::Colours::white.withAlpha (0.11f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), radius);

    g.setColour (seam);
    g.fillRoundedRectangle (r, radius);

    const auto inner = r.reduced (1.0f);
    const float ir   = juce::jmax (0.0f, radius - 1.0f);
    g.setColour (fill);
    g.fillRoundedRectangle (inner, ir);

    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (inner, ir);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), 0.0f, inner.getY(),
                                             juce::Colours::transparentBlack, 0.0f, inner.getY() + 9.0f, false));
    g.fillRect (inner.withHeight (9.0f));
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float b)
{
    b = juce::jlimit (0.0f, 1.0f, b);

    if (b > 0.01f)
    {
        // 光晕洒在面板上
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.5f * b), c.x, c.y,
                                                 col.withAlpha (0.0f), c.x + r * 3.4f, c.y, true));
        g.fillEllipse (circle (c, r * 3.4f));
    }

    // 黑色灯座
    g.setColour (juce::Colour (0xff0a0e12));
    g.fillEllipse (circle (c, r + 1.5f));
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawEllipse (circle (c.translated (0.0f, 0.5f), r + 1.5f), 0.7f);

    // 透镜：熄灭时是深色的有色玻璃，点亮时整颗透出来
    const auto off  = col.interpolatedWith (juce::Colours::black, 0.74f);
    const auto lens = off.interpolatedWith (col, b);
    g.setGradientFill (juce::ColourGradient (lens.brighter (0.12f + 0.45f * b), c.x - r * 0.3f, c.y - r * 0.45f,
                                             lens.darker (0.35f), c.x + r * 0.45f, c.y + r * 0.7f, true));
    g.fillEllipse (circle (c, r));

    if (b > 0.01f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f * b));
        g.fillEllipse (circle (c, r * 0.36f));
    }

    g.setColour (juce::Colours::white.withAlpha (0.42f));
    g.fillEllipse (juce::Rectangle<float> (r * 0.55f, r * 0.34f).withCentre ({ c.x - r * 0.22f, c.y - r * 0.46f }));
}

void drawJack (juce::Graphics& g, juce::Point<float> c, float r)
{
    const auto hex = hexagon (c, r);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (hex, juce::AffineTransform::translation (0.0f, 1.4f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9cfd4), c.x, c.y - r,
                                             juce::Colour (0xff5d656c), c.x, c.y + r, false));
    g.fillPath (hex);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (hex, juce::PathStrokeType (0.7f));

    g.setColour (juce::Colour (0xff15191d));
    g.fillEllipse (circle (c, r * 0.64f));
    g.setColour (juce::Colours::black);
    g.fillEllipse (circle (c, r * 0.38f));
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawEllipse (circle (c.translated (0.0f, 0.5f), r * 0.64f), 0.7f);
}

void drawPatchCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b,
                     juce::Colour col, float sag, float t)
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dx = b.x - a.x;
    p.cubicTo (a.x + dx * 0.22f, a.y + sag, b.x - dx * 0.22f, b.y + sag, b.x, b.y);

    const juce::PathStrokeType::JointStyle j = juce::PathStrokeType::curved;
    const juce::PathStrokeType::EndCapStyle e = juce::PathStrokeType::rounded;

    // 落在面板上的影子
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.strokePath (p, juce::PathStrokeType (t + 2.5f, j, e), juce::AffineTransform::translation (1.5f, 4.5f));

    // 线身：深色轮廓 → 本色 → 一道高光（光从上面来，高光在上沿）
    g.setColour (col.darker (0.75f));
    g.strokePath (p, juce::PathStrokeType (t + 1.6f, j, e));
    g.setColour (col.darker (0.12f));
    g.strokePath (p, juce::PathStrokeType (t, j, e));
    g.setColour (col.brighter (0.7f).withAlpha (0.5f));
    g.strokePath (p, juce::PathStrokeType (t * 0.28f, j, e), juce::AffineTransform::translation (0.0f, -t * 0.24f));

    // 两头的插头：金属套 + 有色的护套
    for (auto end : { a, b })
    {
        g.setColour (juce::Colour (0xff3e444a));
        g.fillEllipse (circle (end, t * 1.05f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd5dade), end.x, end.y - t,
                                                 juce::Colour (0xff6c747b), end.x, end.y + t, false));
        g.fillEllipse (circle (end, t * 0.9f));
        g.setColour (col.darker (0.25f));
        g.fillEllipse (circle (end, t * 0.5f));
    }
}

void drawBatToggle (juce::Graphics& g, juce::Rectangle<float> r, float pos, bool enabled)
{
    const auto  c   = r.getCentre();
    const float s   = juce::jmin (r.getWidth(), r.getHeight());
    const float nut = s * 0.27f;
    const float a   = enabled ? 1.0f : 0.5f;

    // 六角螺母（参考图 6 的 DEEP 开关那种）
    const auto hex = hexagon (c, nut);
    g.setColour (juce::Colours::black.withAlpha (0.45f * a));
    g.fillPath (hex, juce::AffineTransform::translation (0.0f, 1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8c9196).withMultipliedAlpha (a), c.x, c.y - nut,
                                             juce::Colour (0xff2c3035).withMultipliedAlpha (a), c.x, c.y + nut, false));
    g.fillPath (hex);
    g.setColour (juce::Colour (0xff1b1e22));
    g.fillEllipse (circle (c, nut * 0.6f));

    // 拨杆：pos 1 = 朝上，0 = 朝下。正对着看时拨杆的投影长度随角度变化，中间最短。
    const float dir   = pos * 2.0f - 1.0f;                    // -1 下 … +1 上
    const float len   = s * 0.44f * juce::jmax (0.18f, std::abs (dir));
    const float sign  = dir >= 0.0f ? -1.0f : 1.0f;           // 屏幕坐标 y 向下
    const auto  tip   = c.translated (0.0f, sign * len);
    const float wBase = nut * 0.42f, wTip = nut * 0.62f;

    // 拨杆在面板上的影子
    {
        juce::Path sh;
        sh.addLineSegment ({ c.translated (2.0f, 3.0f), tip.translated (2.0f, 3.0f + (sign > 0 ? 2.0f : 0.0f)) }, wTip * 1.6f);
        g.setColour (juce::Colours::black.withAlpha (0.28f * a));
        g.fillPath (sh);
    }

    juce::Path lever;
    lever.startNewSubPath (c.x - wBase, c.y);
    lever.lineTo (tip.x - wTip * 0.8f, tip.y);
    lever.lineTo (tip.x + wTip * 0.8f, tip.y);
    lever.lineTo (c.x + wBase, c.y);
    lever.closeSubPath();

    // 镀铬圆柱：横向渐变（左亮右暗）
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff1f3f5).withMultipliedAlpha (a), c.x - wTip, c.y,
                                             juce::Colour (0xff6b737a).withMultipliedAlpha (a), c.x + wTip, c.y, false));
    g.fillPath (lever);

    // 末端的圆头
    const auto knobBox = circle (tip, wTip);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff).withMultipliedAlpha (a), tip.x - wTip * 0.5f, tip.y - wTip * 0.6f,
                                             juce::Colour (0xff7d858c).withMultipliedAlpha (a), tip.x + wTip * 0.6f, tip.y + wTip * 0.8f, true));
    g.fillEllipse (knobBox);
    g.setColour (juce::Colours::black.withAlpha (0.35f * a));
    g.drawEllipse (knobBox, 0.7f);
}

//==============================================================================
juce::Rectangle<float> drawKey (juce::Graphics& g, juce::Rectangle<float> well, bool down, bool hover,
                                juce::Colour base, float radius, bool enabled)
{
    if (! enabled)
        base = base.withMultipliedSaturation (0.3f).darker (0.25f);

    const float depth = down ? 1.0f : 2.5f;
    const auto  top   = well.reduced (2.0f).withTrimmedBottom (depth).translated (0.0f, down ? 1.5f : 0.0f);

    g.setColour (seam.withAlpha (0.95f));
    g.fillRoundedRectangle (well, radius + 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (well.translated (0.0f, 0.6f), radius + 1.5f, 0.8f);

    g.setColour (base.darker (0.62f));
    g.fillRoundedRectangle (top.withHeight (top.getHeight() + depth), radius);

    const float lift = hover && enabled ? 0.1f : 0.0f;
    g.setGradientFill (juce::ColourGradient (base.brighter (0.16f + lift), 0.0f, top.getY(),
                                             base.darker (0.14f - lift * 0.5f), 0.0f, top.getBottom(), false));
    g.fillRoundedRectangle (top, radius);

    g.setColour (juce::Colours::white.withAlpha (down ? 0.06f : 0.17f));
    g.fillRect (top.reduced (radius, 0.0f).withHeight (1.0f).translated (0.0f, 0.5f));

    return top;
}

//==============================================================================
void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle, Knob style,
               bool enabled, bool hover)
{
    const float R = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  c = bounds.getCentre();

    juce::Colour base;
    switch (style)
    {
        case Knob::pointerRed:    base = capRed;    break;
        case Knob::pointerYellow: base = capYellow; break;
        case Knob::pointerBlack:  base = capBlack;  break;
        case Knob::knurledGrey:   base = capGrey;   break;
        case Knob::knurledBlack:  base = capBlack;  break;
    }

    if (! enabled)
        base = base.withMultipliedSaturation (0.25f).darker (0.35f);

    // —— 接触阴影：往下偏一点的柔边椭圆（光在正上方）
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), c.x, c.y + R * 0.2f,
                                             juce::Colours::transparentBlack, c.x + R * 1.18f, c.y + R * 0.2f, true));
    g.fillEllipse (circle (c.translated (0.0f, R * 0.2f), R * 1.18f));

    const auto rot = juce::AffineTransform::rotation (angle).translated (c.x, c.y);

    // 屏幕坐标里的竖向渐变——光影不跟着转
    auto vertical = [&] (juce::Colour top, juce::Colour bottom, float r)
    {
        return juce::ColourGradient (top, c.x, c.y - r, bottom, c.x, c.y + r, false);
    };

    const bool pointer = style == Knob::pointerRed || style == Knob::pointerYellow || style == Knob::pointerBlack;

    if (pointer)
    {
        // —— 底座圆盘
        g.setGradientFill (vertical (base.brighter (0.18f), base.darker (0.55f), R * 0.9f));
        g.fillEllipse (circle (c, R * 0.86f));
        g.setColour (base.darker (0.75f).withAlpha (0.8f));
        g.drawEllipse (circle (c, R * 0.86f), 0.9f);

        // —— 指针鳍：胶木"鸡头"，尖端朝 angle 方向，尾部宽
        juce::Path fin;
        fin.startNewSubPath (-R * 0.13f, -R * 0.98f);
        fin.lineTo ( R * 0.13f, -R * 0.98f);
        fin.lineTo ( R * 0.3f,   R * 0.62f);
        fin.lineTo (-R * 0.3f,   R * 0.62f);
        fin.closeSubPath();
        fin = fin.createPathWithRoundedCorners (R * 0.12f);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (fin, rot.translated (0.0f, R * 0.08f));
        g.setGradientFill (vertical (base.brighter (0.42f), base.darker (0.32f), R));
        g.fillPath (fin, rot);
        g.setColour (base.darker (0.7f).withAlpha (0.9f));
        g.strokePath (fin, juce::PathStrokeType (0.8f), rot);

        // —— 中心圆帽（略微隆起）
        const float capR = R * 0.5f;
        g.setGradientFill (vertical (base.brighter (0.32f), base.darker (0.25f), capR));
        g.fillEllipse (circle (c, capR));

        // —— 指示线：只有它和鳍跟着转
        juce::Path line;
        line.startNewSubPath (0.0f, -R * 0.52f);
        line.lineTo (0.0f, -R * 0.9f);
        g.setColour ((enabled ? juce::Colour (0xfff7f2e6) : juce::Colour (0xff9a9a9a)));
        g.strokePath (line, juce::PathStrokeType (juce::jmax (1.6f, R * 0.085f), juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded), rot);

        // —— 高光（固定在左上）
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (enabled ? 0.38f : 0.15f), c.x - R * 0.2f, c.y - R * 0.42f,
                                                 juce::Colours::white.withAlpha (0.0f), c.x - R * 0.2f, c.y + R * 0.1f, true));
        g.fillEllipse (juce::Rectangle<float> (R * 0.62f, R * 0.4f).withCentre ({ c.x - R * 0.12f, c.y - R * 0.24f }));
    }
    else
    {
        // —— 滚花外圈（静止不转：图案是对称的，转起来反而会闪）
        constexpr int teeth = 44;
        juce::Path knurl;
        for (int i = 0; i < teeth * 2; ++i)
        {
            const float a  = (float) i * juce::MathConstants<float>::pi / (float) teeth;
            const float rr = (i % 2 == 0) ? R * 0.97f : R * 0.9f;
            const juce::Point<float> pt { c.x + rr * std::sin (a), c.y - rr * std::cos (a) };
            if (i == 0) knurl.startNewSubPath (pt); else knurl.lineTo (pt);
        }
        knurl.closeSubPath();

        g.setGradientFill (vertical (base.brighter (0.28f), base.darker (0.6f), R));
        g.fillPath (knurl);
        g.setColour (base.darker (0.8f).withAlpha (0.7f));
        g.strokePath (knurl, juce::PathStrokeType (0.6f));

        // —— 顶面：一圈斜面 + 平顶
        g.setGradientFill (vertical (base.darker (0.35f), base.brighter (0.15f), R * 0.82f));   // 斜面：上暗下亮
        g.fillEllipse (circle (c, R * 0.8f));
        g.setGradientFill (vertical (base.brighter (0.22f), base.darker (0.12f), R * 0.7f));
        g.fillEllipse (circle (c, R * 0.7f));

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (enabled ? 0.22f : 0.08f), c.x, c.y - R * 0.55f,
                                                 juce::Colours::white.withAlpha (0.0f), c.x, c.y + R * 0.1f, true));
        g.fillEllipse (circle (c, R * 0.7f));

        // —— 白色指示线
        juce::Path line;
        line.startNewSubPath (0.0f, -R * 0.36f);
        line.lineTo (0.0f, -R * 0.8f);
        g.setColour (enabled ? juce::Colour (0xfff2efe6) : juce::Colour (0xff8f8f8f));
        g.strokePath (line, juce::PathStrokeType (juce::jmax (1.6f, R * 0.09f), juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded), rot);
    }

    if (hover && enabled)
    {
        g.setColour (legend.withAlpha (0.16f));
        g.drawEllipse (circle (c, R + 2.0f), 1.2f);
    }
}

void drawScale (juce::Graphics& g, juce::Point<float> c, float radius, float a0, float a1,
                int numTicks, int majorEvery, const juce::StringArray& labels, juce::Colour col,
                float labelRadius)
{
    for (int i = 0; i < numTicks; ++i)
    {
        const float t = numTicks > 1 ? (float) i / (float) (numTicks - 1) : 0.5f;
        const float a = a0 + t * (a1 - a0);
        const bool  major = majorEvery > 0 && (i % majorEvery == 0);
        const auto  p = c.getPointOnCircumference (radius, a);
        const float d = major ? 3.0f : 1.9f;

        g.setColour (col.withMultipliedAlpha (major ? 1.0f : 0.7f));
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (p));
    }

    if (! labels.isEmpty() && labelRadius > 0.0f)
    {
        g.setFont (legendFont (9.0f, true));
        g.setColour (col);

        for (int k = 0; k < labels.size(); ++k)
        {
            const float t = labels.size() > 1 ? (float) k / (float) (labels.size() - 1) : 0.5f;
            const auto  p = c.getPointOnCircumference (labelRadius, a0 + t * (a1 - a0));
            g.drawText (labels[k], juce::Rectangle<float> (26.0f, 12.0f).withCentre (p), juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void drawGlowText (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c,
                   const juce::Font& f, juce::Justification j, bool ellipsis, float glow)
{
    g.setFont (f);

    if (glow > 0.0f)
    {
        g.setColour (c.withMultipliedAlpha (0.22f * glow));
        g.drawText (s, r.translated (-0.7f, 0.0f), j, ellipsis);
        g.drawText (s, r.translated ( 0.7f, 0.0f), j, ellipsis);
        g.drawText (s, r.translated (0.0f, -0.7f), j, ellipsis);
        g.drawText (s, r.translated (0.0f,  0.7f), j, ellipsis);
    }

    g.setColour (c);
    g.drawText (s, r, j, ellipsis);
}

namespace
{
    juce::uint8 segmentMask (juce::juce_wchar ch)
    {
        switch (ch)
        {
            case '0': return 0x3f;  case '1': return 0x06;  case '2': return 0x5b;  case '3': return 0x4f;
            case '4': return 0x66;  case '5': return 0x6d;  case '6': return 0x7d;  case '7': return 0x07;
            case '8': return 0x7f;  case '9': return 0x6f;  case '-': return 0x40;  case 'E': return 0x79;
            case 'r': return 0x50;  case 'n': return 0x54;  case 'o': return 0x5c;  case 'F': return 0x71;
            default:  return 0x00;
        }
    }
}

void drawSevenSegment (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& chars, juce::Colour lit)
{
    const int n = chars.length();
    if (n == 0)
        return;

    const float cw = r.getWidth() / (float) n;

    for (int i = 0; i < n; ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + (float) i * cw, r.getY(), cw, r.getHeight())
                              .reduced (cw * 0.13f, r.getHeight() * 0.07f);
        const float W = cell.getWidth(), H = cell.getHeight();
        const float t = juce::jmin (W * 0.2f, H * 0.12f);
        const auto  mask = segmentMask (chars[i]);

        // 每段是一个两头尖的六边形；整体右斜一点，像真的数码管
        auto seg = [&] (juce::Point<float> p1, juce::Point<float> p2)
        {
            const bool horiz = std::abs (p2.x - p1.x) > std::abs (p2.y - p1.y);
            const float h = t * 0.5f;
            juce::Path p;
            if (horiz)
            {
                p.startNewSubPath (p1);
                p.lineTo (p1.x + h, p1.y - h); p.lineTo (p2.x - h, p2.y - h);
                p.lineTo (p2);
                p.lineTo (p2.x - h, p2.y + h); p.lineTo (p1.x + h, p1.y + h);
            }
            else
            {
                p.startNewSubPath (p1);
                p.lineTo (p1.x + h, p1.y + h); p.lineTo (p2.x + h, p2.y - h);
                p.lineTo (p2);
                p.lineTo (p2.x - h, p2.y - h); p.lineTo (p1.x - h, p1.y + h);
            }
            p.closeSubPath();
            return p;
        };

        const float x0 = cell.getX(), y0 = cell.getY();
        const float gapPx = t * 0.25f;
        const float ht = t * 0.5f;
        const juce::Path segs[7] =
        {
            seg ({ x0 + gapPx,         y0 + ht },       { x0 + W - gapPx,     y0 + ht }),          // a
            seg ({ x0 + W - ht,     y0 + gapPx },       { x0 + W - ht,     y0 + H * 0.5f - gapPx }), // b
            seg ({ x0 + W - ht,     y0 + H * 0.5f + gapPx }, { x0 + W - ht, y0 + H - gapPx }),     // c
            seg ({ x0 + gapPx,         y0 + H - ht },   { x0 + W - gapPx,     y0 + H - ht }),      // d
            seg ({ x0 + ht,         y0 + H * 0.5f + gapPx }, { x0 + ht,    y0 + H - gapPx }),      // e
            seg ({ x0 + ht,         y0 + gapPx },       { x0 + ht,         y0 + H * 0.5f - gapPx }), // f
            seg ({ x0 + gapPx,         y0 + H * 0.5f },    { x0 + W - gapPx,     y0 + H * 0.5f })        // g
        };

        const auto slant = juce::AffineTransform::shear (-0.09f, 0.0f).translated (0.09f * (y0 + H), 0.0f);

        for (int s = 0; s < 7; ++s)
        {
            const bool on = (mask >> s) & 1;
            if (on)
            {
                g.setColour (lit.withAlpha (0.25f));
                g.strokePath (segs[s], juce::PathStrokeType (t * 0.55f), slant);
            }
            g.setColour (on ? lit : lit.withAlpha (0.07f));
            g.fillPath (segs[s], slant);
        }
    }
}

void drawGlassSheen (juce::Graphics& g, juce::Rectangle<float> r, float radius, float strength)
{
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);

    juce::Path sheen;
    sheen.startNewSubPath (r.getX(), r.getY());
    sheen.lineTo (r.getRight(), r.getY());
    sheen.lineTo (r.getRight(), r.getY() + r.getHeight() * 0.16f);
    sheen.lineTo (r.getX(), r.getY() + r.getHeight() * 0.48f);
    sheen.closeSubPath();

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.075f * strength), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.48f, false));
    g.fillPath (sheen);
}

} // namespace pr::hw
