#include "GridEditor.h"

namespace pr
{

using theme::Weight;
using theme::Icon;

namespace
{
    void styleLabel (juce::Label& l, float pt, juce::Colour c, Weight w = Weight::regular,
                     juce::Justification j = juce::Justification::centredLeft)
    {
        l.setFont (theme::font (pt, w));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (j);
        l.setInterceptsMouseClicks (false, false);
        l.setMinimumHorizontalScale (0.85f);
    }
}

//==============================================================================
void GridEditor::Backdrop::setLayout (juce::Rectangle<int> h, juce::Rectangle<int> s,
                                      juce::Rectangle<int> cl, std::vector<Card> c)
{
    header = h;
    status = s;
    chainLabel = cl;
    cards = std::move (c);
    repaint();
}

void GridEditor::Backdrop::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto b = getLocalBounds();

    g.fillAll (canvas);
    fillDotGrid (g, b.withTrimmedTop (header.getBottom()).withTrimmedBottom (status.getHeight()));

    // —— 顶栏 / 状态栏
    g.setColour (bar);
    g.fillRect (header);
    g.fillRect (status);
    g.setColour (stroke);
    g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);
    g.fillRect (status.getX(), status.getY(), status.getWidth(), 1);

    // —— 品牌：一块橙色圆角方块里画个骰子面，旁边是名字
    {
        auto r = header.reduced (gap, 0);
        const auto logo = juce::Rectangle<float> (28.0f, 28.0f).withCentre ({ (float) r.getX() + 14.0f, (float) r.getCentreY() });
        g.setColour (accent);
        g.fillRoundedRectangle (logo, 8.0f);

        juce::Path pips;
        for (auto p : { juce::Point<float> { 0.3f, 0.3f }, { 0.7f, 0.3f }, { 0.5f, 0.5f }, { 0.3f, 0.7f }, { 0.7f, 0.7f } })
            pips.addEllipse (logo.getX() + p.x * logo.getWidth() - 2.2f, logo.getY() + p.y * logo.getHeight() - 2.2f, 4.4f, 4.4f);
        g.setColour (ink);
        g.fillPath (pips);

        g.setColour (text);
        g.setFont (font (15.5f, Weight::bold));
        g.drawText ("ParamRoulette", juce::Rectangle<int> (r.getX() + 38, r.getY(), 150, r.getHeight() / 2 + 3),
                    juce::Justification::bottomLeft, false);

        g.setColour (textFaint);
        g.setFont (font (10.5f, Weight::medium));
        const auto arch = sizeof (void*) == 4 ? "x86 · VST2/VST3"_u8 : "x64 · VST3/VST2"_u8;
        g.drawText (arch, juce::Rectangle<int> (r.getX() + 39, r.getCentreY() + 5, 150, 14),
                    juce::Justification::topLeft, false);
    }

    // —— 信号链标题
    if (! chainLabel.isEmpty())
    {
        auto r = drawCardTitle (g, chainLabel.withWidth (100), "信号链"_u8, juce::Colour (0xffd8d8d8));
        juce::ignoreUnused (r);
        g.setColour (textFaint);
        g.setFont (font (11.0f));
        g.drawText ("IN → 1 … 6 → 输出保护 → OUT，串联"_u8,
                    chainLabel.withTrimmedLeft (96), juce::Justification::centredLeft, true);
    }

    // —— 卡片
    for (const auto& c : cards)
    {
        drawCard (g, c.bounds.toFloat(), rCard);
        drawCardTitle (g, c.bounds.reduced (14, 0).withTrimmedTop (10).withHeight (26), c.title, c.colour);
    }
}

