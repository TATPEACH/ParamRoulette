#include "PluginEditor.h"

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

    /** 每个运动语义的一句话说明——直接写在选择器底下，不用去翻手册。 */
    juce::String semanticHint (MotionSemantic s)
    {
        switch (s)
        {
            case MotionSemantic::Preserve: return "不碰运动层，只在基座附近抖音色"_u8;
            case MotionSemantic::Rise:     return "单段 ramp · invert = 0"_u8;
            case MotionSemantic::Fall:     return "单段 ramp · invert = 1（和上扬同一条曲线）"_u8;
            case MotionSemantic::RiseFall: return "双段 peak · invert = 0"_u8;
            case MotionSemantic::FallRise: return "双段 peak · invert = 1"_u8;
            case MotionSemantic::Wander:   return "双段 · 方向每次随机"_u8;
        }
        return {};
    }

    /** 通用的工具窗口壳子。扫描界面和标签界面都用它——
        两个都是"一个内容组件 + 一个关闭按钮"，没必要各写一个类。 */
    class ToolWindow final : public juce::DocumentWindow
    {
    public:
        ToolWindow (const juce::String& title,
                    std::unique_ptr<juce::Component> content,
                    int w, int h,
                    juce::LookAndFeel& lf,
                    std::function<void()> onCloseRequested)
            : juce::DocumentWindow (title, theme::canvas, juce::DocumentWindow::closeButton),
              onClose (std::move (onCloseRequested))
        {
            // 顶层窗口不会从 editor 继承 LookAndFeel，得自己设，不然里面又是默认的蓝灰色
            setLookAndFeel (&lf);
            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), false);
            setResizable (true, false);
            centreWithSize (w, h);
            setVisible (true);
        }

        ~ToolWindow() override
        {
            clearContentComponent();
            setLookAndFeel (nullptr);
        }

        void closeButtonPressed() override
        {
            if (onClose != nullptr)
                onClose();      // 通常会把 this 删掉，后面不能再有代码
        }

    private:
        std::function<void()> onClose;
    };
}

//==============================================================================
void ParamRouletteEditor::Backdrop::setLayout (juce::Rectangle<int> h, juce::Rectangle<int> s,
                                               juce::Rectangle<int> cl, std::vector<Card> c)
{
    header = h;
    status = s;
    chainLabel = cl;
    cards = std::move (c);
    repaint();
}

