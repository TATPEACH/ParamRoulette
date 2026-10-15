#include "HardwareEditor.h"

namespace pr
{

using theme::Weight;
using theme::Icon;

namespace
{
    constexpr int kHeaderH    = 62;
    constexpr int kRackTitleH = 24;
    constexpr int kPlateH     = 20;     ///< drawSection 里铭牌的高度（11.5pt × 1.72）

    /** 和 hw::drawSection 返回的内区一致：铭牌下面开始、左右各缩 14、底部留 12。 */
    juce::Rectangle<int> sectionInner (juce::Rectangle<int> r)
    {
        return r.withTrimmedTop (kPlateH + 8).reduced (14, 0).withTrimmedBottom (12);
    }

    /** 铭牌那一行（分区标题所在的高度）。标题行右侧放这一区的小开关。 */
    juce::Rectangle<int> titleRow (juce::Rectangle<int> r)
    {
        return r.withHeight (kPlateH).reduced (14, 0);
    }

    juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
}

//==============================================================================
void HardwareEditor::Backdrop::paint (juce::Graphics& g)
{
    using namespace hw;

    const auto b = getLocalBounds();
    const auto F = faceplate;

    // —— 原木框：上下两块横板通长，左右两块竖板夹在中间（真的箱子就是这么拼的）
    fillWood (g, b.withHeight (F.getY()), false);
    fillWood (g, b.withTop (F.getBottom()), false);
    fillWood (g, juce::Rectangle<int> (b.getX(), F.getY(), F.getX(), F.getHeight()), true);
    fillWood (g, juce::Rectangle<int> (F.getRight(), F.getY(), b.getRight() - F.getRight(), F.getHeight()), true);

    // 拼缝 + 外沿倒角
    g.setColour (woodDark.darker (0.45f).withAlpha (0.6f));
    g.fillRect (b.getX(), F.getY(), F.getX(), 1);
    g.fillRect (F.getRight(), F.getY(), b.getRight() - F.getRight(), 1);
    g.fillRect (b.getX(), F.getBottom() - 1, F.getX(), 1);
    g.fillRect (F.getRight(), F.getBottom() - 1, b.getRight() - F.getRight(), 1);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawRect (b, 1);

    // —— 面板（嵌在框里：框的内沿压一圈阴影）
    drawFaceplate (g, F.toFloat());
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRect (F, 1);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), 0.0f, (float) F.getY(),
                                             juce::Colours::transparentBlack, 0.0f, (float) F.getY() + 8.0f, false));
    g.fillRect (F.withHeight (8));

    // —— 上下两块面板之间的接缝 + 每块四角的螺丝
    if (seamAt > F.getY() && seamAt < F.getBottom())
    {
        g.setColour (seam);
        g.fillRect (F.getX(), seamAt - 1, F.getWidth(), 2);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (F.getX(), seamAt + 1, F.getWidth(), 1);

        for (int y : { F.getY() + 11, seamAt - 11, seamAt + 12, F.getBottom() - 11 })
            for (int x : { F.getX() + 11, F.getRight() - 11 })
                drawScrew (g, { (float) x, (float) y }, 4.4f, 0.35f + (float) (x + y) * 0.011f);
    }

    // —— 商标：连笔字 + 一行型号小字（参考图里 SonEQ / Green 4 Mix EQ 的路数）
    if (! logo.isEmpty())
    {
        const auto l = logo.toFloat();
        g.setFont (logoFont (21.0f));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawText ("ParamRoulette", l.withHeight (l.getHeight() * 0.64f).translated (0.0f, 1.2f), juce::Justification::bottomLeft, false);
        g.setColour (legend);
        g.drawText ("ParamRoulette", l.withHeight (l.getHeight() * 0.64f), juce::Justification::bottomLeft, false);

        g.setFont (legendFont (8.5f, true));
        g.setColour (legendDim);
        g.drawText (juce::String ("PARAMETER RANDOMIZER  /  PR-6  /  ") + (sizeof (void*) == 4 ? "x86" : "x64"),
                    l.withTrimmedTop (l.getHeight() * 0.66f).translated (2.0f, 0.0f), juce::Justification::topLeft, false);
    }

    // —— 机架仓：凹进面板的一条深槽，模块插在里面
    if (! rackBay.isEmpty())
    {
        drawRecess (g, rackBay.toFloat(), 6.0f, bay);

        // 仓底的导轨孔（一排小方孔，机架上真的有）
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int x = rackBay.getX() + 18; x < rackBay.getRight() - 12; x += 22)
        {
            g.fillRect (x, rackBay.getY() + 4, 4, 2);
            g.fillRect (x, rackBay.getBottom() - 6, 4, 2);
        }
    }

    if (rackTitle.x > 0)
        drawPlate (g, rackTitle.toFloat(), "信号链"_u8, 11.5f, "SIGNAL CHAIN");

    for (const auto& s : sections)
        drawSection (g, s.bounds, s.title, s.english);

    for (const auto& s : screens)
        drawRecess (g, s.toFloat(), 4.0f, glass);

    for (const auto& l : legends)
    {
        g.setFont (l.latin ? legendFont (9.0f, true) : font (10.5f, Weight::bold));
        g.setColour (legendDim);
        g.drawText (l.text, l.bounds, l.just, true);
    }
}

