#include "PluginEditor.h"

namespace pr
{

namespace
{
    juce::Font uiFont (float h)          { return juce::Font { juce::FontOptions { h } }; }

    void styleCaption (juce::Label& l)
    {
        l.setFont (uiFont (12.0f));
        l.setJustificationType (juce::Justification::centredLeft);
        l.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    }

    /** 扫描窗口。PluginListComponent 自带扫描按钮和进度，这里只给它一个壳。 */
    class ScanWindow final : public juce::DocumentWindow
    {
    public:
        ScanWindow (std::unique_ptr<juce::Component> content, std::function<void()> onCloseRequested)
            : juce::DocumentWindow ("VST3 插件扫描"_u8,
                                    juce::Colour (0xff2b2b2b),
                                    juce::DocumentWindow::closeButton),
              onClose (std::move (onCloseRequested))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), false);
            setResizable (true, false);
            centreWithSize (680, 480);
            setVisible (true);
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
ParamRouletteEditor::ParamRouletteEditor (ParamRouletteProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      list (p.getModel(), p.getChecked()),
      pad (p.getBank())
{
    // 只改自己这一棵树的字体，**不动全局默认**——宿主里别的插件界面不该被我们波及。
    lnf.setDefaultSansSerifTypefaceName ("Microsoft YaHei UI");
    setLookAndFeel (&lnf);

    //== 顶栏 ====================================================================
    for (auto* b : { &bPick, &bScan, &bUI, &bUnload })
        addAndMakeVisible (*b);

    bPick.onClick   = [this] { showPluginMenu(); };
    bScan.onClick   = [this] { showScanWindow(); };
    bUI.onClick     = [this] { toggleHostedWindow(); };
    bUnload.onClick = [this] { doUnload(); };

    lPlugin.setFont (uiFont (14.0f));
    lPlugin.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (lPlugin);

    cbPreset.setTextWhenNoChoicesAvailable ("（无 preset）"_u8);
    cbPreset.setTextWhenNothingSelected ("preset…"_u8);
    cbPreset.onChange = [this]
    {
        if (syncing)
            return;

        const int idx = cbPreset.getSelectedItemIndex();
        if (idx < 0)
            return;

        proc.setHostedProgram (idx);
        note = "已切到 preset "_u8 + juce::String (idx) + " "
             + proc.getHostedProgramName (idx) + "（基座已清空）"_u8;
    };
    addAndMakeVisible (cbPreset);

    //== 随机化 ==================================================================
    for (auto* l : { &lAmount, &lLo, &lHi })
    {
        styleCaption (*l);
        addAndMakeVisible (*l);
    }

    wireSlider (sAmount, *proc.pAmount);
    wireSlider (sLo,     *proc.pRangeLo);
    wireSlider (sHi,     *proc.pRangeHi);

    addAndMakeVisible (bRandom);
    bRandom.onClick = [this]
    {
        note.clear();
        proc.randomizeNow();    // 已经在消息线程上，直接来
    };

    addAndMakeVisible (bProbe);
    bProbe.onClick = [this] { confirmProbe(); };

    styleCaption (lResult);
    addAndMakeVisible (lResult);

    cbMorphMode.addItem ("1D 滑条"_u8, 1);
    cbMorphMode.addItem ("2D 平面"_u8, 2);
    cbMorphMode.setSelectedItemIndex (proc.pMorphMode->getIndex(), juce::dontSendNotification);
    cbMorphMode.onChange = [this]
    {
        if (syncing)
            return;

        const int idx = cbMorphMode.getSelectedItemIndex();
        if (idx < 0)
            return;

        proc.pMorphMode->beginChangeGesture();
        proc.pMorphMode->setValueNotifyingHost (proc.pMorphMode->convertTo0to1 ((float) idx));
        proc.pMorphMode->endChangeGesture();
        pad.setMode (idx == 1 ? MorphMode::TwoD : MorphMode::OneD);
    };
    addAndMakeVisible (cbMorphMode);

    //== 链位一行 ================================================================
    for (int i = 0; i < kMaxChain; ++i)
    {
        auto& ui = chainUI[(size_t) i];

        ui.focus.setButtonText (juce::String (i + 1));
        ui.focus.setTooltip ("链位 "_u8 + juce::String (i + 1)
                             + "。点一下把所有操作切到这一位；"_u8
                             + "「选择插件…」会装到当前聚焦的这一位上。"_u8);
        ui.focus.onClick = [this, i] { proc.setFocusedSlot (i); };

        // 连成一排：JUCE 会把相邻的同 radioGroup 按钮画成连体的
        ui.focus.setConnectedEdges (
            (i == 0 ? 0 : juce::Button::ConnectedOnLeft)
            | (i == kMaxChain - 1 ? 0 : juce::Button::ConnectedOnRight));

        addAndMakeVisible (ui.focus);

        ui.bypass.setButtonText ({});
        ui.bypass.setTooltip ("旁通链位 "_u8 + juce::String (i + 1)
                              + "。旁通是**位置**的属性，换插件不会重置它。"_u8);
        ui.bypass.onClick = [this, i] { proc.setSlotBypassed (i, chainUI[(size_t) i].bypass.getToggleState()); };
        addAndMakeVisible (ui.bypass);
    }

    styleCaption (lGuard);
    lGuard.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (lGuard);

    //== 「只动 N 个」 ============================================================
    styleCaption (lMaxParams);
    addAndMakeVisible (lMaxParams);

    sMaxParams.setSliderStyle (juce::Slider::IncDecButtons);
    sMaxParams.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 18);
    sMaxParams.setRange (0.0, (double) proc.pMaxParams->getRange().getEnd(), 1.0);
    sMaxParams.setValue (proc.pMaxParams->get(), juce::dontSendNotification);

    // 0 不该显示成 "0"——那看着像"一个都不动"。它的意思是"不限制"。
    sMaxParams.textFromValueFunction = [] (double v)
    {
        return v < 0.5 ? juce::String ("不限"_u8) : juce::String ((int) v);
    };
    sMaxParams.valueFromTextFunction = [] (const juce::String& t)
    {
        return t.containsIgnoreCase ("不限"_u8) ? 0.0 : (double) t.getIntValue();
    };

    sMaxParams.onValueChange = [this]
    {
        if (syncing)
            return;

        auto& p = *proc.pMaxParams;
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 ((float) sMaxParams.getValue()));
        p.endChangeGesture();
    };
    addAndMakeVisible (sMaxParams);