void ParamRouletteEditor::Backdrop::paint (juce::Graphics& g)
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
ParamRouletteEditor::ParamRouletteEditor (ParamRouletteProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      pad (p.getBank())
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

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
    cbPreset.onChange = [this]
    {
        if (syncing)
            return;

        const int idx = cbPreset.getSelectedItemIndex();
        if (idx < 0)
            return;

        proc.setHostedProgram (idx);
        setNote ("已切到 preset "_u8 + juce::String (idx) + " " + proc.getHostedProgramName (idx)
                 + "（基座已清空）"_u8);
    };
    addAndMakeVisible (cbPreset);

    for (auto [b, ic, tip] : { std::tuple { &bUI,     Icon::window, "打开 / 关闭当前链位插件的界面（也可以双击信号链里的模块）"_u8 },
                                std::tuple { &bUnload, Icon::eject,  "把当前链位的插件卸掉"_u8 },
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
    bScan.onClick   = [this] { showScanWindow(); };
    bTags.onClick   = [this] { showTagWindow(); };

    //== 信号链 ==================================================================
    chain.onFocus = [this] (int i) { proc.setFocusedSlot (i); };
    chain.onAdd   = [this] (int i) { showPluginMenu (i, nullptr); };
    chain.onBypass = [this] (int i, bool b)
    {
        proc.setSlotBypassed (i, b);
        setNote (b ? "链位 "_u8 + juce::String (i + 1) + " 已旁通"_u8
                   : "链位 "_u8 + juce::String (i + 1) + " 恢复工作"_u8);
    };
    chain.onOpenEditor  = [this] (int i) { openHostedWindow (i); };
    chain.onContextMenu = [this] (int i) { showModuleMenu (i); };
    chain.onGuardToggle = [this] (bool on)
    {
        auto& prm = *proc.pGuardOn;
        prm.beginChangeGesture();
        prm.setValueNotifyingHost (on ? 1.0f : 0.0f);
        prm.endChangeGesture();
        setNote (on ? "输出保护已打开"_u8 : "输出保护已关闭（NaN 仍会被清零）"_u8);
    };
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
    bChainLoad.onClick = [this] { showChainPresetMenu(); };

    //== 随机化 ==================================================================
    wireKnob (kAmount, *proc.pAmount, "强度"_u8, theme::accent);
    kAmount.setTooltip ("0 = 不动，1 = 完全重掷；中间值是从当前值往随机目标挪这么多。双击回到 100%"_u8);
    kAmount.setDoubleClickReturnValue (true, 1.0);

    wireRange();

    stepMax.setRange (0, proc.pMaxParams->getRange().getEnd());
    stepMax.setValue (proc.pMaxParams->get());
    // 0 不该显示成 "0"——那看着像"一个都不动"。它的意思是"不限制"。
    stepMax.textFor = [] (int v) { return v <= 0 ? juce::String ("只动：不限"_u8) : "只动 "_u8 + juce::String (v) + " 个"_u8; };
    stepMax.onChange = [this] (int v)
    {
        if (syncing)
            return;

        auto& prm = *proc.pMaxParams;
        prm.beginChangeGesture();
        prm.setValueNotifyingHost (prm.convertTo0to1 ((float) v));
        prm.endChangeGesture();
    };
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
    tbMotion.onClick = [this]
    {
        if (! syncing)
            proc.setMotionMode (tbMotion.getToggleState());
    };
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
    segSemantic.onChange = [this] (int) { pushMotionSettings(); };
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
    bLockBase.onClick = [this]
    {
        setNote (proc.lockMotionBase() ? ("基座 → "_u8 + proc.getMotionBaseLabel())
                                       : juce::String ("设定失败：没有加载插件"_u8));
    };
    addAndMakeVisible (bLockBase);

    ui::icon (bRestoreBase, Icon::restore);
    bRestoreBase.setTooltip ("精确回到基座（用子插件的完整状态块还原）"_u8);
    bRestoreBase.onClick = [this]
    {
        setNote (proc.restoreMotionBase() ? ("已还原到基座 "_u8 + proc.getMotionBaseLabel())
                                          : juce::String ("还原失败：基座未设定"_u8));
    };
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
    teFilter.onTextChange = [this] { applyFilter(); };
    teFilter.onEscapeKey  = [this] { teFilter.clear(); applyFilter(); grabKeyboardFocus(); };
    addAndMakeVisible (teFilter);

    ui::variant (bOnlyMatching, ui::Variant::chip);
    bOnlyMatching.setClickingTogglesState (true);
    bOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);
    bOnlyMatching.setTooltip ("只显示名字匹配关键词的参数"_u8);
    bOnlyMatching.onClick = [this] { applyFilter(); };
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
    bCheckMatch  .onClick = [this] { list.applyToMatching (true);  setNote ("已勾选全部匹配项"_u8); };
    bUncheckMatch.onClick = [this] { list.applyToMatching (false); setNote ("已取消全部匹配项"_u8); };

    ui::variant (bListMenu, ui::Variant::ghost);
    ui::icon (bListMenu, Icon::more);
    bListMenu.setTooltip ("全选 / 全不选 / 反选（只作用于当前可见的行）"_u8);
    bListMenu.onClick = [this] { showListMenu(); };
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

        proc.pMorphMode->beginChangeGesture();
        proc.pMorphMode->setValueNotifyingHost (proc.pMorphMode->convertTo0to1 ((float) idx));
        proc.pMorphMode->endChangeGesture();
        pad.setMode (idx == 1 ? MorphMode::TwoD : MorphMode::OneD);
    };
    addAndMakeVisible (segMorphMode);

    tiles.onStore = [this] (int i)
    {
        proc.captureSlot (i);
        setNote ("整条链已存入快照 "_u8 + juce::String (i + 1));
    };
    tiles.onRecall = [this] (int i)
    {
        proc.recallSlot (i);
        setNote ("已召回快照 "_u8 + juce::String (i + 1));
    };
    tiles.onClear = [this] (int i)
    {
        proc.clearSlot (i);
        setNote ("已清空快照 "_u8 + juce::String (i + 1));
    };
    addAndMakeVisible (tiles);

    pad.setMode (proc.getMorphMode());
    pad.setPosition (proc.pMorphX->get(), proc.pMorphY->get());
    pad.onDragStart = [this]
    {
        proc.pMorphX->beginChangeGesture();
        proc.pMorphY->beginChangeGesture();
    };
    pad.onPositionChanged = [this] (float x, float y)
    {
        proc.pMorphX->setValueNotifyingHost (x);
        proc.pMorphY->setValueNotifyingHost (y);
    };
    pad.onDragEnd = [this]
    {
        proc.pMorphX->endChangeGesture();
        proc.pMorphY->endChangeGesture();
    };
    addAndMakeVisible (pad);

    //== 状态栏 ==================================================================
    styleLabel (lStatus, 11.5f, theme::textDim);
    addAndMakeVisible (lStatus);

    styleLabel (lHints, 11.0f, theme::textFaint, Weight::regular, juce::Justification::centredRight);
    lHints.setText ("R 随机   ·   Ctrl+Z / Ctrl+Y 历史   ·   1–4 召回   ·   Shift+1–4 存入   ·   Ctrl+F 筛选"_u8,
                    juce::dontSendNotification);
    addAndMakeVisible (lHints);

    //== 挂钩 ====================================================================
    // 换插件前 processor 会**同步**调这个把子插件窗口关掉。装在这里、dtor 里第一件事就摘掉。
    proc.closeHostedWindow = [this] (int chainSlot) { closeHostedWindows (chainSlot); };
    proc.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (1100, 700, 2400, 1600);
    setSize (1240, 780);

    rebuildAll();
    startTimerHz (20);
}