//==============================================================================
HardwareEditor::HardwareEditor (ParamRouletteProcessor& p, EditorHost& h)
    : EditorBase (p, h, h.lookAndFeelFor (Skin::hardware)),
      vu ([this] (float& rms, float& pk) { proc.getOutputLevel (rms, pk); }),
      scope (p.getBank())
{
    addAndMakeVisible (backdrop);

    //== 顶栏 ====================================================================
    hwui::display (bPlugin);
    bPlugin.setTooltip ("当前链位的插件。点开换一个（装到当前聚焦的链位上）。"_u8);
    bPlugin.onClick = [this] { showPluginMenu (proc.getFocusedSlot(), &bPlugin); };
    addAndMakeVisible (bPlugin);

    cbPreset.setTextWhenNoChoicesAvailable ("（无 preset）"_u8);
    cbPreset.setTextWhenNothingSelected ("preset"_u8);
    cbPreset.setTooltip ("子插件的 program。切换会清空运动模式的基座。"_u8);
    cbPreset.onChange = [this] { selectPreset (cbPreset.getSelectedItemIndex()); };
    addAndMakeVisible (cbPreset);

    for (auto [b, ic, tip] : { std::tuple { &bUI,     Icon::window, "打开 / 关闭当前链位插件的界面（也可以双击机架上的模块）。灯亮 = 开着"_u8 },
                                std::tuple { &bUnload, Icon::eject,  "把当前链位的插件卸掉"_u8 },
                                std::tuple { &bSkin,   Icon::knob,   "换回 Grid 风格的界面。功能完全一样，随时可以再换回来"_u8 },
                                std::tuple { &bScan,   Icon::scan,   "扫描插件（可以增删搜索路径）"_u8 },
                                std::tuple { &bTags,   Icon::tag,    "给插件打功能标签。机架上模块胶带和跳线的颜色就来自这里"_u8 } })
    {
        ui::icon (*b, ic);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    }

    hwui::led (bUI, hw::ledGreen);
    bUI.onClick     = [this] { toggleHostedWindow(); };
    bUnload.onClick = [this] { doUnload (proc.getFocusedSlot()); };
    bSkin.onClick   = [this] { host.requestSkin (Skin::grid); };
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

    kCeiling.setName ("天花板"_u8);
    kCeiling.setRange (proc.pCeiling->range.start, proc.pCeiling->range.end, 0.1);
    hwui::knob (kCeiling, hw::Knob::knurledBlack);
    kCeiling.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    kCeiling.setDoubleClickReturnValue (true, -1.0);
    kCeiling.setPixelsPerRange (360.0f);
    kCeiling.setTooltip ("输出保护的天花板（dBFS）：峰值被压在这条线以下。默认 −1 dB，双击复位。"_u8
                         "这一项 Grid 版只能在 DAW 自动化里改"_u8);
    bindSlider (kCeiling, *proc.pCeiling);
    addAndMakeVisible (kCeiling);

    vu.setCaption ("OUTPUT");
    vu.setTooltip ("链尾输出电平（输出保护之后，也就是你真正听到的）。0 VU = −18 dBFS RMS；"_u8
                   "右下角的红灯在峰值超过 −1 dBFS 时亮"_u8);
    addAndMakeVisible (vu);

    for (auto [b, ic, tip] : { std::tuple { &bChainSave, Icon::save, "把整条链（每位的插件 + 状态 + 勾选表）存成文件，在别的工程里也能读回来"_u8 },
                                std::tuple { &bChainLoad, Icon::open, "读一条存过的链。会替换当前整条链，但不动随机化强度 / morph 位置 / 语义这些当前设置"_u8 } })
    {
        ui::icon (*b, ic);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    }
    bChainSave.onClick = [this] { promptSaveChainPreset(); };
    bChainLoad.onClick = [this] { showChainPresetMenu (bChainLoad); };

    //== 随机化 ==================================================================
    wireKnob (kAmount, *proc.pAmount, "强度"_u8, hw::Knob::pointerRed,
              { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" });
    kAmount.setTooltip ("0 = 不动，10 = 完全重掷；中间值是从当前值往随机目标挪这么多。"_u8
                        "拖动时按住 Shift 微调，双击回到满"_u8);
    kAmount.setDoubleClickReturnValue (true, 1.0);

    wireRangeKnobs();

    stepMax.setRange (0, proc.pMaxParams->getRange().getEnd());
    stepMax.setValue (proc.pMaxParams->get());
    stepMax.onChange = [this] (int v) { setMaxParams (v); };
    stepMax.setTooltip ("每次最多改动几个参数（-- = 不限）。只动 1~2 个时每次改动都能听出是哪一维在起作用。"_u8
                        "运动模式下只限制音色层。▲▼ / 滚轮 / 在数码管上上下拖，双击回到「不限」"_u8);
    addAndMakeVisible (stepMax);

    ui::icon (bProbe, Icon::probe);
    hwui::led (bProbe, hw::ledAmber);
    bProbe.setTooltip ("体检：问出每个参数到底收什么值，认出只收端点的伪开关。会出声！跑的时候灯亮"_u8);
    bProbe.onClick = [this] { confirmProbe(); };
    addAndMakeVisible (bProbe);

    ui::variant (bRoll, ui::Variant::primary);
    ui::icon (bRoll, Icon::dice);
    bRoll.onClick = [this] { doRoll(); };
    addAndMakeVisible (bRoll);

    for (auto [b, ic, back] : { std::tuple { &bHistBack, Icon::prev, true }, std::tuple { &bHistFwd, Icon::next, false } })
    {
        ui::icon (*b, ic);
        b->setTooltip (back ? "回到上一个状态（Ctrl+Z）。最多记 20 步，只在内存里，不进工程"_u8
                            : "前进到下一个状态（Ctrl+Y）"_u8);
        b->onClick = [this, back] { doHistory (back); };
        addAndMakeVisible (*b);
    }

    styleReadout (lHistory, 12.0f, hw::vfd, juce::Justification::centred);
    lHistory.setFont (hw::monoFont (12.5f));
    styleReadout (lResult, 11.0f, hw::vfd, juce::Justification::centredLeft);

    //== 运动语义 ================================================================
    tbMotion.setLedColour (hw::ledCyan);
    tbMotion.setButtonText ("运动模式"_u8);
    tbMotion.setTooltip ("运动模式：按「上扬 / 下降 / 先扬后抑」这种音乐意图生成变体，"_u8
                         "而不是逐参数掷骰子。拨上去以后大按键变成「生成变体」"_u8);
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
        selSemantic.setItems (std::move (items));
    }
    selSemantic.onChange = [this] (int i) { setSemantic (i); };
    addAndMakeVisible (selSemantic);

    styleReadout (lSemantic, 11.5f, hw::vfd);

    wireKnob (kMotion, *proc.pMotionAmt, "运动"_u8, hw::Knob::pointerYellow);
    kMotion.setTooltip ("运动层强度：包络深度、方向锚点拉多远、曲线撑多满。可以在 DAW 里画自动化。双击复位"_u8);
    kMotion.setDoubleClickReturnValue (true, 0.65);

    wireKnob (kTone, *proc.pToneAmt, "音色"_u8, hw::Knob::pointerYellow);
    kTone.setTooltip ("音色层：围绕基座的随机抖动幅度。逐参数的幅度在列表的「幅度」列里设。双击复位"_u8);
    kTone.setDoubleClickReturnValue (true, 0.35);

    ui::icon (bLockBase, Icon::pin);
    bLockBase.setTooltip ("把当前状态存成生成变体的出发点。已设定时会覆盖旧的（#N 会 +1）"_u8);
    bLockBase.onClick = [this] { lockBase(); };
    addAndMakeVisible (bLockBase);

    ui::icon (bRestoreBase, Icon::restore);
    bRestoreBase.setTooltip ("精确回到基座（用子插件的完整状态块还原）"_u8);
    bRestoreBase.onClick = [this] { restoreBase(); };
    addAndMakeVisible (bRestoreBase);

    styleReadout (lBase, 11.5f, hw::vfdAmber);
    styleReadout (lProfile, 10.5f, hw::legendDim, juce::Justification::topLeft);
    lProfile.setGlow (0.0f);

    //== 参数 ====================================================================
    teFilter.getProperties().set ("prSearch", true);
    teFilter.setIndents (32, 0);
    teFilter.setJustification (juce::Justification::centredLeft);
    teFilter.setFont (hw::font (12.5f));
    teFilter.setMultiLine (false);
    teFilter.setReturnKeyStartsNewLine (false);
    teFilter.setTextToShowWhenEmpty ("筛选参数名（空格分隔，任一命中）   Ctrl+F"_u8, hw::vfdDim);
    teFilter.setText (proc.filterText, false);
    teFilter.onTextChange = [this] { applyFilter (teFilter.getText(), bOnlyMatching.getToggleState()); };
    teFilter.onEscapeKey  = [this]
    {
        teFilter.clear();
        applyFilter (teFilter.getText(), bOnlyMatching.getToggleState());
        grabKeyboardFocus();
    };
    addAndMakeVisible (teFilter);

    hwui::led (bOnlyMatching, hw::ledAmber);
    bOnlyMatching.setClickingTogglesState (true);
    bOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);
    bOnlyMatching.setTooltip ("只显示名字匹配关键词的参数（自锁键，灯亮 = 开）"_u8);
    bOnlyMatching.onClick = [this] { applyFilter (teFilter.getText(), bOnlyMatching.getToggleState()); };
    addAndMakeVisible (bOnlyMatching);

    segView.setItems ({ { "全部"_u8,   "显示全部参数"_u8, {} },
                        { "已勾选"_u8, "只看勾选了的——回答「我到底勾了哪些」"_u8, {} },
                        { "已改动"_u8, "只看上一次操作真的改动了的"_u8, {} } });
    segView.setAccent (hw::ledAmber);
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
        ui::icon (*b, ic);
        b->setTooltip (tip);
        addAndMakeVisible (*b);
    }
    bCheckMatch  .onClick = [this] { checkMatching (true); };
    bUncheckMatch.onClick = [this] { checkMatching (false); };

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
        proc.setParamSpan (modelIndex, span);
        updateStatus();
    };
    addAndMakeVisible (list);

    //== 快照 / morph ============================================================
    segMorphMode.setItems ({ { "1D", "1D：已用快照按序号排成一条线"_u8, {} },
                             { "2D", "2D：四角固定绑快照 1/2/3/4，双线性插值"_u8, {} } });
    segMorphMode.setAccent (hw::ledGreen);
    segMorphMode.setSelected (proc.pMorphMode->getIndex());
    segMorphMode.onChange = [this] (int idx)
    {
        if (syncing)
            return;

        setMorphModeIndex (idx);
        scope.setMode (idx == 1 ? MorphMode::TwoD : MorphMode::OneD);
    };
    addAndMakeVisible (segMorphMode);

    memory.onStore  = [this] (int i) { storeSnapshot (i); };
    memory.onRecall = [this] (int i) { recallSnapshot (i); };
    memory.onClear  = [this] (int i) { clearSnapshot (i); };
    addAndMakeVisible (memory);

    scope.setMode (proc.getMorphMode());
    scope.setPosition (proc.pMorphX->get(), proc.pMorphY->get());
    scope.onDragStart       = [this] { morphDragStart(); };
    scope.onPositionChanged = [this] (float x, float y) { morphMoved (x, y); };
    scope.onDragEnd         = [this] { morphDragEnd(); };
    scope.setTooltip ("拖动亮点在快照之间连续过渡（会被 DAW 录成 Morph X / Y 自动化）。双击回中"_u8);
    addAndMakeVisible (scope);

    //== 状态行 ==================================================================
    styleReadout (lStatus, 12.0f, hw::vfd);
    lStatus.setGlow (0.45f);
}