    //== 随机化历史 ==============================================================
    // 插件内部没有 undo。没有这两个按钮的话，"上一个其实更好"就只能靠运气再掷回来。
    bHistBack.onClick = [this]
    {
        note = proc.historyBack() ? "← 回到上一个状态"_u8 : "已经是最早的记录了"_u8;
    };
    bHistFwd.onClick = [this]
    {
        note = proc.historyForward() ? "→ 前进到下一个状态"_u8 : "已经是最新的了"_u8;
    };

    for (auto* b : { &bHistBack, &bHistFwd })
    {
        b->setTooltip ("随机化历史。最多记 20 步，只在内存里，不进工程。"_u8);
        addAndMakeVisible (*b);
    }

    styleCaption (lHistory);
    lHistory.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lHistory);

    //== 运动模式 ================================================================
    tbMotion.onClick = [this]
    {
        if (! syncing)
            proc.setMotionMode (tbMotion.getToggleState());
    };
    addAndMakeVisible (tbMotion);

    for (int i = 0; i < kNumMotionSemantics; ++i)
        cbSemantic.addItem (motionSemanticLabel ((MotionSemantic) i), i + 1);

    cbSemantic.onChange = [this] { pushMotionSettings(); };
    addAndMakeVisible (cbSemantic);

    // 运动 / 音色两个强度**是可自动化参数**（pMotionAmt / pToneAmt），所以走
    // wireSlider 那条路，和强度/上下限完全一样。
    //
    // 以前它们是普通成员、画不了自动化，理由写的是"VST3 的参数表不能中途增删"——
    // 那说的是**运行时**。编译期在构造函数里追加是安全的，宿主按参数 ID 匹配自动化。
    for (auto* l : { &lMotionAmt, &lToneAmt })
    {
        styleCaption (*l);
        addAndMakeVisible (*l);
    }

    wireSlider (sMotionAmt, *proc.pMotionAmt);
    wireSlider (sToneAmt,   *proc.pToneAmt);

    bLockBase.onClick = [this]
    {
        note = proc.lockMotionBase()
                 ? ("基座 → "_u8 + proc.getMotionBaseLabel())
                 : "设定失败：没有加载插件"_u8;
    };
    addAndMakeVisible (bLockBase);

    bRestoreBase.onClick = [this]
    {
        note = proc.restoreMotionBase() ? ("已还原到基座 "_u8 + proc.getMotionBaseLabel())
                                        : "还原失败：基座未设定"_u8;
    };
    addAndMakeVisible (bRestoreBase);

    styleCaption (lProfile);
    addAndMakeVisible (lProfile);

    syncMotionUI();

    //== filter ==================================================================
    teFilter.setMultiLine (false);
    teFilter.setReturnKeyStartsNewLine (false);
    teFilter.setTextToShowWhenEmpty ("过滤关键词（空格分隔，任一命中即算匹配）"_u8,
                                     juce::Colour (0xff6a6a6a));
    teFilter.setText (proc.filterText, false);
    teFilter.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (teFilter);

    tbOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);
    tbOnlyMatching.onClick = [this] { applyFilter(); };
    addAndMakeVisible (tbOnlyMatching);

    // 显示范围。和关键词是两条独立筛选，同时生效。
    cbView.addItem ("全部"_u8,   1);
    cbView.addItem ("仅勾选"_u8, 2);
    cbView.addItem ("仅改动"_u8, 3);
    cbView.setSelectedId (1, juce::dontSendNotification);
    cbView.onChange = [this]
    {
        if (syncing)
            return;

        switch (cbView.getSelectedId())
        {
            case 2:  list.setViewMode (ParamListView::ViewMode::CheckedOnly); break;
            case 3:  list.setViewMode (ParamListView::ViewMode::ChangedOnly); break;
            default: list.setViewMode (ParamListView::ViewMode::All);         break;
        }
        updateStatus();
    };
    addAndMakeVisible (cbView);

    for (auto* b : { &bCheckMatch, &bUncheckMatch, &bAll, &bNone, &bInvert })
        addAndMakeVisible (*b);

    bCheckMatch  .onClick = [this] { list.applyToMatching (true); };
    bUncheckMatch.onClick = [this] { list.applyToMatching (false); };
    bAll         .onClick = [this] { list.checkAll (true); };
    bNone        .onClick = [this] { list.checkAll (false); };
    bInvert      .onClick = [this] { list.invertAll(); };

    //== 列表 ====================================================================
    styleCaption (lStatus);
    addAndMakeVisible (lStatus);

    list.onChecksChanged = [this]
    {
        proc.saveExclusionsNow();
        updateStatus();
    };
    addAndMakeVisible (list);

    //== slot + morph ============================================================
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& s = slotUI[(size_t) i];

        s.capture.setButtonText ("存 "_u8 + juce::String (i + 1));
        s.recall .setButtonText ("召回"_u8);
        s.clear  .setButtonText ("×"_u8);

        s.capture.onClick = [this, i]
        {
            proc.captureSlot (i);
            note = "已存入 Slot "_u8 + juce::String (i + 1);
        };
        s.recall.onClick = [this, i]
        {
            proc.recallSlot (i);
            note = "已召回 Slot "_u8 + juce::String (i + 1);
        };
        s.clear.onClick = [this, i]
        {
            proc.clearSlot (i);
            note = "已清空 Slot "_u8 + juce::String (i + 1);
        };

        addAndMakeVisible (s.capture);
        addAndMakeVisible (s.recall);
        addAndMakeVisible (s.clear);
    }

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

    //== 挂钩 ====================================================================
    // 换插件前 processor 会**同步**调这个把子插件窗口关掉。装在这里、dtor 里第一件事就摘掉。
    proc.closeHostedWindow = [this] (int chainSlot) { closeHostedWindows (chainSlot); };
    proc.addChangeListener (this);

    setResizable (true, true);
    // 最小宽度提到 680：随机化那一行现在还要塞「只动 N 个」和历史前后翻，
    // 640 下 lResult 会被挤到只剩一百多像素，运动模式那句诊断根本看不全。
    setResizeLimits (680, 560, 1600, 1200);
    setSize (800, 690);

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

    setLookAndFeel (nullptr);
}