ParamRouletteEditor::~ParamRouletteEditor()
{
    // 顺序不能变：钩子里捕获了 this，先摘钩子再拆自己。
    proc.closeHostedWindow = nullptr;
    proc.removeChangeListener (this);
    stopTimer();

    closeHostedWindows (-1);
    scanWindow.reset();
    tagWindow.reset();

    setLookAndFeel (nullptr);
}

//==============================================================================
void ParamRouletteEditor::wireKnob (juce::Slider& s, juce::AudioParameterFloat& p,
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
    s.setValue (p.get(), juce::dontSendNotification);
    s.setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);

    s.onDragStart = [&p] { p.beginChangeGesture(); };
    s.onDragEnd   = [&p] { p.endChangeGesture(); };
    s.onValueChange = [this, &s, &p]
    {
        if (! syncing)
            p.setValueNotifyingHost (p.convertTo0to1 ((float) s.getValue()));
    };

    addAndMakeVisible (s);
}

void ParamRouletteEditor::wireRange()
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
void ParamRouletteEditor::paint (juce::Graphics&)
{
    // 全部画在 backdrop 里（缓存成图）。这里什么都不用画。
}

void ParamRouletteEditor::resized()
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
void ParamRouletteEditor::mouseDown (const juce::MouseEvent&)
{
    // 点空白处把键盘焦点拿回来，快捷键才有效（筛选框获得过焦点之后尤其需要）。
    grabKeyboardFocus();
}

bool ParamRouletteEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode());

    if (mods.isCommandDown() && ! mods.isAltDown())
    {
        if (code == 'Z' && ! mods.isShiftDown())                 { doHistory (true);  return true; }
        if (code == 'Y' || (code == 'Z' && mods.isShiftDown()))  { doHistory (false); return true; }
        if (code == 'F')
        {
            teFilter.grabKeyboardFocus();
            teFilter.selectAll();
            return true;
        }
        return false;
    }

    if (mods.isAltDown())
        return false;

    if (code == 'R' && ! mods.isShiftDown())
    {
        doRoll();
        return true;
    }

    // 1–4 召回、Shift+1–4 存入。Shift 下有的键盘布局给的是符号，两种都认。
    static const char shifted[] = { '!', '@', '#', '$' };
    for (int i = 0; i < kNumSlots; ++i)
    {
        const bool digit = (code == (juce::juce_wchar) ('1' + i));
        const bool sym   = k.getTextCharacter() == (juce::juce_wchar) shifted[i];

        if (digit || sym)
        {
            if (mods.isShiftDown())
            {
                if (proc.getNumLoadedSlots() > 0) { proc.captureSlot (i); setNote ("整条链已存入快照 "_u8 + juce::String (i + 1)); }
            }
            else if (proc.getBank().isUsed (i))
            {
                proc.recallSlot (i);
                setNote ("已召回快照 "_u8 + juce::String (i + 1));
            }
            return true;
        }
    }

    return false;
}

void ParamRouletteEditor::doRoll()
{
    if (proc.getHosted() == nullptr)
    {
        setNote ("当前链位没有插件——先在信号链上点一个空位装一个"_u8);
        return;
    }

    note.clear();
    proc.randomizeNow();    // 已经在消息线程上，直接来
}

void ParamRouletteEditor::doHistory (bool back)
{
    if (back)
        setNote (proc.historyBack() ? "← 回到上一个状态"_u8 : "已经是最早的记录了"_u8);
    else
        setNote (proc.historyForward() ? "→ 前进到下一个状态"_u8 : "已经是最新的了"_u8);
}

void ParamRouletteEditor::setNote (const juce::String& n)
{
    note = n;
    noteTime = juce::Time::getMillisecondCounter();
    updateStatus();
}