HardwareEditor::~HardwareEditor() = default;

//==============================================================================
void HardwareEditor::styleReadout (HwReadout& r, float pt, juce::Colour c, juce::Justification j)
{
    r.setFont (hw::font (pt, Weight::medium));
    r.setColour (c);
    r.setJustification (j);
    addAndMakeVisible (r);
}

void HardwareEditor::wireKnob (HwKnob& s, juce::AudioParameterFloat& p, const juce::String& label,
                               hw::Knob style, const juce::StringArray& scale)
{
    s.setName (label);
    s.setRange (0.0, 1.0, 0.001);
    hwui::knob (s, style);
    if (! scale.isEmpty())
        hwui::scale (s, scale);
    s.textFromValueFunction = [] (double v) { return percent (v); };

    bindSlider (s, p);
    addAndMakeVisible (s);
}

void HardwareEditor::wireRangeKnobs()
{
    // 硬件上"一个范围"就是两颗旋钮。两颗互相推着走：下限拧过了上限，上限被一起推上去
    // （反过来也一样）——不会出现 下限 > 上限 这种看着就错的组合。
    wireKnob (kLo, *proc.pRangeLo, "下限"_u8, hw::Knob::knurledGrey);
    wireKnob (kHi, *proc.pRangeHi, "上限"_u8, hw::Knob::knurledGrey);

    for (auto* k : { &kLo, &kHi })
    {
        k->textFromValueFunction = [] (double v) { return juce::String (v, 2); };
        k->onDragStart = [this] { proc.pRangeLo->beginChangeGesture(); proc.pRangeHi->beginChangeGesture(); };
        k->onDragEnd   = [this] { proc.pRangeLo->endChangeGesture();   proc.pRangeHi->endChangeGesture(); };
    }

    kLo.setDoubleClickReturnValue (true, 0.0);
    kHi.setDoubleClickReturnValue (true, 1.0);
    kLo.setTooltip ("随机目标的下限（归一化 0..1）。拧过上限会把上限一起推上去。双击回到 0"_u8);
    kHi.setTooltip ("随机目标的上限（归一化 0..1）。拧到下限以下会把下限一起推下来。双击回到 1"_u8);

    kLo.onValueChange = [this]
    {
        if (syncing)
            return;

        const float lo = (float) kLo.getValue();
        proc.pRangeLo->setValueNotifyingHost (lo);

        if (lo > proc.pRangeHi->get())
        {
            const juce::ScopedValueSetter<bool> guard (syncing, true);
            kHi.setValue (lo, juce::dontSendNotification);
            proc.pRangeHi->setValueNotifyingHost (lo);
        }
    };

    kHi.onValueChange = [this]
    {
        if (syncing)
            return;

        const float hi = (float) kHi.getValue();
        proc.pRangeHi->setValueNotifyingHost (hi);

        if (hi < proc.pRangeLo->get())
        {
            const juce::ScopedValueSetter<bool> guard (syncing, true);
            kLo.setValue (hi, juce::dontSendNotification);
            proc.pRangeLo->setValueNotifyingHost (hi);
        }
    };

    kLo.setValue (proc.pRangeLo->get(), juce::dontSendNotification);
    kHi.setValue (proc.pRangeHi->get(), juce::dontSendNotification);
}

