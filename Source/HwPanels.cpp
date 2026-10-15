#include "HwPanels.h"

namespace pr
{

using theme::Weight;

namespace
{
    juce::Rectangle<float> circleBox (juce::Point<float> c, float r) { return { c.x - r, c.y - r, r * 2.0f, r * 2.0f }; }

    /** VFD 的金属网：每 3px 一道很淡的暗线。3×3 的小图平铺。 */
    const juce::Image& meshTile()
    {
        static const juce::Image tile = []
        {
            juce::Image img (juce::Image::ARGB, 3, 3, true, juce::SoftwareImageType());
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    bd.setPixelColour (x, y, (x == 0 || y == 0) ? juce::Colours::black.withAlpha (0.22f)
                                                                : juce::Colours::transparentBlack);
            return img;
        }();
        return tile;
    }

    void paintMesh (juce::Graphics& g, juce::Rectangle<int> area)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.setTiledImageFill (meshTile(), 0, 0, 1.0f);
        g.fillRect (area);
    }

    /** 屏幕玻璃底：中间微亮（荧光照在玻璃上），四角压暗。 */
    void paintGlass (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, juce::Colour tint, float radius)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.07f), r.getCentreX(), r.getCentreY(),
                                                 juce::Colours::black.withAlpha (0.35f), r.getX(), r.getY(), true));
        g.fillRoundedRectangle (r, radius);
    }
}

//==============================================================================
HwParamList::HwParamList()
{
    setOpaque (false);
}

void HwParamList::paint (juce::Graphics& g)
{
    using namespace hw;

    paintGlass (g, getLocalBounds().toFloat(), glass, vfd, 3.0f);
    paintHeader (g, getLocalBounds().removeFromTop (kHeaderH));

    if (! visible.isEmpty())
        return;

    const auto area = getLocalBounds().withTrimmedTop (kHeaderH).toFloat();
    const bool noPlugin = model == nullptr || model->isEmpty();

    drawGlowText (g, noPlugin ? "NO PLUGIN" : "NO MATCH", area.withHeight (28.0f).withCentre ({ area.getCentreX(), area.getCentreY() - 20.0f }),
                  vfd, legendFont (20.0f, true), juce::Justification::centred, false, 0.6f);
    g.setFont (font (12.5f, Weight::medium));
    g.setColour (vfdDim.brighter (0.4f));
    g.drawText (noPlugin ? "没有加载插件——点上面机架里的空位装一个"_u8 : "没有符合筛选的参数，换个关键词或者切回「全部」"_u8,
                area.withHeight (20.0f).withCentre ({ area.getCentreX(), area.getCentreY() + 10.0f }),
                juce::Justification::centred, true);
}

void HwParamList::paintOverChildren (juce::Graphics& g)
{
    paintMesh (g, getLocalBounds());
    hw::drawGlassSheen (g, getLocalBounds().toFloat(), 3.0f, 0.9f);
}

void HwParamList::paintHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    using namespace hw;

    const auto cols = layoutCols (area.withWidth (viewport.getMaximumVisibleWidth()));

    // 全选格：三态。全勾 = 实心亮块，部分 = 中间一道，全没勾 = 空框
    {
        const int nChecked = getNumChecked();
        const int nVisible = visible.size();
        const auto box = cols.check.toFloat().withSizeKeepingCentre (11.0f, 11.0f);

        if (nVisible > 0 && nChecked == nVisible)
        {
            g.setColour (vfd.withAlpha (0.25f));
            g.fillRect (box.expanded (2.0f));
            g.setColour (vfd);
            g.fillRect (box);
        }
        else
        {
            g.setColour (vfdDim);
            g.drawRect (box, 1.0f);
            if (nChecked > 0)
            {
                g.setColour (vfd);
                g.fillRect (box.withSizeKeepingCentre (7.0f, 2.0f));
            }
        }
    }

    g.setFont (font (10.5f, Weight::bold));
    g.setColour (vfdDim.brighter (0.25f));

    g.drawText ("#",            cols.index, juce::Justification::centredRight, false);
    g.drawText ("参数"_u8,      cols.name,  juce::Justification::centredLeft,  false);
    g.drawText ("类型"_u8,      cols.kind,  juce::Justification::centredLeft,  false);
    g.drawText ("旧值"_u8,      cols.was,   juce::Justification::centredLeft,  false);
    g.drawText ("当前值"_u8,    cols.value, juce::Justification::centredLeft,  false);
    g.drawText ("幅度"_u8,      cols.span,  juce::Justification::centred,      false);
    g.drawText ("备注"_u8,      cols.note.withTrimmedLeft (8), juce::Justification::centredLeft, false);

    // 虚线分隔（点阵屏上画不出实线的那种感觉）
    g.setColour (vfdFaint.brighter (0.2f));
    for (int x = area.getX() + 6; x < area.getRight() - 6; x += 4)
        g.fillRect (x, area.getBottom() - 2, 2, 1);
}