//==============================================================================
void ParamRouletteEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (proc.getStructureGeneration() != lastGeneration)
    {
        rebuildAll();
        return;
    }

    list.refresh();
    refreshSnapshotTiles();
    syncMotionUI();
    updateStatus();
}

void ParamRouletteEditor::timerCallback()
{
    if (proc.getStructureGeneration() != lastGeneration)
    {
        rebuildAll();
    }
    else if (proc.consumeHostedValuesDirty())
    {
        // processor 的定时器只推 morph，不刷显示值——这一步只能在这边做。
        proc.refreshModelValues();
        list.refresh();
    }

    syncFromParameters();
    updateStatus();
}

void ParamRouletteEditor::rebuildAll()
{
    lastGeneration = proc.getStructureGeneration();

    // **列表每次都要重新指一遍数据源**：聚焦的链位可能换了。上一版这里是构造时绑死的
    // 引用，切链位之后列表还在画旧插件的参数。
    list.setSource (proc.getModel(), proc.getChecked());

    // 恢复工程时 filter 是从状态里读回来的，界面要跟上。
    if (teFilter.getText() != proc.filterText)
        teFilter.setText (proc.filterText, false);

    bOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);

    list.setFilter (proc.filterText, proc.onlyMatching);
    list.rebuild();

    rebuildPresetList();
    refreshSnapshotTiles();
    syncMotionUI();
    updateStatus();
}

void ParamRouletteEditor::rebuildPresetList()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    cbPreset.clear (juce::dontSendNotification);

    const int n = proc.getHostedNumPrograms();

    // 只有一个 program 的插件（大多数 VST3）没得选，别在界面上摆个死控件
    if (n <= 1)
    {
        cbPreset.setEnabled (false);
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        auto name = proc.getHostedProgramName (i).trim();
        if (name.isEmpty())
            name = "(未命名)"_u8;

        cbPreset.addItem (juce::String (i) + "  " + name, i + 1);
    }

    cbPreset.setEnabled (true);
    cbPreset.setSelectedItemIndex (juce::jmax (0, proc.getHostedProgram()), juce::dontSendNotification);
}

void ParamRouletteEditor::refreshSnapshotTiles()
{
    std::array<SnapshotTiles::Tile, 4> t;

    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& cs = proc.getBank()[i];
        t[(size_t) i].used = cs.anyUsed();
        t[(size_t) i].name = cs.name.isNotEmpty() ? cs.name : "快照 "_u8 + juce::String (i + 1);

        int n = 0;
        for (const auto& s : cs.perSlot)
            if (s.used)
                ++n;
        t[(size_t) i].numSlots = n;
    }

    tiles.setTiles (t, proc.getBank().getWinner(), proc.getNumLoadedSlots() > 0);
    pad.repaint();
}

void ParamRouletteEditor::refreshChainUI()
{
    std::array<ChainStrip::Slot, kMaxChain> s;

    for (int i = 0; i < kMaxChain; ++i)
    {
        auto& v = s[(size_t) i];
        auto* p = proc.getSlotPlugin (i);

        v.loaded   = p != nullptr;
        v.bypassed = proc.isSlotBypassed (i);

        if (p == nullptr)
            continue;

        const auto d   = p->getPluginDescription();
        const auto tag = proc.getScanner().loadTag (d.createIdentifierString());

        v.name   = proc.getSlotName (i);
        v.colour = theme::tagColour (tag);
        v.tag    = tag == FnTag::Unknown ? juce::String ("未分类"_u8) : fnTagLabel (tag);
        v.detail = d.pluginFormatName + "  ·  "_u8 + juce::String (p->getParameters().size()) + " 参数"_u8;
    }

    // —— 链尾安全网。NaN 优先：那是"某个子插件坏了"，比增益衰减要紧得多。
    if (proc.consumeSawNonFinite())
        guardWarnUntil = juce::Time::getMillisecondCounter() + 4000;

    ChainStrip::Guard gd;
    gd.on        = proc.pGuardOn->get();
    gd.grDb      = proc.getGuardGrDb();
    gd.ceilingDb = proc.pCeiling->get();
    gd.nanWarn   = juce::Time::getMillisecondCounter() < guardWarnUntil;

    chain.setState (s, proc.getFocusedSlot(), gd);
}

void ParamRouletteEditor::refreshHistoryUI()
{
    const bool has = proc.getHosted() != nullptr;

    bHistBack.setEnabled (has && proc.canHistoryBack());
    bHistFwd .setEnabled (has && proc.canHistoryForward());

    const int n = proc.getHistoryCount();
    lHistory.setText (n > 1 ? (juce::String (proc.getHistoryPos() + 1) + " / " + juce::String (n))
                            : juce::String ("—"_u8),
                      juce::dontSendNotification);
}