//==============================================================================
GridEditor::GridEditor (ParamRouletteProcessor& p, EditorHost& h)
    : EditorBase (p, h, h.lookAndFeelFor (Skin::grid)),
      pad (p.getBank())
{
    addAndMakeVisible (backdrop);

    //== 顶栏 ====================================================================
    ui::icon (bPlugin, Icon::chevronDown);
    ui::alignLeft (bPlugin);
    bPlugin.setTooltip ("当前链位的插件。点开换一个（装到当前聚焦的链位上）。"_u8);
    bPlugin.onClick = [this] { showPluginMenu (proc.getFocusedSlot(), &bPlugin); };
    addAndMakeVisible (bPlugin);

    cbPreset.setTextWhenNoChoicesAvailable ("（无 preset）"_u8);
    cbPreset.setTextWhenNothingSelected ("preset"_u8);
    cbPreset.setTooltip ("子插件的 program。切换会清空运动模式的基座。"_u8);
    cbPreset.onChange = [this] { selectPreset (cbPreset.getSelectedItemIndex()); };
    addAndMakeVisible (cbPreset);

    for (auto [b, ic, tip] : { std::tuple { &bUI,     Icon::window, "打开 / 关闭当前链位插件的界面（也可以双击信号链里的模块）"_u8 },
                                std::tuple { &bUnload, Icon::eject,  "把当前链位的插件卸掉"_u8 },
                                std::tuple { &bSkin,   Icon::knob,   "换成音频硬件拟物风的界面（藏青面板、旋钮、VU 表）。功能完全一样，随时可以换回来"_u8 },
                                std::tuple { &bScan,   Icon::scan,   "扫描插件（可以增删搜索路径）"_u8 },
                                std::tuple { &bTags,   Icon::tag,    "给插件打功能标签。信号链上模块和连线的颜色就来自这里"_u8 } })
    {
        ui::variant (*b, ui::Variant::ghost);
        ui::icon (*b, ic);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    }

    bUI.onClick     = [this] { toggleHostedWindow(); };
    bUnload.onClick = [this] { doUnload (proc.getFocusedSlot()); };
    bSkin.onClick   = [this] { host.requestSkin (Skin::hardware); };
    bScan.onClick   = [this] { showScanWindow(); };
    bTags.onClick   = [this] { showTagWindow(); };

    //== 信号链 ==================================================================
    chain.onFocus       = [this] (int i) { proc.setFocusedSlot (i); };
    chain.onAdd         = [this] (int i) { showPluginMenu (i, nullptr); };
    chain.onBypass      = [this] (int i, bool b) { setSlotBypass (i, b); };
    chain.onOpenEditor  = [this] (int i) { host.openHostedWindow (i); };
    chain.onContextMenu = [this] (int i) { showModuleMenu (i); };
    chain.onGuardToggle = [this] (bool on) { setGuardOn (on); };
    addAndMakeVisible (chain);

    for (auto [b, ic, tip] : { std::tuple { &bChainSave, Icon::save, "把整条链（每位的插件 + 状态 + 勾选表）存成文件，在别的工程里也能读回来"_u8 },
                                std::tuple { &bChainLoad, Icon::open, "读一条存过的链。会替换当前整条链，但不动随机化强度 / morph 位置 / 语义这些当前设置"_u8 } })
    {
        ui::variant (*b, ui::Variant::ghost);
        ui::icon (*b, ic);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    }
    bChainSave.onClick = [this] { promptSaveChainPreset(); };
    bChainLoad.onClick = [this] { showChainPresetMenu (bChainLoad); };

    //== 随机化 ==================================================================
    wireKnob (kAmount, *proc.pAmount, "强度"_u8, theme::accent);
    kAmount.setTooltip ("0 = 不动，1 = 完全重掷；中间值是从当前值往随机目标挪这么多。双击回到 100%"_u8);
    kAmount.setDoubleClickReturnValue (true, 1.0);

    wireRange();

    stepMax.setRange (0, proc.pMaxParams->getRange().getEnd());
    stepMax.setValue (proc.pMaxParams->get());
    // 0 不该显示成 "0"——那看着像"一个都不动"。它的意思是"不限制"。
    stepMax.textFor = [] (int v) { return v <= 0 ? juce::String ("只动：不限"_u8) : "只动 "_u8 + juce::String (v) + " 个"_u8; };
    stepMax.onChange = [this] (int v) { setMaxParams (v); };
    stepMax.setTooltip ("每次最多改动几个参数。只动 1~2 个时每次改动都能听出是哪一维在起作用。"_u8
                        "运动模式下只限制音色层。滚轮可调，双击回到「不限」"_u8);
    addAndMakeVisible (stepMax);

    ui::variant (bProbe, ui::Variant::ghost);
    ui::icon (bProbe, Icon::probe);
    bProbe.setTooltip ("体检：问出每个参数到底收什么值，认出只收端点的伪开关。会出声！"_u8);
    bProbe.onClick = [this] { confirmProbe(); };
    addAndMakeVisible (bProbe);

    ui::variant (bRoll, ui::Variant::primary);
    ui::icon (bRoll, Icon::dice);
    bRoll.onClick = [this] { doRoll(); };
    addAndMakeVisible (bRoll);

    for (auto [b, ic, back] : { std::tuple { &bHistBack, Icon::prev, true }, std::tuple { &bHistFwd, Icon::next, false } })
    {
        ui::variant (*b, ui::Variant::ghost);
        ui::icon (*b, ic);
        b->setTooltip (back ? "回到上一个状态（Ctrl+Z）。最多记 20 步，只在内存里，不进工程"_u8
                            : "前进到下一个状态（Ctrl+Y）"_u8);
        b->onClick = [this, back] { doHistory (back); };
        addAndMakeVisible (*b);
    }

    styleLabel (lHistory, 11.5f, theme::textDim, Weight::medium, juce::Justification::centred);
    addAndMakeVisible (lHistory);

    styleLabel (lResult, 11.5f, theme::textDim, Weight::regular, juce::Justification::topLeft);
    addAndMakeVisible (lResult);

    //== 运动语义 ================================================================
    ui::asSwitch (tbMotion);
    ui::tint (tbMotion, theme::motion);
    tbMotion.setTooltip ("运动模式：按「上扬 / 下降 / 先扬后抑」这种音乐意图生成变体，"_u8
                         "而不是逐参数掷骰子。打开后大按钮变成「生成变体」"_u8);
    tbMotion.onClick = [this] { setMotionOn (tbMotion.getToggleState()); };
    addAndMakeVisible (tbMotion);

    {
        std::vector<SegmentedControl::Item> items;
        for (int i = 0; i < kNumMotionSemantics; ++i)
        {
            const auto sem = (MotionSemantic) i;
            items.push_back ({ motionSemanticLabel (sem), motionSemanticLabel (sem) + "："_u8 + semanticHint (sem),
                               [sem] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
                               { theme::drawSemanticGlyph (g, sem, r, c, 1.8f); } });
        }
        segSemantic.setItems (std::move (items));
    }
    segSemantic.setAccent (theme::motion);
    segSemantic.onChange = [this] (int i) { setSemantic (i); };
    addAndMakeVisible (segSemantic);

    styleLabel (lSemantic, 11.5f, theme::textDim);
    addAndMakeVisible (lSemantic);

    wireKnob (kMotion, *proc.pMotionAmt, "运动"_u8, theme::motion);
    kMotion.setTooltip ("运动层强度：包络深度、方向锚点拉多远、曲线撑多满。可以在 DAW 里画自动化"_u8);
    kMotion.setDoubleClickReturnValue (true, 0.65);

    wireKnob (kTone, *proc.pToneAmt, "音色"_u8, theme::motion);
    kTone.setTooltip ("音色层：围绕基座的随机抖动幅度。逐参数的幅度在列表的「幅度」列里设"_u8);
    kTone.setDoubleClickReturnValue (true, 0.35);

    ui::icon (bLockBase, Icon::pin);
    bLockBase.setTooltip ("把当前状态存成生成变体的出发点。已设定时会覆盖旧的（#N 会 +1）"_u8);
    bLockBase.onClick = [this] { lockBase(); };
    addAndMakeVisible (bLockBase);

    ui::icon (bRestoreBase, Icon::restore);
    bRestoreBase.setTooltip ("精确回到基座（用子插件的完整状态块还原）"_u8);
    bRestoreBase.onClick = [this] { restoreBase(); };
    addAndMakeVisible (bRestoreBase);

    styleLabel (lBase, 11.5f, theme::motion, Weight::medium);
    addAndMakeVisible (lBase);

    styleLabel (lProfile, 11.0f, theme::textFaint, Weight::regular, juce::Justification::topLeft);
    addAndMakeVisible (lProfile);

    //== 参数 ====================================================================
    styleLabel (lCount, 11.5f, theme::textDim, Weight::medium);
    addAndMakeVisible (lCount);

    teFilter.getProperties().set ("prSearch", true);
    teFilter.setIndents (32, 0);
    teFilter.setJustification (juce::Justification::centredLeft);
    teFilter.setFont (theme::font (12.5f));
    teFilter.setMultiLine (false);
    teFilter.setReturnKeyStartsNewLine (false);
    teFilter.setTextToShowWhenEmpty ("筛选参数名（空格分隔，任一命中）   Ctrl+F"_u8, theme::textFaint);
    teFilter.setText (proc.filterText, false);
    teFilter.onTextChange = [this] { applyFilter (teFilter.getText(), bOnlyMatching.getToggleState()); };
    teFilter.onEscapeKey  = [this]
    {
        teFilter.clear();
        applyFilter (teFilter.getText(), bOnlyMatching.getToggleState());
        grabKeyboardFocus();
    };
    addAndMakeVisible (teFilter);

    ui::variant (bOnlyMatching, ui::Variant::chip);
    bOnlyMatching.setClickingTogglesState (true);
    bOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);
    bOnlyMatching.setTooltip ("只显示名字匹配关键词的参数"_u8);
    bOnlyMatching.onClick = [this] { applyFilter (teFilter.getText(), bOnlyMatching.getToggleState()); };
    addAndMakeVisible (bOnlyMatching);

    segView.setItems ({ { "全部"_u8,   "显示全部参数"_u8, {} },
                        { "已勾选"_u8, "只看勾选了的——回答「我到底勾了哪些」"_u8, {} },
                        { "已改动"_u8, "只看上一次操作真的改动了的"_u8, {} } });
    segView.onChange = [this] (int i)
    {
        list.setViewMode (i == 1 ? ParamListView::ViewMode::CheckedOnly
                        : i == 2 ? ParamListView::ViewMode::ChangedOnly
                                 : ParamListView::ViewMode::All);
        updateStatus();
    };
    addAndMakeVisible (segView);

    for (auto [b, ic, tip] : { std::tuple { &bCheckMatch,   Icon::check, "把全部匹配关键词的参数勾上（包括被「仅匹配」藏起来的）"_u8 },
                                std::tuple { &bUncheckMatch, Icon::close, "把全部匹配关键词的参数取消勾选——「凡是叫这个的都别动」"_u8 } })
    {
        ui::variant (*b, ui::Variant::ghost);
        ui::icon (*b, ic);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    }
    bCheckMatch  .onClick = [this] { checkMatching (true); };
    bUncheckMatch.onClick = [this] { checkMatching (false); };

    ui::variant (bListMenu, ui::Variant::ghost);
    ui::icon (bListMenu, Icon::more);
    bListMenu.setTooltip ("全选 / 全不选 / 反选（只作用于当前可见的行）"_u8);
    bListMenu.onClick = [this] { showListMenu (bListMenu); };
    addAndMakeVisible (bListMenu);

    list.onChecksChanged = [this]
    {
        proc.saveExclusionsNow();
        updateStatus();
    };
    list.onSpanChanged = [this] (int modelIndex, float span)
    {
        proc.setParamSpan (modelIndex, span);   // 内部会持久化
        updateStatus();
    };
    addAndMakeVisible (list);

    //== 快照 / morph ============================================================
    segMorphMode.setItems ({ { "1D", "1D 滑条：已用快照按序号排成一条线"_u8, {} },
                             { "2D", "2D 平面：四角固定绑快照 1/2/3/4，双线性插值"_u8, {} } });
    segMorphMode.setAccent (theme::morph);
    segMorphMode.setSelected (proc.pMorphMode->getIndex());
    segMorphMode.onChange = [this] (int idx)
    {
        if (syncing)
            return;

        setMorphModeIndex (idx);
        pad.setMode (idx == 1 ? MorphMode::TwoD : MorphMode::OneD);
    };
    addAndMakeVisible (segMorphMode);

    tiles.onStore  = [this] (int i) { storeSnapshot (i); };
    tiles.onRecall = [this] (int i) { recallSnapshot (i); };
    tiles.onClear  = [this] (int i) { clearSnapshot (i); };
    addAndMakeVisible (tiles);

    pad.setMode (proc.getMorphMode());
    pad.setPosition (proc.pMorphX->get(), proc.pMorphY->get());
    pad.onDragStart       = [this] { morphDragStart(); };
    pad.onPositionChanged = [this] (float x, float y) { morphMoved (x, y); };
    pad.onDragEnd         = [this] { morphDragEnd(); };
    addAndMakeVisible (pad);

    //== 状态栏 ==================================================================
    styleLabel (lStatus, 11.5f, theme::textDim);
    addAndMakeVisible (lStatus);

    styleLabel (lHints, 11.0f, theme::textFaint, Weight::regular, juce::Justification::centredRight);
    lHints.setText ("R 随机   ·   Ctrl+Z / Ctrl+Y 历史   ·   1–4 召回   ·   Shift+1–4 存入   ·   Ctrl+F 筛选"_u8,
                    juce::dontSendNotification);
    addAndMakeVisible (lHints);
}