//==============================================================================
void ParamRouletteEditor::wireSlider (juce::Slider& s, juce::AudioParameterFloat& p)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 18);
    s.setRange (0.0, 1.0, 0.001);
    s.setValue (p.get(), juce::dontSendNotification);

    s.onDragStart = [&p] { p.beginChangeGesture(); };
    s.onDragEnd   = [&p] { p.endChangeGesture(); };
    s.onValueChange = [this, &s, &p]
    {
        if (! syncing)
            p.setValueNotifyingHost (p.convertTo0to1 ((float) s.getValue()));
    };

    addAndMakeVisible (s);
}

void ParamRouletteEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}

void ParamRouletteEditor::resized()
{
    auto r = getLocalBounds().reduced (8);

    {   // 顶栏
        auto row = r.removeFromTop (28);
        bPick.setBounds (row.removeFromLeft (92));
        row.removeFromLeft (4);
        bScan.setBounds (row.removeFromLeft (60));
        bUnload.setBounds (row.removeFromRight (52));
        row.removeFromRight (4);
        bUI.setBounds (row.removeFromRight (52));
        row.removeFromRight (8);
        cbPreset.setBounds (row.removeFromRight (190));
        row.removeFromRight (8);
        lPlugin.setBounds (row);
    }

    r.removeFromTop (6);

    {   // 链位一行：6 个聚焦按钮 + 各自的旁通勾选，右边是 limiter 读数
        auto row = r.removeFromTop (24);

        lGuard.setBounds (row.removeFromRight (180));
        row.removeFromRight (8);

        const int cellW = row.getWidth() / kMaxChain;

        for (int i = 0; i < kMaxChain; ++i)
        {
            auto cell = (i == kMaxChain - 1) ? row : row.removeFromLeft (cellW);
            chainUI[(size_t) i].bypass.setBounds (cell.removeFromRight (22));
            chainUI[(size_t) i].focus .setBounds (cell);
        }
    }

    r.removeFromTop (6);

    {   // 强度 / 下限 / 上限
        auto row = r.removeFromTop (24);
        const int w = row.getWidth() / 3;

        auto c1 = row.removeFromLeft (w);
        auto c2 = row.removeFromLeft (w);
        auto c3 = row;

        auto put = [] (juce::Rectangle<int> c, juce::Label& lab, juce::Slider& sl)
        {
            lab.setBounds (c.removeFromLeft (32));
            sl .setBounds (c.withTrimmedRight (8));
        };

        put (c1, lAmount, sAmount);
        put (c2, lLo, sLo);
        put (c3, lHi, sHi);
    }

    r.removeFromTop (6);

    {   // 随机化 / 体检 / 结果 / 只动 N / 历史 / morph 模式
        auto row = r.removeFromTop (26);
        bRandom.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (4);
        bProbe.setBounds (row.removeFromLeft (56));

        // 从右往左切，lResult 吃掉中间剩下的
        cbMorphMode.setBounds (row.removeFromRight (88));
        row.removeFromRight (10);

        bHistFwd .setBounds (row.removeFromRight (26));
        lHistory .setBounds (row.removeFromRight (44));
        bHistBack.setBounds (row.removeFromRight (26));
        row.removeFromRight (10);

        sMaxParams.setBounds (row.removeFromRight (78));
        lMaxParams.setBounds (row.removeFromRight (30));
        row.removeFromRight (8);

        lResult.setBounds (row);
    }

    r.removeFromTop (6);

    {   // 运动模式
        auto row = r.removeFromTop (26);
        tbMotion.setBounds (row.removeFromLeft (86));
        row.removeFromLeft (4);
        cbSemantic.setBounds (row.removeFromLeft (104));
        row.removeFromLeft (10);

        bRestoreBase.setBounds (row.removeFromRight (76));
        row.removeFromRight (4);
        bLockBase.setBounds (row.removeFromRight (76));
        row.removeFromRight (10);

        const int w = row.getWidth() / 2;
        auto c1 = row.removeFromLeft (w);
        auto c2 = row;

        lMotionAmt.setBounds (c1.removeFromLeft (32));
        sMotionAmt.setBounds (c1.withTrimmedRight (8));
        lToneAmt  .setBounds (c2.removeFromLeft (32));
        sToneAmt  .setBounds (c2);
    }

    r.removeFromTop (2);
    lProfile.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);

    r.removeFromTop (6);

    {   // filter 栏。从右往左切，输入框吃掉剩下的。
        auto row = r.removeFromTop (26);
        bInvert.setBounds (row.removeFromRight (52));
        row.removeFromRight (4);
        bNone.setBounds (row.removeFromRight (58));
        row.removeFromRight (4);
        bAll.setBounds (row.removeFromRight (52));
        row.removeFromRight (10);
        bUncheckMatch.setBounds (row.removeFromRight (74));
        row.removeFromRight (4);
        bCheckMatch.setBounds (row.removeFromRight (74));
        row.removeFromRight (10);
        cbView.setBounds (row.removeFromRight (84));
        row.removeFromRight (6);
        tbOnlyMatching.setBounds (row.removeFromRight (68));
        row.removeFromRight (6);
        teFilter.setBounds (row);
    }

    r.removeFromTop (4);
    lStatus.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    {   // 底部：slot 一行 + morph。高度固定，切换 1D/2D 时窗口不跳。
        auto bottom = r.removeFromBottom (200);
        auto slotRow = bottom.removeFromTop (26);
        const int sw = slotRow.getWidth() / kNumSlots;

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto cell = (i == kNumSlots - 1) ? slotRow : slotRow.removeFromLeft (sw);
            cell.removeFromRight (6);

            auto& s = slotUI[(size_t) i];
            s.clear .setBounds (cell.removeFromRight (24));
            cell.removeFromRight (3);
            s.recall.setBounds (cell.removeFromRight (46));
            cell.removeFromRight (3);
            s.capture.setBounds (cell);
        }

        pad.setBounds (bottom);
    }

    r.removeFromBottom (6);
    list.setBounds (r);
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
    refreshSlotButtons();
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