void ParamRouletteEditor::syncMotionUI()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto s = proc.getMotionSettings();

    tbMotion.setToggleState (proc.isMotionMode(), juce::dontSendNotification);

    const int idx = juce::jlimit (0, kNumMotionSemantics - 1, (int) s.semantic);
    segSemantic.setSelected (idx);
    lSemantic.setText (motionSemanticLabel (s.semantic) + "  ·  "_u8 + semanticHint (s.semantic),
                       juce::dontSendNotification);
}

void ParamRouletteEditor::syncFromParameters()
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

    // 子插件自己的界面上也能换 preset（fauna 就有），那边换了这边要跟上
    if (cbPreset.isEnabled())
    {
        const int p = proc.getHostedProgram();
        if (p >= 0 && cbPreset.getSelectedItemIndex() != p)
            cbPreset.setSelectedItemIndex (p, juce::dontSendNotification);
    }
}

juce::String ParamRouletteEditor::buildStatusText()
{
    if (proc.isLoading())
        return "正在加载插件…"_u8;

    juce::String s;

    if (proc.getHosted() == nullptr)
    {
        s << "链位 "_u8 << (proc.getFocusedSlot() + 1) << " 是空的"_u8;
    }
    else
    {
        s << "链位 "_u8 << (proc.getFocusedSlot() + 1) << "  ·  "_u8 << proc.getModel().size() << " 个参数"_u8
          << "  ·  勾选 "_u8 << list.getNumChecked() << " / "_u8 << list.getNumVisible()
          << "  ·  快照 "_u8 << proc.getBank().numUsed() << " / " << kNumSlots;

        // 一个都没勾的时候随机化会**静默无事发生**，必须说出来。
        // 勾选状态是按插件持久化的（跨实例记忆），所以这个 0 可能是上次留下的。
        if (list.getNumChecked() == 0 && proc.getModel().size() > 0)
            s << "   ⚠ 一个都没勾，随机化不会有任何变化——点表头的圆圈全选"_u8;

        if (proc.getProbe().isRunning())
            s << "  ·  体检中…"_u8;
    }

    if (note.isNotEmpty())
        s << "     " << note;

    return s;
}