GridEditor::~GridEditor() = default;

//==============================================================================
void GridEditor::wireKnob (juce::Slider& s, juce::AudioParameterFloat& p,
                           const juce::String& label, juce::Colour c)
{
    s.setName (label);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (180);
    s.setRange (0.0, 1.0, 0.001);
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
    s.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    s.setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);

    bindSlider (s, p);
    addAndMakeVisible (s);
}

void GridEditor::wireRange()
{
    // 下限 / 上限做成一根双滑块：两个数本来就是一对，分成两根滑条的时候
    // 很容易拖出 下限 > 上限 的组合（虽然 normalise 会兜住，但界面上看着就是错的）。
    sRange.setName ("取值范围"_u8);
    sRange.setSliderStyle (juce::Slider::TwoValueHorizontal);
    sRange.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sRange.setRange (0.0, 1.0, 0.001);
    sRange.setColour (juce::Slider::trackColourId, theme::accent);
    sRange.setMinAndMaxValues (proc.pRangeLo->get(), proc.pRangeHi->get(), juce::dontSendNotification);
    sRange.setTooltip ("随机目标的取值区间（归一化 0..1）。拖两个圆点分别改下限 / 上限"_u8);

    sRange.onDragStart = [this]
    {
        proc.pRangeLo->beginChangeGesture();
        proc.pRangeHi->beginChangeGesture();
    };
    sRange.onDragEnd = [this]
    {
        proc.pRangeLo->endChangeGesture();
        proc.pRangeHi->endChangeGesture();
    };
    sRange.onValueChange = [this]
    {
        if (syncing)
            return;

        proc.pRangeLo->setValueNotifyingHost ((float) sRange.getMinValue());
        proc.pRangeHi->setValueNotifyingHost ((float) sRange.getMaxValue());
        sRange.repaint();
    };

    addAndMakeVisible (sRange);
}