void HwParamList::paintRow (juce::Graphics& g, int displayRow, juce::Rectangle<int> area)
{
    using namespace hw;

    if (model == nullptr)
        return;

    const int mi = visible.getUnchecked (displayRow);
    if (! juce::isPositiveAndBelow (mi, model->size()))
        return;

    const auto& p    = model->getReference (mi);
    const bool  on   = isChecked (mi);
    const auto  cols = layoutCols (area);

    if (displayRow == hoverRow)
    {
        g.setColour (vfd.withAlpha (0.07f));
        g.fillRect (area.reduced (3, 1));
    }

    // 本次改动过的行：最左边一个琥珀色的 ▸
    if (p.changed)
    {
        juce::Path tri;
        const float cy = (float) area.getCentreY(), x = (float) area.getX() + 2.0f;
        tri.addTriangle (x, cy - 4.0f, x, cy + 4.0f, x + 5.0f, cy);
        g.setColour (vfdAmber);
        g.fillPath (tri);
    }

    // —— 勾选：点亮的像素块 / 空框
    {
        const auto box = cols.check.toFloat().withSizeKeepingCentre (11.0f, 11.0f);
        if (on)
        {
            g.setColour (vfd.withAlpha (0.22f));
            g.fillRect (box.expanded (2.0f));
            g.setColour (vfd);
            g.fillRect (box);
        }
        else
        {
            g.setColour (vfdDim);
            g.drawRect (box, 1.0f);
        }
    }

    // —— 序号
    g.setFont (monoFont (11.5f));
    g.setColour (vfdDim.brighter (0.15f));
    g.drawText (juce::String (p.index), cols.index, juce::Justification::centredRight, false);

    // —— 名字：没勾的压暗但可读；写不进去的标红
    const bool inert = (p.health == Health::Inert);
    g.setFont (font (12.5f, Weight::medium));
    g.setColour (inert ? vfdRed : (on ? vfd : vfdDim.brighter (0.5f)));
    g.drawText (p.name, cols.name, juce::Justification::centredLeft, true);

    // —— 类型：一个细框里的小字
    if (! cols.kind.isEmpty())
    {
        juce::String label = kindLabel (p.kind);
        if (p.kind == Kind::Stepped)
            label << " " << p.numSteps;

        const auto c = p.kind == Kind::Toggle  ? vfdAmber
                     : p.kind == Kind::Stepped ? vfd
                                               : vfdDim.brighter (0.3f);

        g.setFont (font (10.5f, Weight::medium));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 12.0f;
        const auto box = cols.kind.toFloat().withSizeKeepingCentre (juce::jmin ((float) cols.kind.getWidth() - 4.0f, tw), 16.0f)
                                            .withX ((float) cols.kind.getX());
        g.setColour (c.withAlpha (0.5f));
        g.drawRect (box, 1.0f);
        g.setColour (c);
        g.drawFittedText (label, box.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    // —— 旧值：只有本次真的改动过才显示
    if (p.changed && ! cols.was.isEmpty())
    {
        g.setFont (font (11.5f));
        g.setColour (vfdDim.brighter (0.3f));
        g.drawText (p.prevDisplay, cols.was.withTrimmedRight (6), juce::Justification::centredLeft, true);
    }

    // —— 当前值 + 一排 LED 段（"动了多少"一眼可见）
    {
        auto vr = cols.value;
        const auto vc = p.blob ? vfdAmber : (p.changed ? vfd.brighter (0.45f) : vfd);

        g.setFont (font (12.0f, p.changed ? Weight::bold : Weight::regular));
        g.setColour (vc);
        g.drawText (p.display, vr.withTrimmedBottom (5).withTrimmedRight (8), juce::Justification::centredLeft, true);

        if (! p.blob)
        {
            constexpr int nSeg = 16;
            const float barW = (float) vr.getWidth() - 10.0f;
            const float segW = barW / (float) nSeg;
            const float y    = (float) vr.getBottom() - 6.0f;
            const int   lit  = juce::roundToInt (juce::jlimit (0.0f, 1.0f, p.value) * (float) nSeg);
            const int   was  = juce::jlimit (0, nSeg - 1, (int) (juce::jlimit (0.0f, 1.0f, p.prevValue) * (float) nSeg - 0.001f));

            for (int s = 0; s < nSeg; ++s)
            {
                const auto seg = juce::Rectangle<float> ((float) vr.getX() + (float) s * segW, y, segW - 1.5f, 3.0f);
                g.setColour (s < lit ? (p.changed ? vfd.brighter (0.2f) : vfd.withAlpha (0.75f)) : vfdFaint);
                g.fillRect (seg);

                if (p.changed && s == was)
                {
                    g.setColour (vfdAmber);
                    g.drawRect (seg.expanded (0.5f, 1.0f), 1.0f);
                }
            }
        }
    }

    // —— 音色层抖动幅度：设过的是琥珀色数字，没设过是一个暗点（悬停变 +）
    {
        const bool hov = (displayRow == hoverRow) && hoverSpan;

        if (p.hasCustomSpan())
        {
            const auto label = juce::String::fromUTF8 ("\xc2\xb1") + juce::String (p.span, 3);
            g.setFont (monoFont (11.5f));
            g.setColour (hov ? vfdAmber.brighter (0.3f) : vfdAmber);
            g.drawFittedText (label, cols.span, juce::Justification::centred, 1, 0.8f);
        }
        else
        {
            g.setFont (font (12.0f, Weight::bold));
            g.setColour (hov ? vfd : vfdDim);
            g.drawText (hov ? "+" : juce::String::fromUTF8 ("\xc2\xb7"), cols.span, juce::Justification::centred, false);
        }
    }

    // —— 备注：体检结论优先
    if (! cols.note.isEmpty())
    {
        juce::String noteText;
        juce::Colour dot = juce::Colours::transparentBlack;

        if (p.health == Health::Inert)             { noteText = healthLabel (p.health); dot = vfdRed; }
        else if (p.health == Health::EndpointOnly) { noteText = healthLabel (p.health); dot = vfdAmber; }
        else if (p.defaultSkip())                  { noteText = skipReasonLabel (p.skipReason); }
        else if (p.health == Health::Ok)           { noteText = healthLabel (p.health); dot = vfd; }

        auto nr = cols.note.withTrimmedLeft (8);

        if (! dot.isTransparent())
        {
            g.setColour (dot);
            g.fillRect (nr.removeFromLeft (7).toFloat().withSizeKeepingCentre (5.0f, 5.0f));
            nr.removeFromLeft (5);
        }

        g.setFont (font (11.0f));
        g.setColour (dot.isTransparent() ? vfdDim.brighter (0.3f) : dot);
        g.drawText (noteText, nr, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
void HwChainRack::resized()
{
    const auto b = getLocalBounds().toFloat();

    // 模块下面留 ~20px：跳线从模块底部的插孔出来，会往下垂
    const float h = juce::jlimit (60.0f, 108.0f, b.getHeight() - 22.0f);
    const float y = b.getY() + 2.0f;

    constexpr float jackW = 30.0f, gapPx = 18.0f, guardW = 186.0f;

    inNode  = { b.getX() + 14.0f,     y + h - 14.0f };
    outNode = { b.getRight() - 14.0f, y + h - 14.0f };

    const float avail = b.getWidth() - 2.0f * jackW - guardW - (float) kMaxChain * gapPx - gapPx;
    const float w     = juce::jmax (66.0f, avail / (float) kMaxChain);

    float x = b.getX() + jackW + gapPx * 0.5f;
    for (auto& r : rects)
    {
        r = { x, y, w, h };
        x += w + gapPx;
    }

    guardRect = { x, y, guardW, h };
}

juce::Rectangle<float> HwChainRack::powerRect (juce::Rectangle<float> m) const
{
    // 输出保护模块：「开关」是左边那个拨杆。链位模块：底部中间的 IN 键。
    if (m == guardRect)
        return { m.getX() + 8.0f, m.getY() + 24.0f, 34.0f, 46.0f };

    return { m.getCentreX() - 17.0f, m.getBottom() - 25.0f, 34.0f, 20.0f };
}

juce::Rectangle<int> HwChainRack::ceilingKnobArea() const
{
    // 在 NaN 灯右边、OUT 插孔左边（插孔占最右边 ~20px，读数不能压着它）
    const auto g = guardRect;
    return juce::Rectangle<float> (g.getRight() - 90.0f, g.getY() + 15.0f, 68.0f, g.getHeight() - 19.0f).toNearestInt();
}

void HwChainRack::paintPlate (juce::Graphics& g, juce::Rectangle<float> r, bool blank, bool isFocused, bool hover)
{
    using namespace hw;

    // 模块面板：比主面板亮一级的藏青（空白挡板暗一级），一圈细黑缝，上沿一道高光
    const auto base = blank ? panel.darker (0.25f) : panel.brighter (hover ? 0.2f : 0.12f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (base.brighter (0.06f), 0.0f, r.getY(), base.darker (0.12f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    fillPanelGrain (g, r.toNearestInt(), 0.8f);
    g.setColour (seam);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.fillRect (r.reduced (4.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));

    drawScrew (g, { r.getX() + 7.0f, r.getY() + 7.0f }, 2.6f, 0.3f + r.getX() * 0.013f);
    drawScrew (g, { r.getRight() - 7.0f, r.getY() + 7.0f }, 2.6f, 1.1f + r.getX() * 0.007f);

    if (isFocused)
    {
        g.setColour (ledAmber.withAlpha (0.55f));
        g.drawRoundedRectangle (r.expanded (1.5f), 5.0f, 1.6f);
    }
}

void HwChainRack::paintRackModule (juce::Graphics& g, int i)
{
    using namespace hw;

    const auto& s   = slots[(size_t) i];
    const auto  r   = rects[(size_t) i];
    const bool  foc = (i == focused);
    const bool  hov = (i == hoverIndex);

    paintPlate (g, r, ! s.loaded, foc, hov);

    // 链位号 + 选中灯
    g.setFont (legendFont (11.0f, true));
    g.setColour (foc ? legend : legendDim);
    g.drawText (juce::String (i + 1), juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 1.0f, 18.0f, 13.0f),
                juce::Justification::centredLeft, false);
    drawLed (g, { r.getRight() - 17.0f, r.getY() + 7.5f }, 2.6f, ledAmber, foc ? 1.0f : 0.0f);

    drawJack (g, inJack (r), 6.0f);
    drawJack (g, outJack (r), 6.0f);

    if (! s.loaded)
    {
        // 空白挡板：中间一个刻出来的 +，悬停时提示能装插件
        const auto c = juce::Point<float> (r.getCentreX(), r.getY() + r.getHeight() * 0.4f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (circleBox (c.translated (0.0f, 0.8f), 10.0f), 1.4f);
        g.setColour ((hov ? legend : legendFaint).withAlpha (0.8f));
        g.drawEllipse (circleBox (c, 10.0f), 1.2f);
        theme::drawIcon (g, theme::Icon::plus, circleBox (c, 6.5f), hov ? legend : legendFaint, 1.6f);

        g.setFont (font (10.5f, Weight::bold));
        g.setColour (hov ? legend : legendFaint);
        g.drawText (hov ? "点击装载"_u8 : "空位"_u8, juce::Rectangle<float> (r.getX(), c.y + 13.0f, r.getWidth(), 14.0f),
                    juce::Justification::centred, false);
        return;
    }

    const bool active = ! s.bypassed;

    // —— 美纹纸 + 手写的插件名。每块纸稍微歪一点点（按链位固定），像真的手贴上去的
    {
        const auto tape = juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 17.0f, r.getWidth() - 16.0f, 25.0f);
        const float tilt = ((float) ((i * 37) % 7) - 3.0f) * 0.0045f;
        const auto  xf   = juce::AffineTransform::rotation (tilt, tape.getCentreX(), tape.getCentreY());

        juce::Path paper;
        // 两头撕出来的毛边
        paper.startNewSubPath (tape.getX() + 1.0f, tape.getY());
        paper.lineTo (tape.getRight() - 1.0f, tape.getY());
        for (int k = 0; k <= 6; ++k)
            paper.lineTo (tape.getRight() + ((k % 2) ? -1.6f : 0.6f), tape.getY() + tape.getHeight() * (float) k / 6.0f);
        paper.lineTo (tape.getX() + 1.0f, tape.getBottom());
        for (int k = 6; k >= 0; --k)
            paper.lineTo (tape.getX() + ((k % 2) ? 1.6f : -0.6f), tape.getY() + tape.getHeight() * (float) k / 6.0f);
        paper.closeSubPath();

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillPath (paper, xf.translated (0.0f, 1.2f));
        g.setColour (juce::Colour (0xffe9dfbf).withMultipliedBrightness (active ? 1.0f : 0.72f));
        g.fillPath (paper, xf);

        // 功能色：纸左端一小块彩色胶带
        g.setColour (s.colour.withMultipliedSaturation (0.85f).withAlpha (active ? 0.95f : 0.45f));
        g.fillRect (juce::Rectangle<float> (tape.getX() + 2.0f, tape.getY() + 3.0f, 5.0f, tape.getHeight() - 6.0f).transformedBy (xf));

        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (xf);
        g.setColour (juce::Colour (0xff1f2c6e).withAlpha (active ? 1.0f : 0.55f));   // 蓝色记号笔
        g.setFont (scriptFont (13.0f));
        g.drawFittedText (s.name, tape.withTrimmedLeft (10.0f).withTrimmedRight (3.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.7f);
    }

    // —— 功能类别（功能色）/ 已旁通（琥珀）
    {
        const auto row = juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 45.0f, r.getWidth() - 16.0f, 14.0f);
        g.setFont (font (10.5f, Weight::bold));
        g.setColour (active ? s.colour.brighter (0.25f) : ledAmber);
        g.drawText (active ? s.tag : "已旁通"_u8, row, juce::Justification::centredLeft, true);
    }

    // —— IN 键：灯亮 = 在工作
    {
        const auto key = powerRect (r);
        const bool kh  = hov && hoverPart == Part::power;
        const auto top = drawKey (g, key, ! active, kh, rubber, 3.0f, true);
        drawLed (g, { top.getX() + 7.0f, top.getCentreY() }, 2.3f, ledGreen, active ? 1.0f : 0.0f);
        g.setFont (legendFont (9.0f, true));
        g.setColour (legend);
        g.drawText ("IN", top.withTrimmedLeft (11.0f), juce::Justification::centred, false);
    }

    if (! active)
    {
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 4.0f);
    }
}

void HwChainRack::paintGuardModule (juce::Graphics& g)
{
    using namespace hw;

    const auto r   = guardRect;
    const bool hov = (hoverPart == Part::guardBody || hoverPart == Part::guardPower);

    paintPlate (g, r, false, false, hov);

    g.setFont (font (10.5f, Weight::bold));
    g.setColour (guard.nanWarn ? vfdRed : legend);
    g.drawText (guard.nanWarn ? "NaN 已清零"_u8 : "输出保护"_u8,
                juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 1.0f, 90.0f, 13.0f), juce::Justification::centredLeft, false);

    // 拨杆：上 = 开
    {
        const auto t = powerRect (r);
        drawBatToggle (g, t.withTrimmedBottom (10.0f), guard.on ? 1.0f : 0.0f, true);
        g.setFont (legendFont (8.0f, true));
        g.setColour (legendDim);
        g.drawText ("LIMIT", t.withTrimmedTop (t.getHeight() - 11.0f), juce::Justification::centred, false);
    }

    // 增益衰减灯（1 / 3 / 6 / 12 dB）+ NaN 灯
    {
        const float x0 = r.getX() + 50.0f;
        const float y0 = r.getY() + 26.0f;
        static const float steps[] = { 1.0f, 3.0f, 6.0f, 12.0f };
        static const char* labels[] = { "1", "3", "6", "12" };

        g.setFont (legendFont (7.5f, true));
        for (int k = 0; k < 4; ++k)
        {
            const float y  = y0 + (float) (3 - k) * 11.0f;
            const bool  on = guard.on && guard.grDb >= steps[k] - 0.5f;
            drawLed (g, { x0 + 4.0f, y }, 2.6f, k >= 2 ? ledRed : ledAmber, on ? 1.0f : 0.0f);
            g.setColour (legendDim);
            g.drawText (labels[k], juce::Rectangle<float> (x0 + 10.0f, y - 5.0f, 16.0f, 10.0f), juce::Justification::centredLeft, false);
        }

        g.setColour (legendFaint);
        g.drawText ("GR dB", juce::Rectangle<float> (x0 - 4.0f, y0 + 40.0f, 40.0f, 10.0f), juce::Justification::centredLeft, false);

        drawLed (g, { x0 + 36.0f, y0 + 4.0f }, 3.0f, ledRed, guard.nanWarn ? 1.0f : 0.0f);
        g.setColour (guard.nanWarn ? vfdRed : legendDim);
        g.drawText ("NaN", juce::Rectangle<float> (x0 + 26.0f, y0 + 10.0f, 22.0f, 10.0f), juce::Justification::centred, false);
    }

    drawJack (g, inJack (r), 6.0f);
    drawJack (g, outJack (r), 6.0f);
}

void HwChainRack::paint (juce::Graphics& g)
{
    using namespace hw;

    for (int i = 0; i < kMaxChain; ++i)
        paintRackModule (g, i);

    paintGuardModule (g);

    // 面板上的总输入 / 总输出插孔
    for (auto [node, label] : { std::pair { inNode, "IN" }, std::pair { outNode, "OUT" } })
    {
        drawJack (g, node, 7.0f);
        g.setFont (legendFont (8.5f, true));
        g.setColour (legendDim);
        g.drawText (label, juce::Rectangle<float> (30.0f, 11.0f).withCentre (node.translated (0.0f, -14.0f)),
                    juce::Justification::centred, false);
    }

    // 跳线压在面板上面（插在前面板的插孔里）。颜色跟着信号走。
    juce::Colour sig = theme::audio;
    auto from = inNode;

    for (int i = 0; i < kMaxChain; ++i)
    {
        const auto& s = slots[(size_t) i];
        inColour[(size_t) i] = sig;

        drawPatchCable (g, from, inJack (rects[(size_t) i]), sig.withMultipliedSaturation (0.85f), 16.0f, 4.5f);

        if (s.loaded && ! s.bypassed)
            sig = s.colour;

        from = outJack (rects[(size_t) i]);
    }

    drawPatchCable (g, from, inJack (guardRect), sig.withMultipliedSaturation (0.85f), 16.0f, 4.5f);
    drawPatchCable (g, outJack (guardRect), outNode, sig.withMultipliedSaturation (0.85f), 12.0f, 4.5f);
}

//==============================================================================
HwMorphScope::HwMorphScope (const SnapshotBank& b)
    : MorphPad (b)
{
    setOpaque (false);
}

juce::Point<float> HwMorphScope::handlePoint() const
{
    const auto r = padArea();

    if (mode == MorphMode::TwoD)
        return { r.getX() + posX * r.getWidth(), r.getY() + posY * r.getHeight() };

    return { r.getX() + posX * r.getWidth(), r.getBottom() - 22.0f };
}

void HwMorphScope::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    trail.erase (std::remove_if (trail.begin(), trail.end(),
                                 [now] (const TrailPoint& t) { return now - t.t > kTrailMs; }),
                 trail.end());

    if (trail.size() <= 1)
        stopTimer();

    repaint();
}

void HwMorphScope::paint (juce::Graphics& g)
{
    using namespace hw;

    const auto b = getLocalBounds().toFloat();
    paintGlass (g, b, crtGlass, crt, 5.0f);

    // 余辉：位置变了才记一笔
    {
        if (mode != trailMode)
        {
            trail.clear();
            trailMode = mode;
        }

        const auto hp = handlePoint();
        if (trail.empty() || trail.back().p.getDistanceFrom (hp) > 0.5f)
        {
            trail.push_back ({ hp, juce::Time::getMillisecondCounter() });
            if (trail.size() > 48)
                trail.erase (trail.begin());
            if (trail.size() > 1 && ! isTimerRunning())
                startTimerHz (30);
        }
    }

    auto full = getLocalBounds();
    auto readout = full.removeFromBottom (kReadoutH + kMargin / 2).reduced (kMargin, 0);
    const auto r = padArea();

    if (r.getWidth() < 8.0f || r.getHeight() < 8.0f)
        return;

    paintGraticule (g, mode == MorphMode::TwoD ? r : b.reduced ((float) kMargin * 0.5f).withTrimmedBottom ((float) kReadoutH));

    if (mode == MorphMode::TwoD)
        paintScopeTwoD (g, r);
    else
        paintScopeOneD (g, r);

    if (bank.numUsed() < 2)
    {
        // 没法插值：屏幕上只亮一行字，像示波器没信号时的提示
        const auto area = (mode == MorphMode::TwoD ? r : b.reduced ((float) kMargin)).withTrimmedBottom (mode == MorphMode::TwoD ? 0.0f : (float) kReadoutH);
        g.setColour (crtGlass.withAlpha (0.82f));
        g.fillRoundedRectangle (area, 4.0f);

        const auto lines = juce::StringArray::fromLines (bank.numUsed() == 0
            ? "还没有快照\n随机到喜欢的声音，按上面的数字键存起来"_u8
            : "只有 1 个快照\n再存一个才能在它们之间插值"_u8);

        drawGlowText (g, lines[0], area.withHeight (20.0f).withCentre ({ area.getCentreX(), area.getCentreY() - 10.0f }),
                      crt, font (13.0f, Weight::bold), juce::Justification::centred, true, 0.6f);
        g.setFont (font (11.5f));
        g.setColour (crt.withAlpha (0.6f));
        g.drawFittedText (lines[1], area.withHeight (34.0f).withCentre ({ area.getCentreX(), area.getCentreY() + 16.0f }).toNearestInt(),
                          juce::Justification::centred, 2, 0.85f);
    }
    else
    {
        paintTrail (g);
        paintWeights (g, readout.toFloat());
    }

    paintSpot (g, handlePoint(), dragging ? 6.0f : 5.0f, bank.numUsed() >= 2 ? 1.0f : 0.35f);

    // 扫描线 + 玻璃反光
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (g.getClipBounds().getIntersection (getLocalBounds()));
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        const auto clip = g.getClipBounds();
        for (int y = clip.getY() - (clip.getY() % 3); y < clip.getBottom(); y += 3)
            g.fillRect (clip.getX(), y, clip.getWidth(), 1);
    }
    drawGlassSheen (g, b, 5.0f, 1.1f);
}

void HwMorphScope::paintGraticule (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace hw;

    // 示波器刻度：8×8 格，中间十字带小刻度
    g.setColour (crt.withAlpha (0.08f));
    for (int k = 1; k < 8; ++k)
    {
        const float x = r.getX() + r.getWidth() * (float) k / 8.0f;
        const float y = r.getY() + r.getHeight() * (float) k / 8.0f;
        g.fillRect (juce::Rectangle<float> (x - 0.5f, r.getY(), 1.0f, r.getHeight()));
        g.fillRect (juce::Rectangle<float> (r.getX(), y - 0.5f, r.getWidth(), 1.0f));
    }

    g.setColour (crt.withAlpha (0.2f));
    g.fillRect (juce::Rectangle<float> (r.getCentreX() - 0.5f, r.getY(), 1.0f, r.getHeight()));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getCentreY() - 0.5f, r.getWidth(), 1.0f));

    for (int k = 1; k < 40; ++k)
    {
        const float x = r.getX() + r.getWidth() * (float) k / 40.0f;
        const float y = r.getY() + r.getHeight() * (float) k / 40.0f;
        g.fillRect (juce::Rectangle<float> (x - 0.5f, r.getCentreY() - 2.0f, 1.0f, 4.0f));
        g.fillRect (juce::Rectangle<float> (r.getCentreX() - 2.0f, y - 0.5f, 4.0f, 1.0f));
    }

    g.setColour (crt.withAlpha (0.16f));
    g.drawRect (r, 1.0f);
}

void HwMorphScope::paintScopeOneD (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace hw;

    const float trackY = r.getBottom() - 22.0f;
    const auto  plot   = juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(), juce::jmax (0.0f, trackY - 18.0f - r.getY()));

    int used[kNumSlots];
    int nUsed = 0;
    for (int i = 0; i < kNumSlots; ++i)
        if (bank.isUsed (i))
            used[nUsed++] = i;

    // 每个快照一条权重曲线（磷光迹线）——和 computeWeights 的 1D 分段线性同源
    if (plot.getHeight() > 24.0f && nUsed >= 2)
    {
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
                if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
            }

            g.setColour (crt.withAlpha (0.12f));
            g.strokePath (curve, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (crt.withAlpha (0.85f));
            g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // 曲线顶点上标快照号
            const float t = (nUsed == 1) ? 0.5f : (float) k / (float) (nUsed - 1);
            g.setFont (legendFont (10.0f, true));
            g.setColour (crt);
            g.drawText (juce::String (s + 1), juce::Rectangle<float> (16.0f, 12.0f).withCentre ({ r.getX() + t * r.getWidth(), inner.getY() - 7.0f }),
                        juce::Justification::centred, false);
        }

        const float hx = r.getX() + posX * r.getWidth();
        g.setColour (crt.withAlpha (0.4f));
        g.fillRect (juce::Rectangle<float> (hx - 0.5f, plot.getY() + 4.0f, 1.0f, plot.getHeight() - 8.0f));
    }

    // 扫描基线 + 快照落点
    g.setColour (crt.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (r.getX(), trackY - 0.5f, r.getWidth(), 1.0f));

    for (int i = 0; i < nUsed; ++i)
    {
        const float t = (nUsed == 1) ? 0.5f : (float) i / (float) (nUsed - 1);
        const juce::Point<float> c { r.getX() + t * r.getWidth(), trackY };

        g.setColour (crt.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (c.x - 0.75f, c.y - 6.0f, 1.5f, 12.0f));
        g.setFont (legendFont (10.0f, true));
        g.drawText (juce::String (used[i] + 1), juce::Rectangle<float> (16.0f, 12.0f).withCentre (c.translated (0.0f, 14.0f)),
                    juce::Justification::centred, false);
    }
}