void ParamRouletteEditor::pushMotionSettings()
{
    if (syncing)
        return;

    // 强度那两个滑条**不经过这里**——它们由 wireSlider 直接写各自的可自动化参数。
    // 这里只管语义：它不可自动化，只能走 processor 的成员。
    // 所以先把当前设置读出来，只覆盖 semantic 再写回，免得把参数值来回搬一趟。
    auto s = proc.getMotionSettings();
    s.semantic = (MotionSemantic) juce::jlimit (0, kNumMotionSemantics - 1,
                                                cbSemantic.getSelectedItemIndex());
    proc.setMotionSettings (s);
}

void ParamRouletteEditor::syncMotionUI()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto s = proc.getMotionSettings();

    tbMotion.setToggleState (proc.isMotionMode(), juce::dontSendNotification);

    const int idx = juce::jlimit (0, kNumMotionSemantics - 1, (int) s.semantic);
    if (cbSemantic.getSelectedItemIndex() != idx)
        cbSemantic.setSelectedItemIndex (idx, juce::dontSendNotification);

    // 两个强度滑条现在跟着参数走，统一在 syncFromParameters 里回写。
}

void ParamRouletteEditor::refreshChainUI()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    const int focus = proc.getFocusedSlot();

    for (int i = 0; i < kMaxChain; ++i)
    {
        auto& ui = chainUI[(size_t) i];
        const bool loaded = proc.isSlotLoaded (i);

        // 按钮上写 "序号 插件名"。名字截短——六个格子分不到多少像素，
        // 而且完整名字在顶栏那一行已经有了。
        juce::String t (i + 1);
        if (loaded)
        {
            auto n = proc.getSlotName (i);
            t << " " << (n.length() > 9 ? n.substring (0, 8) + "…"_u8 : n);
        }
        ui.focus.setButtonText (t);

        // 三种状态要一眼分得开：聚焦的、装了插件的、空的。
        ui.focus.setColour (juce::TextButton::buttonColourId,
                            i == focus       ? juce::Colour (0xff3d6b7d)
                            : loaded         ? juce::Colour (0xff3a3a3a)
                                             : juce::Colour (0xff2a2a2a));
        ui.focus.setColour (juce::TextButton::textColourOffId,
                            loaded ? juce::Colour (0xffe0e0e0) : juce::Colour (0xff707070));

        ui.bypass.setToggleState (proc.isSlotBypassed (i), juce::dontSendNotification);
        ui.bypass.setEnabled (loaded);
    }

    // —— 链尾安全网的读数。
    // NaN 优先显示：那是"某个子插件坏了"，比增益衰减要紧得多。
    if (proc.consumeSawNonFinite())
        guardWarnUntil = juce::Time::getMillisecondCounter() + 4000;

    const bool warn = juce::Time::getMillisecondCounter() < guardWarnUntil;

    if (warn)
    {
        lGuard.setText ("⚠ 子插件输出了 NaN，已清零"_u8, juce::dontSendNotification);
        lGuard.setColour (juce::Label::textColourId, juce::Colour (0xffd9544f));
    }
    else if (const float gr = proc.getGuardGrDb(); gr > 0.1f)
    {
        lGuard.setText ("限幅 −"_u8 + juce::String (gr, 1) + " dB"_u8, juce::dontSendNotification);
        lGuard.setColour (juce::Label::textColourId, juce::Colour (0xffd9a441));
    }
    else
    {
        lGuard.setText ({}, juce::dontSendNotification);
    }
}