void ParamRouletteEditor::updateStatus()
{
    using namespace theme;

    const bool has    = proc.getHosted() != nullptr;
    const bool motionOn = proc.isMotionMode();

    //-- 状态栏：新消息高亮 3 秒；"一个都没勾"整行变黄
    lStatus.setText (buildStatusText(), juce::dontSendNotification);
    const bool fresh = note.isNotEmpty() && juce::Time::getMillisecondCounter() - noteTime < 3000;
    lStatus.setColour (juce::Label::textColourId,
                       (has && list.getNumChecked() == 0 && ! proc.getModel().isEmpty()) ? warn
                       : fresh ? text : textDim);

    //-- 顶栏
    {
        ui::badge (bPlugin, proc.getFocusedSlot() + 1);
        bPlugin.setButtonText (has ? proc.getHostedName() : juce::String ("空位 —— 点这里选一个插件"_u8));
    }

    const bool winOpen = juce::isPositiveAndBelow (proc.getFocusedSlot(), kMaxChain)
                      && hostedWindows[(size_t) proc.getFocusedSlot()] != nullptr;
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
        juce::String res;
        const auto r = proc.getLastResult();

        if (r.touched > 0)
        {
            res << (motionOn ? "上次生成："_u8 : "上次随机："_u8)
                << "改动 "_u8 << proc.getLastChangedCount() << " / 写入 "_u8 << r.touched;

            if (const auto m = proc.getLastMotion(); motionOn && m.note.isNotEmpty())
                res << "\n" << m.note;
            else if (! motionOn && r.looksSuspicious())
                res << "\n⚠ 大量参数纹丝不动，跑一次「体检」看看"_u8;
        }
        else
        {
            res = has ? juce::String ("还没掷过。按 R 或者点上面的按钮"_u8) : juce::String();
        }

        lResult.setText (res, juce::dontSendNotification);
        lResult.setColour (juce::Label::textColourId, (! motionOn && r.looksSuspicious()) ? warn : textDim);
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
        // 运动层的角色识别情况。认不出包络就直说——不然用户会以为语义没生效是 bug。
        juce::String prof;
        if (has)
        {
            const auto& p = proc.getMotionProfile();
            prof = (motionOn && ! p.hasMotion() ? juce::String ("⚠ "_u8) : juce::String()) + p.summary();

            // 曲线能不能改是两回事：轮廓点没暴露成参数时，唯一的路是插件的状态块。
            const int cg = proc.getContourGroups();
            prof << "\n" << (cg > 0 ? ("曲线可写 "_u8 + juce::String (cg) + " 条"_u8)
                                    : juce::String ("曲线不可写（格式不认识）"_u8));
        }

        lProfile.setText (prof, juce::dontSendNotification);
        lProfile.setColour (juce::Label::textColourId,
                            (motionOn && has && ! proc.getMotionProfile().hasMotion()) ? warn : textFaint);
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

//==============================================================================
void ParamRouletteEditor::showPluginMenu (int chainSlot, juce::Component* anchor)
{
    auto types = proc.getScanner().getKnownPlugins().getTypes();

    if (types.isEmpty())
    {
        // 一个都没扫过就弹空菜单，用户只会以为坏了。直接把他领到扫描窗口。
        setNote ("插件表是空的，先扫描一次"_u8);
        showScanWindow();
        return;
    }

    juce::PopupMenu m;
    m.addSectionHeader ("装到链位 "_u8 + juce::String (chainSlot + 1));

    // 必须用**静态**版本：非静态的那两个在 8.0.10 里已标 deprecated。
    juce::KnownPluginList::addToMenu (m, types, juce::KnownPluginList::sortByManufacturer,
                                      proc.isSlotLoaded (chainSlot)
                                          ? proc.getSlotPlugin (chainSlot)->getPluginDescription().createIdentifierString()
                                          : juce::String());

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    auto opts = juce::PopupMenu::Options().withTargetComponent (anchor != nullptr ? anchor : &chain);
    if (anchor == nullptr)
        opts = opts.withMousePosition();

    m.showMenuAsync (opts, [safe, types, chainSlot] (int result)
    {
        if (result == 0)
            return;

        auto* self = safe.getComponent();
        if (self == nullptr)
            return;

        const int i = juce::KnownPluginList::getIndexChosenByMenu (types, result);
        if (juce::isPositiveAndBelow (i, types.size()))
            self->loadDescription (chainSlot, types.getReference (i));
    });
}

void ParamRouletteEditor::showModuleMenu (int chainSlot)
{
    const bool loaded = proc.isSlotLoaded (chainSlot);
    const bool open   = hostedWindows[(size_t) chainSlot] != nullptr;

    juce::PopupMenu m;
    m.addSectionHeader ("链位 "_u8 + juce::String (chainSlot + 1)
                        + (loaded ? " · "_u8 + proc.getSlotName (chainSlot) : juce::String()));
    m.addItem (1, open ? "关闭插件界面"_u8 : "打开插件界面"_u8, loaded);
    m.addItem (2, loaded ? "更换插件…"_u8 : "装一个插件…"_u8);
    m.addItem (3, "旁通"_u8, loaded, proc.isSlotBypassed (chainSlot));
    m.addSeparator();
    m.addItem (4, "卸载"_u8, loaded);

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chain).withMousePosition(),
                     [safe, chainSlot] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr)
            return;

        switch (r)
        {
            case 1: self->openHostedWindow (chainSlot); break;
            case 2: self->showPluginMenu (chainSlot, nullptr); break;
            case 3: self->proc.setSlotBypassed (chainSlot, ! self->proc.isSlotBypassed (chainSlot)); break;
            case 4: self->doUnload (chainSlot); break;
            default: break;
        }
    });
}