//==============================================================================
void HardwareEditor::paint (juce::Graphics&)
{
    // 全部画在 backdrop 里（缓存成图）。
}

void HardwareEditor::resized()
{
    const auto bounds = getLocalBounds();
    backdrop.setBounds (bounds);
    backdrop.sections.clear();
    backdrop.screens.clear();
    backdrop.legends.clear();

    const auto F = bounds.reduced (hw::frame);
    backdrop.faceplate = F;

    auto r = F.reduced (20, 0);

    //== 顶栏 ====================================================================
    {
        auto h = r.removeFromTop (kHeaderH).withTrimmedTop (6);
        h.removeFromLeft (14);                          // 角上的螺丝
        backdrop.logo = h.removeFromLeft (206).reduced (0, 6);

        auto row = h.withSizeKeepingCentre (h.getWidth(), 36);
        row.removeFromRight (14);

        auto key = [] (juce::Rectangle<int> a) { return a.withSizeKeepingCentre (a.getWidth(), 32); };
        bTags.setBounds (key (row.removeFromRight (66)));
        row.removeFromRight (6);
        bScan.setBounds (key (row.removeFromRight (66)));
        row.removeFromRight (6);
        bSkin.setBounds (key (row.removeFromRight (100)));
        row.removeFromRight (18);

        bPlugin.setBounds (row.removeFromLeft (juce::jmin (330, juce::roundToInt ((float) row.getWidth() * 0.46f))));
        row.removeFromLeft (8);
        cbPreset.setBounds (row.removeFromLeft (juce::jmin (220, row.getWidth() / 2)));
        row.removeFromLeft (10);
        bUI.setBounds (key (row.removeFromLeft (70)));
        row.removeFromLeft (6);
        bUnload.setBounds (key (row.removeFromLeft (66)));
    }

    //== 机架 ====================================================================
    const int rackH = juce::jlimit (140, 160, juce::roundToInt ((float) getHeight() * 0.19f));
    {
        auto rack  = r.removeFromTop (rackH);
        auto title = rack.removeFromTop (kRackTitleH);

        backdrop.rackTitle = { title.getX() + 12, title.getY() + 1 };
        bChainLoad.setBounds (title.removeFromRight (66).withSizeKeepingCentre (66, 24));
        title.removeFromRight (6);
        bChainSave.setBounds (title.removeFromRight (66).withSizeKeepingCentre (66, 24));

        auto bay = rack.withTrimmedTop (2).withTrimmedBottom (4);
        backdrop.rackBay = bay;

        auto inner = bay.reduced (8, 5);
        auto vuArea = inner.removeFromRight (juce::jlimit (130, 172, inner.getWidth() / 7));
        vu.setBounds (vuArea.withSizeKeepingCentre (vuArea.getWidth(), juce::jmin (vuArea.getHeight(), 112)));
        inner.removeFromRight (8);

        chain.setBounds (inner);
        kCeiling.setBounds (chain.ceilingKnobArea().translated (chain.getX(), chain.getY()));

        backdrop.seamAt = bay.getBottom() + 9;
    }

    r.removeFromTop (20);

    //== 状态行 ==================================================================
    {
        auto foot = r.removeFromBottom (34).withTrimmedBottom (6);
        auto hints = foot.removeFromRight (juce::jmin (430, juce::roundToInt ((float) foot.getWidth() * 0.4f)));
        backdrop.legends.push_back ({ hints, "R 随机 · Ctrl+Z / Y 历史 · 1–4 召回 · Shift+1–4 存入 · Ctrl+F 筛选"_u8,
                                      false, juce::Justification::centredRight });
        foot.removeFromRight (12);
        backdrop.screens.push_back (foot);
        lStatus.setBounds (foot.reduced (12, 1));
    }
    r.removeFromBottom (8);

    //== 三栏 ====================================================================
    const int leftW  = juce::jlimit (286, 318, juce::roundToInt ((float) getWidth() * 0.25f));
    const int rightW = juce::jlimit (262, 300, juce::roundToInt ((float) getWidth() * 0.235f));

    auto left   = r.removeFromLeft (leftW);
    r.removeFromLeft (12);
    auto right  = r.removeFromRight (rightW);
    r.removeFromRight (12);
    auto center = r;

    const int randomH = juce::jlimit (222, 248, left.getHeight() / 2);
    auto randomSec = left.removeFromTop (randomH);
    left.removeFromTop (10);
    auto motionSec = left;

    backdrop.sections.push_back ({ randomSec, "随机化"_u8,   "RANDOM" });
    backdrop.sections.push_back ({ motionSec, "运动语义"_u8, "MOTION" });
    backdrop.sections.push_back ({ center,    "参数"_u8,     "PARAMETERS" });
    backdrop.sections.push_back ({ right,     "快照 · Morph"_u8, "MEMORY" });

    //-- 随机化 ------------------------------------------------------------------
    {
        auto title = titleRow (randomSec);
        auto hist  = title.removeFromRight (108).withSizeKeepingCentre (108, 22).translated (0, -1);
        bHistFwd.setBounds (hist.removeFromRight (28));
        bHistBack.setBounds (hist.removeFromLeft (28));
        backdrop.screens.push_back (hist.reduced (3, 0));
        lHistory.setBounds (hist.reduced (4, 1));

        auto c = sectionInner (randomSec);

        const int rowAH = juce::jlimit (92, 118, c.getHeight() - 90);
        auto rowA = c.removeFromTop (rowAH);
        kAmount.setBounds (rowA.removeFromLeft (juce::jmin (rowAH + 4, 122)));
        rowA.removeFromLeft (6);

        auto stepRow = rowA.removeFromBottom (36);
        backdrop.legends.push_back ({ stepRow.removeFromLeft (46), "只动"_u8, false, juce::Justification::centred });
        stepMax.setBounds (stepRow.withSizeKeepingCentre (juce::jmin (104, stepRow.getWidth()), 34).withX (stepRow.getX()));
        rowA.removeFromBottom (2);

        const int kw = rowA.getWidth() / 2;
        kLo.setBounds (rowA.removeFromLeft (kw));
        kHi.setBounds (rowA);

        c.removeFromTop (8);
        auto rollRow = c.removeFromTop (40);
        bProbe.setBounds (rollRow.removeFromLeft (78));
        rollRow.removeFromLeft (8);
        bRoll.setBounds (rollRow);

        c.removeFromTop (8);
        backdrop.screens.push_back (c);
        lResult.setBounds (c.reduced (9, 2));
    }

    //-- 运动语义 ----------------------------------------------------------------
    {
        auto c = sectionInner (motionSec);
        c.removeFromTop (2);

        // 竖向预算：旋钮行吃剩下的，角色识别那两行字有地方才显示
        constexpr int lcdH = 40, btnH = 28, fixed = 6 + lcdH + 8 + btnH;
        const int  avail       = c.getHeight() - fixed;
        const bool showProfile = avail - 84 >= 38;
        const int  row1        = juce::jlimit (70, 100, avail - (showProfile ? 38 : 0));

        auto top = c.removeFromTop (row1);
        tbMotion.setBounds (top.removeFromLeft (46));
        top.removeFromLeft (2);
        selSemantic.setBounds (top.removeFromLeft (juce::jmin (128, top.getWidth() / 2)));
        top.removeFromLeft (2);
        const int kw = top.getWidth() / 2;
        kMotion.setBounds (top.removeFromLeft (kw));
        kTone.setBounds (top);

        c.removeFromTop (6);
        auto lcd = c.removeFromTop (lcdH);
        backdrop.screens.push_back (lcd);
        lSemantic.setBounds (lcd.reduced (9, 2).removeFromTop (18).translated (0, 1));
        lBase.setBounds (lcd.reduced (9, 2).removeFromBottom (18).translated (0, -1));

        c.removeFromTop (8);
        auto btns = c.removeFromTop (btnH);
        bLockBase.setBounds (btns.removeFromLeft (btns.getWidth() / 2 - 3));
        btns.removeFromLeft (6);
        bRestoreBase.setBounds (btns);

        c.removeFromTop (6);
        lProfile.setVisible (showProfile);
        lProfile.setBounds (c);
    }

    //-- 参数 --------------------------------------------------------------------
    {
        auto title = titleRow (center);
        bListMenu.setBounds (title.removeFromRight (34).withSizeKeepingCentre (34, 26).translated (0, 2));
        title.removeFromRight (6);
        segView.setBounds (title.removeFromRight (234).withSizeKeepingCentre (234, 26).translated (0, 2));

        auto c = sectionInner (center);
        c.removeFromTop (4);

        auto tools = c.removeFromTop (32);
        bUncheckMatch.setBounds (tools.removeFromRight (96));
        tools.removeFromRight (6);
        bCheckMatch.setBounds (tools.removeFromRight (96));
        tools.removeFromRight (6);
        bOnlyMatching.setBounds (tools.removeFromRight (84));
        tools.removeFromRight (10);
        teFilter.setBounds (tools);

        c.removeFromTop (10);
        backdrop.screens.push_back (c);
        list.setBounds (c.reduced (5));
    }

    //-- 快照 / morph ------------------------------------------------------------
    {
        auto title = titleRow (right);
        segMorphMode.setBounds (title.removeFromRight (92).withSizeKeepingCentre (92, 26).translated (0, 2));

        auto c = sectionInner (right);
        c.removeFromTop (6);
        memory.setBounds (c.removeFromTop (72));

        c.removeFromTop (8);
        backdrop.screens.push_back (c);
        scope.setBounds (c.reduced (5));
    }

    backdrop.repaint();
}