//==============================================================================
void GridEditor::paint (juce::Graphics&)
{
    // 全部画在 backdrop 里（缓存成图）。这里什么都不用画。
}

void GridEditor::resized()
{
    using theme::gap;

    auto r = getLocalBounds();
    backdrop.setBounds (r);

    auto header = r.removeFromTop (kHeaderH);
    auto status = r.removeFromBottom (kStatusH);
    r.reduce (gap, gap);

    //== 顶栏 ====================================================================
    {
        auto h = header.reduced (gap, 0);
        h.removeFromLeft (196);    // 品牌，画在 backdrop 里

        auto row = h.withSizeKeepingCentre (h.getWidth(), 30);
        bTags.setBounds (row.removeFromRight (72));
        row.removeFromRight (4);
        bScan.setBounds (row.removeFromRight (72));
        row.removeFromRight (16);

        bPlugin.setBounds (row.removeFromLeft (juce::jmin (260, row.getWidth() / 3)));
        row.removeFromLeft (8);
        cbPreset.setBounds (row.removeFromLeft (juce::jmin (200, row.getWidth() / 2)));
        row.removeFromLeft (8);
        bUI.setBounds (row.removeFromLeft (74));
        row.removeFromLeft (2);
        bUnload.setBounds (row.removeFromLeft (74));

        // 外观切换放在**左边那一组排完之后剩下的空里**，紧挨着扫描 / 标签。
        // 先排左边再排它，左边几个控件的宽度就和没有它的时候一模一样。
        bSkin.setBounds (row.removeFromRight (juce::jmin (100, juce::jmax (0, row.getWidth() - 8))));
    }

    //== 信号链 ==================================================================
    auto chainArea = r.removeFromTop (kChainH);
    auto chainLabel = chainArea.removeFromTop (26);
    {
        auto lr = chainLabel;
        bChainLoad.setBounds (lr.removeFromRight (72).withSizeKeepingCentre (72, 26));
        lr.removeFromRight (4);
        bChainSave.setBounds (lr.removeFromRight (72).withSizeKeepingCentre (72, 26));
    }
    chain.setBounds (chainArea);

    r.removeFromTop (gap);

    //== 三栏 ====================================================================
    // 两侧栏跟着窗口宽度缩放，中间的参数列表才不会在最小尺寸下被挤没。
    const int leftW  = juce::jlimit (268, kLeftW,  juce::roundToInt ((float) getWidth() * 0.245f));
    const int rightW = juce::jlimit (236, kRightW, juce::roundToInt ((float) getWidth() * 0.222f));

    auto left   = r.removeFromLeft (leftW);
    r.removeFromLeft (gap);
    auto right  = r.removeFromRight (rightW);
    r.removeFromRight (gap);
    auto center = r;

    auto randomCard = left.removeFromTop (kRandomH);
    left.removeFromTop (gap);
    auto motionCard = left;

    constexpr int kTitleH = 36;   // 卡片标题行（含上边距）
    constexpr int kPad    = 14;

    //-- 随机化 ------------------------------------------------------------------
    {
        auto c = randomCard.reduced (kPad, 0);
        auto title = c.removeFromTop (kTitleH).withTrimmedTop (10).withHeight (26);

        // 标题行右侧：历史 ‹ 3/12 ›
        bHistFwd .setBounds (title.removeFromRight (26));
        lHistory .setBounds (title.removeFromRight (44));
        bHistBack.setBounds (title.removeFromRight (26));

        c.removeFromTop (6);
        auto rowA = c.removeFromTop (80);
        kAmount.setBounds (rowA.removeFromLeft (70));
        rowA.removeFromLeft (10);

        auto rangeArea = rowA.removeFromTop (40);
        sRange.setBounds (rangeArea);
        rowA.removeFromTop (8);
        stepMax.setBounds (rowA.removeFromTop (28));

        c.removeFromTop (8);
        auto rollRow = c.removeFromTop (40);
        bProbe.setBounds (rollRow.removeFromRight (76));
        rollRow.removeFromRight (8);
        bRoll.setBounds (rollRow);
        c.removeFromTop (8);
        lResult.setBounds (c.withTrimmedBottom (8));
    }

    //-- 运动语义 ----------------------------------------------------------------
    {
        auto c = motionCard.reduced (kPad, 0);
        auto title = c.removeFromTop (kTitleH).withTrimmedTop (10).withHeight (26);
        tbMotion.setBounds (title.removeFromRight (40));

        c.removeFromTop (6);
        segSemantic.setBounds (c.removeFromTop (30));
        c.removeFromTop (4);
        lSemantic.setBounds (c.removeFromTop (18));
        c.removeFromTop (6);

        auto rowK = c.removeFromTop (80);
        kMotion.setBounds (rowK.removeFromLeft (68));
        kTone  .setBounds (rowK.removeFromLeft (68));
        rowK.removeFromLeft (8);

        auto baseCol = rowK;
        bLockBase   .setBounds (baseCol.removeFromTop (26));
        baseCol.removeFromTop (6);
        bRestoreBase.setBounds (baseCol.removeFromTop (26));
        baseCol.removeFromTop (4);
        lBase.setBounds (baseCol.removeFromTop (18));

        c.removeFromTop (8);
        lProfile.setBounds (c.withTrimmedBottom (10));
    }

    //-- 参数 --------------------------------------------------------------------
    {
        auto c = center.reduced (kPad, 0);
        auto title = c.removeFromTop (kTitleH).withTrimmedTop (10).withHeight (26);

        lCount.setBounds (title.withTrimmedLeft (64).withWidth (160));
        bListMenu.setBounds (title.removeFromRight (30));
        title.removeFromRight (6);
        segView.setBounds (title.removeFromRight (210));

        c.removeFromTop (6);
        auto tools = c.removeFromTop (30);
        bUncheckMatch.setBounds (tools.removeFromRight (92));
        tools.removeFromRight (2);
        bCheckMatch.setBounds (tools.removeFromRight (92));
        tools.removeFromRight (8);
        bOnlyMatching.setBounds (tools.removeFromRight (70));
        tools.removeFromRight (8);
        teFilter.setBounds (tools);

        c.removeFromTop (8);
        list.setBounds (c.withTrimmedBottom (8));
    }

    //-- 快照 / morph ------------------------------------------------------------
    {
        auto c = right.reduced (kPad, 0);
        auto title = c.removeFromTop (kTitleH).withTrimmedTop (10).withHeight (26);
        segMorphMode.setBounds (title.removeFromRight (88));

        c.removeFromTop (8);
        tiles.setBounds (c.removeFromTop (112));
        c.removeFromTop (8);
        pad.setBounds (c.withTrimmedBottom (6));
    }

    //== 状态栏 ==================================================================
    {
        auto s = status.reduced (gap, 0);
        lHints.setBounds (s.removeFromRight (juce::jmin (520, s.getWidth() / 2)));
        lStatus.setBounds (s);
    }

    backdrop.setLayout (header, status, chainLabel,
                        { { randomCard, "随机化"_u8,     theme::accent },
                          { motionCard, "运动语义"_u8,   theme::motion },
                          { center,     "参数"_u8,       juce::Colour (0xffd8d8d8) },
                          { right,      "快照 · Morph"_u8, theme::morph } });
}