void ParamRouletteEditor::refreshHistoryUI()
{
    const bool has = proc.getHosted() != nullptr;

    bHistBack.setEnabled (has && proc.canHistoryBack());
    bHistFwd .setEnabled (has && proc.canHistoryForward());

    const int n = proc.getHistoryCount();
    lHistory.setText (n > 1 ? (juce::String (proc.getHistoryPos() + 1) + "/" + juce::String (n))
                            : juce::String ("—"_u8),
                      juce::dontSendNotification);
}

void ParamRouletteEditor::rebuildAll()
{
    lastGeneration = proc.getStructureGeneration();
    note.clear();

    lPlugin.setText (proc.getHosted() != nullptr ? proc.getHostedName() : juce::String ("（未加载插件）"_u8),
                     juce::dontSendNotification);

    // 恢复工程时 filter 是从状态里读回来的，界面要跟上。
    if (teFilter.getText() != proc.filterText)
        teFilter.setText (proc.filterText, false);

    tbOnlyMatching.setToggleState (proc.onlyMatching, juce::dontSendNotification);

    list.setFilter (proc.filterText, proc.onlyMatching);
    list.rebuild();

    rebuildPresetList();
    refreshSlotButtons();
    syncMotionUI();
    updateStatus();
    repaint();
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
    cbPreset.setSelectedItemIndex (juce::jmax (0, proc.getHostedProgram()),
                                   juce::dontSendNotification);
}