void HwMorphScope::paintScopeTwoD (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace hw;

    const juce::Point<float> corner[kNumSlots] =
    {
        { r.getX(),     r.getY()      },
        { r.getRight(), r.getY()      },
        { r.getX(),     r.getBottom() },
        { r.getRight(), r.getBottom() }
    };

    const auto handle = handlePoint();
    const auto w = bank.computeWeights (MorphMode::TwoD, posX, posY);

    // 手柄到每个有权重的角拉一道光束，亮度 / 粗细跟着权重
    for (int s = 0; s < kNumSlots; ++s)
    {
        const float weight = w.weightOf (s);
        if (weight <= 0.001f)
            continue;

        juce::Path beam;
        beam.startNewSubPath (handle);
        beam.lineTo (corner[s]);
        g.setColour (crt.withAlpha (0.08f + 0.18f * weight));
        g.strokePath (beam, juce::PathStrokeType (3.0f + 5.0f * weight));
        g.setColour (crt.withAlpha (0.25f + 0.6f * weight));
        g.strokePath (beam, juce::PathStrokeType (0.8f + 1.4f * weight));
    }

    for (int s = 0; s < kNumSlots; ++s)
    {
        const bool used = bank.isUsed (s);
        const bool win  = used && bank.getWinner() == s;
        const auto c    = corner[s];

        // 角上的落点：用了的是亮点，没用的是一个空心小框
        if (used)
            paintSpot (g, c, win ? 4.5f : 3.5f, win ? 1.0f : 0.7f);
        else
        {
            g.setColour (crt.withAlpha (0.25f));
            g.drawRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c), 1.0f);
        }

        const bool left = (s == 0 || s == 2);
        const bool top  = (s == 0 || s == 1);
        const auto label = juce::Rectangle<float> (left ? r.getX() + 8.0f : r.getRight() - 8.0f - 16.0f,
                                                   top  ? r.getY() + 5.0f : r.getBottom() - 5.0f - 12.0f, 16.0f, 12.0f);
        g.setFont (legendFont (10.5f, true));
        g.setColour (crt.withAlpha (used ? 0.9f : 0.3f));
        g.drawText (juce::String (s + 1), label, left ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }
}