void ParamRouletteEditor::showScanWindow()
{
    if (scanWindow != nullptr)
    {
        scanWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    scanWindow = std::make_unique<ToolWindow> ("插件扫描"_u8,
                                               proc.getScanner().createListComponent(),
                                               720, 500, lnf,
                                               [safe]
                                               {
                                                   if (auto* self = safe.getComponent())
                                                       self->closeScanWindow();
                                               });
}

void ParamRouletteEditor::closeScanWindow()
{
    proc.getScanner().savePluginList();
    scanWindow.reset();
}

void ParamRouletteEditor::showTagWindow()
{
    if (tagWindow != nullptr)
    {
        tagWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    tagWindow = std::make_unique<ToolWindow> ("插件功能标签"_u8,
                                              std::make_unique<PluginTagView> (proc.getScanner()),
                                              660, 580, lnf,
                                              [safe]
                                              {
                                                  if (auto* self = safe.getComponent())
                                                      self->closeTagWindow();
                                              });
}

void ParamRouletteEditor::closeTagWindow()
{
    tagWindow.reset();
    updateStatus();     // 标签可能改了，信号链上的颜色要跟着变
}

void ParamRouletteEditor::loadDescription (int chainSlot, const juce::PluginDescription& desc)
{
    // 旧界面必须先死，实例才能换。只关这一位——别的链位的窗口该留着。
    closeHostedWindows (chainSlot);
    setNote ("正在加载 "_u8 + desc.name + " …"_u8);

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    proc.loadPluginIntoSlot (chainSlot, desc, [safe, chainSlot] (const juce::String& error)
    {
        if (auto* self = safe.getComponent())
        {
            self->proc.setFocusedSlot (chainSlot);
            self->rebuildAll();
            self->setNote (error.isEmpty() ? ("链位 "_u8 + juce::String (chainSlot + 1) + " 已装上 "_u8
                                              + self->proc.getSlotName (chainSlot))
                                           : ("加载失败："_u8 + error));
        }
    });
}

void ParamRouletteEditor::closeHostedWindows (int chainSlot)
{
    if (chainSlot < 0)
    {
        for (auto& w : hostedWindows)
            w.reset();

        return;
    }

    if (juce::isPositiveAndBelow (chainSlot, kMaxChain))
        hostedWindows[(size_t) chainSlot].reset();
}

void ParamRouletteEditor::toggleHostedWindow()
{
    // 开关的是**当前聚焦的**链位的界面。想同时看两位就切过去再点一次，
    // 两个窗口会一起留着。
    const int slot = proc.getFocusedSlot();

    if (! juce::isPositiveAndBelow (slot, kMaxChain))
        return;

    if (hostedWindows[(size_t) slot] != nullptr)
    {
        hostedWindows[(size_t) slot].reset();
        updateStatus();
        return;
    }

    openHostedWindow (slot);
}

void ParamRouletteEditor::openHostedWindow (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kMaxChain))
        return;

    auto& win = hostedWindows[(size_t) slot];

    if (win != nullptr)
    {
        win->toFront (true);
        return;
    }

    auto* p = proc.getSlotPlugin (slot);
    if (p == nullptr)
        return;

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    win = std::make_unique<HostedPluginWindow> (*p,
                                                [safe, slot]
                                                {
                                                    if (auto* self = safe.getComponent())
                                                    {
                                                        self->closeHostedWindows (slot);
                                                        self->updateStatus();
                                                    }
                                                });
    updateStatus();
}

void ParamRouletteEditor::doUnload (int chainSlot)
{
    closeHostedWindows (chainSlot);
    proc.unloadSlot (chainSlot);
    setNote ("链位 "_u8 + juce::String (chainSlot + 1) + " 已清空"_u8);
}

void ParamRouletteEditor::applyFilter()
{
    proc.filterText   = teFilter.getText();
    proc.onlyMatching = bOnlyMatching.getToggleState();

    list.setFilter (proc.filterText, proc.onlyMatching);
    updateStatus();
}

void ParamRouletteEditor::showListMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("只作用于当前可见的 "_u8 + juce::String (list.getNumVisible()) + " 行"_u8);
    m.addItem (1, "全选"_u8);
    m.addItem (2, "全不选"_u8);
    m.addItem (3, "反选"_u8);
    m.addSeparator();
    m.addItem (4, "取消勾选所有「写不进」的参数"_u8);

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bListMenu), [safe] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr)
            return;

        switch (r)
        {
            case 1: self->list.checkAll (true);  break;
            case 2: self->list.checkAll (false); break;
            case 3: self->list.invertAll();      break;
            case 4: self->list.uncheckInert();   self->setNote ("已把写不进去的参数移出随机范围"_u8); break;
            default: break;
        }
    });
}

//==============================================================================
void ParamRouletteEditor::pushMotionSettings()
{
    if (syncing)
        return;

    // 强度那两个旋钮**不经过这里**——它们由 wireKnob 直接写各自的可自动化参数。
    // 这里只管语义：它不可自动化，只能走 processor 的成员。
    auto s = proc.getMotionSettings();
    s.semantic = (MotionSemantic) juce::jlimit (0, kNumMotionSemantics - 1, segSemantic.getSelected());
    proc.setMotionSettings (s);
}