void ParamRouletteEditor::refreshSlotButtons()
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& s = slotUI[(size_t) i];
        const bool used = proc.getBank().isUsed (i);

        s.recall.setEnabled (used);
        s.clear .setEnabled (used);

        s.capture.setColour (juce::TextButton::buttonColourId,
                             used ? slotColour (i).withMultipliedBrightness (0.55f)
                                  : juce::Colour (0xff3a3a3a));
    }

    pad.repaint();
}

void ParamRouletteEditor::syncFromParameters()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    // 用户正按着的控件不回写，否则自动化和手会互相拽。
    if (! sAmount.isMouseButtonDown()) sAmount.setValue (proc.pAmount->get(),  juce::dontSendNotification);
    if (! sLo    .isMouseButtonDown()) sLo    .setValue (proc.pRangeLo->get(), juce::dontSendNotification);
    if (! sHi    .isMouseButtonDown()) sHi    .setValue (proc.pRangeHi->get(), juce::dontSendNotification);

    // 运动 / 音色 / 只动 N 个也是可自动化参数，同一条回写路径
    if (! sMotionAmt.isMouseButtonDown()) sMotionAmt.setValue (proc.pMotionAmt->get(), juce::dontSendNotification);
    if (! sToneAmt  .isMouseButtonDown()) sToneAmt  .setValue (proc.pToneAmt->get(),   juce::dontSendNotification);
    if (! sMaxParams.isMouseButtonDown()) sMaxParams.setValue (proc.pMaxParams->get(), juce::dontSendNotification);

    pad.setPosition (proc.pMorphX->get(), proc.pMorphY->get());   // 拖动中它自己会拒绝

    const int modeIdx = proc.pMorphMode->getIndex();
    if (cbMorphMode.getSelectedItemIndex() != modeIdx)
        cbMorphMode.setSelectedItemIndex (modeIdx, juce::dontSendNotification);

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

    if (proc.getHosted() == nullptr)
        return note.isNotEmpty() ? note
                                 : juce::String ("未加载插件 —— 点左上角「选择插件…」挑一个 VST3"_u8);

    juce::String s;
    s << "参数 "_u8 << proc.getModel().size()
      << "  ·  勾选 "_u8 << list.getNumChecked() << " / 可见 "_u8 << list.getNumVisible()
      << "  ·  快照 "_u8 << proc.getBank().numUsed() << " / " << kNumSlots;

    if (const int np = proc.getHostedNumPrograms(); np > 1)
        s << "  ·  preset "_u8 << proc.getHostedProgram() << " / " << np;

    // 一个都没勾的时候随机化会**静默无事发生**，必须说出来。
    // 勾选状态是按插件持久化的（跨实例记忆），所以这个 0 可能是上次留下的。
    if (list.getNumChecked() == 0 && proc.getModel().size() > 0)
        s << "   ⚠ 一个都没勾，随机化不会有任何变化——点「全选」"_u8;

    if (proc.getProbe().isRunning())
        s << "  ·  体检中…"_u8;

    if (note.isNotEmpty())
        s << "  ·  "_u8 << note;

    return s;
}