void HwMorphScope::paintSpot (juce::Graphics& g, juce::Point<float> c, float radius, float intensity)
{
    using namespace hw;

    g.setGradientFill (juce::ColourGradient (crt.withAlpha (0.55f * intensity), c.x, c.y,
                                             crt.withAlpha (0.0f), c.x + radius * 3.5f, c.y, true));
    g.fillEllipse (circleBox (c, radius * 3.5f));
    g.setColour (crt.withAlpha (intensity));
    g.fillEllipse (circleBox (c, radius));
    g.setColour (juce::Colours::white.withAlpha (0.8f * intensity));
    g.fillEllipse (circleBox (c, radius * 0.45f));
}

void HwMorphScope::paintTrail (juce::Graphics& g)
{
    if (trail.size() < 2)
        return;

    const auto now = juce::Time::getMillisecondCounter();

    // 最后一个点就是手柄本身，单独画；前面的按年龄淡出
    for (size_t i = 0; i + 1 < trail.size(); ++i)
    {
        const float age = (float) (now - trail[i].t) / (float) kTrailMs;
        if (age >= 1.0f)
            continue;

        const float a = (1.0f - age) * 0.55f;
        g.setColour (hw::crt.withAlpha (a));
        g.drawLine ({ trail[i].p, trail[i + 1].p }, 2.0f + 2.0f * (1.0f - age));
    }
}

void HwMorphScope::paintWeights (juce::Graphics& g, juce::Rectangle<float> strip)
{
    using namespace hw;

    const auto w = bank.computeWeights (mode, posX, posY);

    juce::String txt;
    for (int s = 0; s < kNumSlots; ++s)
        if (const float weight = w.weightOf (s); weight > 0.005f)
            txt << (txt.isEmpty() ? "" : "   ") << (s + 1) << ":" << juce::roundToInt (weight * 100.0f) << "%";

    drawGlowText (g, txt, strip, crt, monoFont (12.0f), juce::Justification::centred, false, 0.5f);
}

} // namespace pr