//==============================================================================
void HardwareEditor::focusFilter()
{
    teFilter.grabKeyboardFocus();
    teFilter.selectAll();
}

void HardwareEditor::rebuildSkin()
{
    list.setSource (proc.getModel(), proc.getChecked());

    if (teFilter.getText() != proc.filterText)
        teFilter.setText (proc.filterText, false);

    bOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);

    list.setFilter (proc.filterText, proc.onlyMatching);
    list.rebuild();

    fillPresetBox (cbPreset);
}

void HardwareEditor::refreshSnapshotTiles()
{
    memory.setTiles (makeSnapshotTiles(), proc.getBank().getWinner(), proc.getNumLoadedSlots() > 0);
    scope.repaint();
}

void HardwareEditor::syncMotionUI()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto s = proc.getMotionSettings();

    tbMotion.setToggleState (proc.isMotionMode(), juce::dontSendNotification);

    const int idx = juce::jlimit (0, kNumMotionSemantics - 1, (int) s.semantic);
    selSemantic.setSelected (idx);
    lSemantic.setText (motionSemanticLabel (s.semantic) + "  ·  "_u8 + semanticHint (s.semantic));
}

void HardwareEditor::syncFromParameters()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    // 用户正按着的控件不回写，否则自动化和手会互相拽。
    if (! kAmount .isMouseButtonDown()) kAmount .setValue (proc.pAmount->get(),    juce::dontSendNotification);
    if (! kMotion .isMouseButtonDown()) kMotion .setValue (proc.pMotionAmt->get(), juce::dontSendNotification);
    if (! kTone   .isMouseButtonDown()) kTone   .setValue (proc.pToneAmt->get(),   juce::dontSendNotification);
    if (! kCeiling.isMouseButtonDown()) kCeiling.setValue (proc.pCeiling->get(),   juce::dontSendNotification);

    if (! kLo.isMouseButtonDown() && ! kHi.isMouseButtonDown())
    {
        kLo.setValue (juce::jmin (proc.pRangeLo->get(), proc.pRangeHi->get()), juce::dontSendNotification);
        kHi.setValue (juce::jmax (proc.pRangeLo->get(), proc.pRangeHi->get()), juce::dontSendNotification);
    }

    stepMax.setValue (proc.pMaxParams->get());

    scope.setPosition (proc.pMorphX->get(), proc.pMorphY->get());
    segMorphMode.setSelected (proc.pMorphMode->getIndex());
    scope.setMode (proc.getMorphMode());

    syncPresetBox (cbPreset);
}