//==============================================================================
void GridEditor::focusFilter()
{
    teFilter.grabKeyboardFocus();
    teFilter.selectAll();
}

void GridEditor::rebuildSkin()
{
    // **列表每次都要重新指一遍数据源**：聚焦的链位可能换了。上一版这里是构造时绑死的
    // 引用，切链位之后列表还在画旧插件的参数。
    list.setSource (proc.getModel(), proc.getChecked());

    // 恢复工程时 filter 是从状态里读回来的，界面要跟上。
    if (teFilter.getText() != proc.filterText)
        teFilter.setText (proc.filterText, false);

    bOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);

    list.setFilter (proc.filterText, proc.onlyMatching);
    list.rebuild();

    fillPresetBox (cbPreset);
}

void GridEditor::refreshSnapshotTiles()
{
    tiles.setTiles (makeSnapshotTiles(), proc.getBank().getWinner(), proc.getNumLoadedSlots() > 0);
    pad.repaint();
}

void GridEditor::refreshHistoryUI()
{
    const bool has = proc.getHosted() != nullptr;

    bHistBack.setEnabled (has && proc.canHistoryBack());
    bHistFwd .setEnabled (has && proc.canHistoryForward());
    lHistory.setText (historyText(), juce::dontSendNotification);
}

void GridEditor::syncMotionUI()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto s = proc.getMotionSettings();

    tbMotion.setToggleState (proc.isMotionMode(), juce::dontSendNotification);

    const int idx = juce::jlimit (0, kNumMotionSemantics - 1, (int) s.semantic);
    segSemantic.setSelected (idx);
    lSemantic.setText (motionSemanticLabel (s.semantic) + "  ·  "_u8 + semanticHint (s.semantic),
                       juce::dontSendNotification);
}