void ParamRouletteEditor::promptSaveChainPreset()
{
    // 用异步的 AlertWindow：插件里不能开模态循环（JUCE_MODAL_LOOPS_PERMITTED=0），
    // 那会锁死宿主的消息线程。
    auto* aw = new juce::AlertWindow ("存成链预设"_u8,
                                      "给这条链起个名字。存到 "_u8
                                        + ParamRouletteProcessor::getChainPresetDir().getFullPathName(),
                                      juce::MessageBoxIconType::NoIcon);

    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("name", proc.getHostedName(), "名字"_u8);
    aw->addButton ("存"_u8,   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("取消"_u8, 0, juce::KeyPress (juce::KeyPress::escapeKey));
    ui::variant (*aw->getButton (0), ui::Variant::primary);

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        const auto name = aw->getTextEditorContents ("name");
        aw->setLookAndFeel (nullptr);
        std::unique_ptr<juce::AlertWindow> owned (aw);

        auto* self = safe.getComponent();
        if (self == nullptr || result != 1)
            return;

        const auto err = self->proc.saveChainPreset (name);
        self->setNote (err.isEmpty() ? ("链已存为 "_u8 + name) : err);
    }), false);
}

void ParamRouletteEditor::showChainPresetMenu()
{
    const auto files = proc.listChainPresets();

    juce::PopupMenu m;
    m.addSectionHeader ("链预设"_u8);

    if (files.isEmpty())
    {
        m.addItem (-1, "（还没有存过链预设）"_u8, false);
    }
    else
    {
        for (int i = 0; i < files.size(); ++i)
            m.addItem (i + 1, files[i].getFileNameWithoutExtension());
    }

    m.addSeparator();
    m.addItem (9999, "打开预设文件夹…"_u8);

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bChainLoad),
                     [safe, files] (int choice)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || choice <= 0)
            return;

        if (choice == 9999)
        {
            ParamRouletteProcessor::getChainPresetDir().revealToUser();
            return;
        }

        const auto file = files[choice - 1];
        self->setNote ("正在读 "_u8 + file.getFileNameWithoutExtension() + " …"_u8);

        self->proc.loadChainPreset (file, [safe, file] (const juce::String& error)
        {
            if (auto* s = safe.getComponent())
            {
                s->rebuildAll();
                s->setNote (error.isEmpty() ? ("已读入 "_u8 + file.getFileNameWithoutExtension()) : error);
            }
        });
    });
}

//==============================================================================
void ParamRouletteEditor::confirmProbe()
{
    if (proc.getHosted() == nullptr || proc.getProbe().isRunning())
        return;

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    // 回调收到 1 = 按了第一个按钮，0 = 第二个或按了 Esc。
    // 这个方向是安全的：误关窗不会启动体检。
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        "参数体检"_u8,
        "体检会把每个参数依次扫到 0、中点、满值再还原，用来问出它到底收什么值：\n"
        "有的参数写什么都没用，有的只收两个精确端点、中间值会被插件原地弹回"
        "（fauna 的 eg*.inv 就是后者，强度拉满也一动不动）。\n\n"
        "过程中会出声，而且可能很响。建议先把监听音量拉下来。\n\n"
        "大约需要 "_u8 + juce::String (InertProbe::totalMs()) + " 毫秒。"_u8,
        "开始体检"_u8,
        "取消"_u8,
        this,
        juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (result != 1)
                return;

            if (auto* self = safe.getComponent())
                self->startProbe();
        }));
}

void ParamRouletteEditor::startProbe()
{
    if (proc.getHosted() == nullptr || proc.getProbe().isRunning())
        return;

    setNote ("体检中…别动"_u8);

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    proc.startInertProbe ([safe] (int numInert, int numEndpointOnly)
    {
        auto* self = safe.getComponent();
        if (self == nullptr)
            return;

        juce::String n;

        if (numEndpointOnly > 0)
            n << "认出 "_u8 << numEndpointOnly << " 个只收端点的参数（已改成开关处理，之后随机化只写 0/1）"_u8;

        if (numInert > 0)
        {
            if (n.isNotEmpty()) n << "；"_u8;
            n << numInert << " 个写不进去（列表里标红）"_u8;
        }

        self->setNote (n.isEmpty() ? juce::String ("体检完成：所有参数都正常"_u8) : ("体检完成："_u8 + n));

        // 只为真正写不进去的那批提议取消勾选。"只收端点"的已经被修好了，
        // 把它们踢出随机范围反而是倒退。
        if (numInert > 0)
            self->offerUncheckInert (numInert);
    });
}

void ParamRouletteEditor::offerUncheckInert (int numInert)
{
    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon,
        "体检结果"_u8,
        juce::String (numInert) + " 个参数无论写什么都会被插件弹回原值，"
        "留在随机范围里只会稀释随机化的效果。\n\n要把它们取消勾选吗？"_u8,
        "取消勾选"_u8,
        "保持原样"_u8,
        this,
        juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (result != 1)
                return;

            if (auto* self = safe.getComponent())
            {
                self->list.uncheckInert();
                self->setNote ("已把写不进去的参数移出随机范围"_u8);
            }
        }));
}

} // namespace pr