void HardwareEditor::updateStatus()
{
    using namespace hw;

    const bool has      = proc.getHosted() != nullptr;
    const bool motionOn = proc.isMotionMode();

    //-- 状态行：新消息亮 3 秒；"一个都没勾"变琥珀色
    lStatus.setText (buildStatusText());
    lStatus.setColour ((has && list.getNumChecked() == 0 && ! proc.getModel().isEmpty()) ? vfdAmber
                       : isNoteFresh() ? vfd.brighter (0.35f) : vfd.withMultipliedBrightness (0.82f));

    //-- 顶栏
    ui::badge (bPlugin, proc.getFocusedSlot() + 1);
    bPlugin.setButtonText (has ? proc.getHostedName() : juce::String ("空位 —— 点这里选一个插件"_u8));

    const bool winOpen = host.isHostedWindowOpen (proc.getFocusedSlot());
    bUI.setToggleState (winOpen, juce::dontSendNotification);
    bUI    .setEnabled (has);
    bUnload.setEnabled (has);

    //-- 随机化：同一颗大键两种行为——字和灯色都要跟着换，不然不知道按下去会发生什么
    bRoll.setButtonText (motionOn ? "生成变体"_u8 : "随机化"_u8);
    ui::tint (bRoll, motionOn ? juce::Colour (0xff1c8f86) : capRed);
    bRoll.setTooltip (motionOn ? "按当前语义从基座生成一个变体（R）"_u8 : "掷一次（R）"_u8);
    bRoll.setEnabled (has);

    const bool probing = proc.getProbe().isRunning();
    bProbe.setToggleState (probing, juce::dontSendNotification);
    bProbe.setEnabled (has && ! probing);

    kAmount.setEnabled (has && ! motionOn);
    kLo    .setEnabled (has && ! motionOn);
    kHi    .setEnabled (has && ! motionOn);
    stepMax.setEnabled (has);

    {
        bool suspicious = false;
        lResult.setText (resultText (suspicious));
        lResult.setColour (suspicious ? vfdAmber : vfd.withMultipliedBrightness (0.85f));
    }

    bHistBack.setEnabled (has && proc.canHistoryBack());
    bHistFwd .setEnabled (has && proc.canHistoryForward());
    lHistory.setText (historyText());

    refreshChainUI();
    kCeiling.setEnabled (proc.pGuardOn->get());

    //-- 运动语义
    tbMotion    .setEnabled (has);
    selSemantic .setEnabled (has && motionOn);
    kMotion     .setEnabled (has && motionOn);
    kTone       .setEnabled (has && motionOn);
    bLockBase   .setEnabled (has && motionOn);
    bRestoreBase.setEnabled (has && motionOn && proc.hasMotionBase());
    bLockBase.setButtonText (proc.hasMotionBase() ? "重设基座"_u8 : "设为基座"_u8);

    lSemantic.setColour (has && motionOn ? vfd : vfdDim);
    lBase.setText (has && motionOn ? "基座："_u8 + proc.getMotionBaseLabel()
                                   : (has ? juce::String ("运动模式关着——拨杆拨上去才生效"_u8) : juce::String()));
    lBase.setColour (has && motionOn ? vfdAmber : vfdDim);

    {
        bool warnProfile = false;
        lProfile.setText (profileText (warnProfile));
        lProfile.setColour (warnProfile ? vfdAmber : legendDim);
    }

    //-- 参数
    const bool terms = list.hasFilterTerms();
    bCheckMatch  .setEnabled (has && terms);
    bUncheckMatch.setEnabled (has && terms);
    bOnlyMatching.setEnabled (has);
    teFilter     .setEnabled (has);
    segView      .setEnabled (has);
    bListMenu    .setEnabled (has);
}

} // namespace pr