void GridEditor::syncFromParameters()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    // 用户正按着的控件不回写，否则自动化和手会互相拽。
    if (! kAmount.isMouseButtonDown()) kAmount.setValue (proc.pAmount->get(),    juce::dontSendNotification);
    if (! kMotion.isMouseButtonDown()) kMotion.setValue (proc.pMotionAmt->get(), juce::dontSendNotification);
    if (! kTone  .isMouseButtonDown()) kTone  .setValue (proc.pToneAmt->get(),   juce::dontSendNotification);

    if (! sRange.isMouseButtonDown())
        sRange.setMinAndMaxValues (juce::jmin (proc.pRangeLo->get(), proc.pRangeHi->get()),
                                   juce::jmax (proc.pRangeLo->get(), proc.pRangeHi->get()),
                                   juce::dontSendNotification);

    stepMax.setValue (proc.pMaxParams->get());

    pad.setPosition (proc.pMorphX->get(), proc.pMorphY->get());   // 拖动中它自己会拒绝
    segMorphMode.setSelected (proc.pMorphMode->getIndex());
    pad.setMode (proc.getMorphMode());

    syncPresetBox (cbPreset);
}

void GridEditor::updateStatus()
{
    using namespace theme;

    const bool has      = proc.getHosted() != nullptr;
    const bool motionOn = proc.isMotionMode();

    //-- 状态栏：新消息高亮 3 秒；"一个都没勾"整行变黄
    lStatus.setText (buildStatusText(), juce::dontSendNotification);
    lStatus.setColour (juce::Label::textColourId,
                       (has && list.getNumChecked() == 0 && ! proc.getModel().isEmpty()) ? warn
                       : isNoteFresh() ? text : textDim);

    //-- 顶栏
    {
        ui::badge (bPlugin, proc.getFocusedSlot() + 1);
        bPlugin.setButtonText (has ? proc.getHostedName() : juce::String ("空位 —— 点这里选一个插件"_u8));
    }

    const bool winOpen = host.isHostedWindowOpen (proc.getFocusedSlot());
    bUI.setButtonText (winOpen ? "关界面"_u8 : "界面"_u8);
    bUI.setToggleState (winOpen, juce::dontSendNotification);
    bUI    .setEnabled (has);
    bUnload.setEnabled (has);

    //-- 随机化
    // 一个按钮两种行为，文字和颜色都得跟着换，不然没法知道现在按下去会发生什么
    bRoll.setButtonText (motionOn ? "生成变体"_u8 : "随机化"_u8);
    ui::tint (bRoll, motionOn ? theme::motion : theme::accent);
    bRoll.setTooltip (motionOn ? "按当前语义从基座生成一个变体（R）"_u8 : "掷一次（R）"_u8);
    bRoll.setEnabled (has);
    bProbe.setEnabled (has && ! proc.getProbe().isRunning());

    // 强度 / 范围只对普通随机化有意义；运动模式用的是它自己那两个强度
    kAmount.setEnabled (has && ! motionOn);
    sRange .setEnabled (has && ! motionOn);
    stepMax.setEnabled (has);

    {
        bool suspicious = false;
        lResult.setText (resultText (suspicious), juce::dontSendNotification);
        lResult.setColour (juce::Label::textColourId, suspicious ? warn : textDim);
    }

    refreshHistoryUI();
    refreshChainUI();

    //-- 运动语义
    tbMotion    .setEnabled (has);
    segSemantic .setEnabled (has && motionOn);
    lSemantic   .setEnabled (has && motionOn);
    kMotion     .setEnabled (has && motionOn);
    kTone       .setEnabled (has && motionOn);
    bLockBase   .setEnabled (has && motionOn);
    bRestoreBase.setEnabled (has && motionOn && proc.hasMotionBase());

    // 已设定之后动词换成"重设"，一眼能看出这一下是**覆盖**不是首次设定
    bLockBase.setButtonText (proc.hasMotionBase() ? "重设基座"_u8 : "设为基座"_u8);

    // 基座身份必须显示出来。只写"已锁定"的话，"我到底锁没锁上、锁的是哪一个"
    // 完全看不出来——出问题时既没法自查也没法报告。#N 每次重设 +1。
    lBase.setText (has && motionOn ? "基座："_u8 + proc.getMotionBaseLabel() : juce::String(), juce::dontSendNotification);

    {
        bool warnProfile = false;
        lProfile.setText (profileText (warnProfile), juce::dontSendNotification);
        lProfile.setColour (juce::Label::textColourId, warnProfile ? warn : textFaint);
    }

    //-- 参数
    lCount.setText (has ? juce::String (proc.getModel().size()) + " 个  ·  "_u8 + proc.getHostedName()
                        : juce::String(),
                    juce::dontSendNotification);

    // 「勾选匹配 / 取消匹配」只在有关键词时才有意义
    const bool terms = list.hasFilterTerms();
    bCheckMatch  .setEnabled (has && terms);
    bUncheckMatch.setEnabled (has && terms);
    bOnlyMatching.setEnabled (has);
    teFilter     .setEnabled (has);
    segView      .setEnabled (has);
    bListMenu    .setEnabled (has);
}

} // namespace pr