void ParamRouletteEditor::updateStatus()
{
    lStatus.setText (buildStatusText(), juce::dontSendNotification);

    const bool has    = proc.getHosted() != nullptr;
    const bool motion = proc.isMotionMode();

    // 一个都没勾是个静默陷阱，状态行整条变黄
    lStatus.setColour (juce::Label::textColourId,
                       (has && list.getNumChecked() == 0 && ! proc.getModel().isEmpty())
                           ? juce::Colour (0xffd9a441) : juce::Colour (0xff9a9a9a));

    juce::String res;
    const auto r = proc.getLastResult();

    if (r.touched > 0)
    {
        res << (motion ? "上次生成："_u8 : "上次随机："_u8)
            << "改动 "_u8 << proc.getLastChangedCount() << " / 写入 "_u8 << r.touched;

        if (const auto m = proc.getLastMotion(); motion && m.note.isNotEmpty())
            res << "   " << m.note;
        else if (! motion && r.looksSuspicious())
            res << "   ⚠ 大量参数纹丝不动，跑一次「体检」看看"_u8;
    }

    lResult.setText (res, juce::dontSendNotification);
    lResult.setColour (juce::Label::textColourId,
                       (! motion && r.looksSuspicious()) ? juce::Colour (0xffd9a441)
                                                         : juce::Colour (0xff9a9a9a));

    // 运动层的角色识别情况。认不出包络就直说——不然用户会以为语义没生效是 bug。
    juce::String prof;
    if (has)
    {
        const auto& p = proc.getMotionProfile();
        prof = (motion && ! p.hasMotion() ? juce::String ("⚠ "_u8) : juce::String()) + p.summary();

        // 曲线能不能改是两回事：轮廓点没暴露成参数时，唯一的路是插件的状态块。
        const int cg = proc.getContourGroups();
        prof << (cg > 0 ? ("  ·  曲线可写 "_u8 + juce::String (cg) + " 条"_u8)
                        : juce::String ("  ·  曲线不可写（格式不认识）"_u8));

        // 基座身份必须显示出来。只写"已锁定"的话，"我到底锁没锁上、锁的是哪一个"
        // 完全看不出来——出问题时既没法自查也没法报告。#N 每次重设 +1。
        if (motion)
            prof << "  ·  基座："_u8 << proc.getMotionBaseLabel();
    }
    lProfile.setText (prof, juce::dontSendNotification);
    lProfile.setColour (juce::Label::textColourId,
                        (motion && has && ! proc.getMotionProfile().hasMotion())
                            ? juce::Colour (0xffd9a441) : juce::Colour (0xff8a8a8a));

    bUI    .setEnabled (has);
    bUnload.setEnabled (has);
    bRandom.setEnabled (has);
    bProbe .setEnabled (has && ! proc.getProbe().isRunning());
    const bool winOpen = juce::isPositiveAndBelow (proc.getFocusedSlot(), kMaxChain)
                      && hostedWindows[(size_t) proc.getFocusedSlot()] != nullptr;
    bUI.setButtonText (winOpen ? "关界面"_u8 : "界面"_u8);

    // 「只动 N 个」对普通随机化和运动模式的音色层都有效，所以只要有插件就能调
    sMaxParams.setEnabled (has);
    refreshHistoryUI();
    refreshChainUI();

    // 一个按钮两种行为，文字得跟着换，不然没法知道现在按下去会发生什么
    bRandom.setButtonText (motion ? "生成变体"_u8 : "随机化"_u8);

    // 强度 / 上下限只对普通随机化有意义；运动模式用的是它自己那两个强度
    sAmount.setEnabled (has && ! motion);
    sLo    .setEnabled (has && ! motion);
    sHi    .setEnabled (has && ! motion);

    tbMotion    .setEnabled (has);
    cbSemantic  .setEnabled (has && motion);
    sMotionAmt  .setEnabled (has && motion);
    sToneAmt    .setEnabled (has && motion);
    bLockBase   .setEnabled (has && motion);
    bRestoreBase.setEnabled (has && motion && proc.hasMotionBase());

    // 已设定之后动词换成"重设"，一眼能看出这一下是**覆盖**不是首次设定
    bLockBase.setButtonText (proc.hasMotionBase() ? "重设基座"_u8 : "设为基座"_u8);

    for (int i = 0; i < kNumSlots; ++i)
        slotUI[(size_t) i].capture.setEnabled (has);
}

//==============================================================================
void ParamRouletteEditor::showPluginMenu()
{
    auto types = proc.getScanner().getKnownPlugins().getTypes();

    if (types.isEmpty())
    {
        // 一个都没扫过就弹空菜单，用户只会以为坏了。直接把他领到扫描窗口。
        note = "插件表是空的，先扫描一次"_u8;
        showScanWindow();
        return;
    }

    juce::PopupMenu m;

    // 必须用**静态**版本：非静态的那两个在 8.0.10 里已标 deprecated。
    juce::KnownPluginList::addToMenu (m, types,
                                      juce::KnownPluginList::sortByManufacturer,
                                      proc.getHostedId());

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bPick),
                     [safe, types] (int result)
                     {
                         if (result == 0)
                             return;

                         auto* self = safe.getComponent();
                         if (self == nullptr)
                             return;

                         const int i = juce::KnownPluginList::getIndexChosenByMenu (types, result);
                         if (juce::isPositiveAndBelow (i, types.size()))
                             self->loadDescription (types.getReference (i));
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

    scanWindow = std::make_unique<ScanWindow> (proc.getScanner().createListComponent(),
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

void ParamRouletteEditor::loadDescription (const juce::PluginDescription& desc)
{
    // 旧界面必须先死，实例才能换。只关这一位——别的链位的窗口该留着。
    closeHostedWindows (proc.getFocusedSlot());
    note = "正在加载 "_u8 + desc.name + " …"_u8;
    updateStatus();

    juce::Component::SafePointer<ParamRouletteEditor> safe (this);

    proc.loadPlugin (desc, [safe] (const juce::String& error)
    {
        if (auto* self = safe.getComponent())
        {
            self->note = error.isEmpty() ? juce::String() : ("加载失败："_u8 + error);
            self->rebuildAll();

            // rebuildAll 会清 note，失败信息得再写一次才留得住。
            if (error.isNotEmpty())
            {
                self->note = "加载失败："_u8 + error;
                self->updateStatus();
            }
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

    auto& win = hostedWindows[(size_t) slot];

    if (win != nullptr)
    {
        win.reset();
        updateStatus();
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

void ParamRouletteEditor::doUnload()
{
    closeHostedWindows (proc.getFocusedSlot());
    proc.unloadPlugin();
    note.clear();
}

void ParamRouletteEditor::applyFilter()
{
    proc.filterText   = teFilter.getText();
    proc.onlyMatching = tbOnlyMatching.getToggleState();

    list.setFilter (proc.filterText, proc.onlyMatching);
    updateStatus();
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
        "体检会把每个参数依次扫到 0、中点、满值再还原，用来问出它到底**收什么值**：\n"
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

    note = "体检中…别动"_u8;
    updateStatus();

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

        self->note = n.isEmpty() ? juce::String ("体检完成：所有参数都正常"_u8) : ("体检完成："_u8 + n);
        self->updateStatus();

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
                self->note = "已把写不进去的参数移出随机范围"_u8;
                self->updateStatus();
            }
        }));
}

} // namespace pr
