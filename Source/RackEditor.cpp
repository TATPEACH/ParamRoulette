#include "RackEditor.h"

namespace pr
{

using theme::Icon;

namespace
{
    constexpr int   kMenuH  = 26;
    constexpr float kRowW   = 85.0f * 15.0f;     ///< 一排 85 HP
    constexpr float kRowH   = 380.0f;            ///< 3U

    juce::String percentText (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
}

//==============================================================================
void RackEditor::Backdrop::paint (juce::Graphics& g)
{
    // Rack 的机架是无限延伸的导轨；这里两排模块上下露出来的地方也接着画导轨
    const float rowH = kRowH * zoom;
    if (rowH < 1.0f)
        return;

    const float start = rowTop - std::ceil (rowTop / rowH) * rowH;
    for (float y = start; y < (float) getHeight(); y += rowH)
        rk::paintRails (g, { 0.0f, y, (float) getWidth(), rowH }, zoom);
}

//==============================================================================
RackEditor::RackEditor (ParamRouletteProcessor& p, EditorHost& h)
    : EditorBase (p, h, h.lookAndFeelFor (Skin::rack)),
      scope (p.getBank()),
      browser (p.getScanner()),
      meterTimer ([this] { meterTick(); })
{
    rk::loadView (viewFile());

    addAndMakeVisible (backdrop);
    addAndMakeVisible (menuBar);
    buildMenus();

    //== 第一排：信号链 ==========================================================
    chain.onFocus       = [this] (int i) { proc.setFocusedSlot (i); };
    chain.onAdd         = [this] (int i) { openBrowser (i); };
    chain.onBypass      = [this] (int i, bool b) { setSlotBypass (i, b); };
    chain.onOpenEditor  = [this] (int i) { host.openHostedWindow (i); };
    chain.onContextMenu = [this] (int i) { showRackModuleMenu (i); };
    chain.onGuardToggle = [this] (bool on) { setGuardOn (on); };
    chain.onMove        = [this] (int from, int to) { doMove (from, to); };
    chain.onProgramMenu = [this] (int i) { showProgramMenu (i); };
    chain.onEditToggle  = [this] (int i)
    {
        if (host.isHostedWindowOpen (i)) { host.closeHostedWindows (i); updateStatus(); }
        else                             { host.openHostedWindow (i); }
    };
    chain.onCablesChanged = [this] { cables.repaint(); };
    addAndMakeVisible (chain);

    kCeiling.label = "天花板"_u8;
    kCeiling.setRange (proc.pCeiling->range.start, proc.pCeiling->range.end, 0.1);
    kCeiling.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    kCeiling.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getDoubleValue(); };
    kCeiling.setDoubleClickReturnValue (true, -1.0);
    kCeiling.getProperties().set ("rkQuick", "-0.3,-1,-3,-6");
    kCeiling.onTip  = [this] (RackKnob& k, bool show) { if (show) tip.show (k.tipText(), getLocalArea (&k, k.getLocalBounds())); else tip.setVisible (false); };
    kCeiling.onMenu = [this] (RackKnob& k) { showKnobMenu (k); };
    bindSlider (kCeiling, *proc.pCeiling);
    addAndMakeVisible (kCeiling);

    //== RANDOM ==================================================================
    addAndMakeVisible (mRandom);

    wireKnob (kAmount, *proc.pAmount, "强度"_u8);
    kAmount.setDoubleClickReturnValue (true, 1.0);
    kAmount.getProperties().set ("rkQuick", "0,0.25,0.5,0.75,1");
    mRandom.addAndMakeVisible (kAmount);

    wireRangeKnobs();
    mRandom.addAndMakeVisible (kLo);
    mRandom.addAndMakeVisible (kHi);

    kMax.label = "每次只动"_u8;
    kMax.setRange (0.0, (double) proc.pMaxParams->getRange().getEnd(), 1.0);
    kMax.textFromValueFunction = [] (double v) { return v <= 0.0 ? juce::String ("不限"_u8) : juce::String (juce::roundToInt (v)) + " 个"_u8; };
    kMax.valueFromTextFunction = [] (const juce::String& t) { return (double) t.retainCharacters ("0123456789").getIntValue(); };
    kMax.setDoubleClickReturnValue (true, 0.0);
    kMax.getProperties().set ("rkQuick", "0,1,2,4,8");
    kMax.onTip  = [this] (RackKnob& k, bool show) { if (show) tip.show (k.tipText(), getLocalArea (&k, k.getLocalBounds())); else tip.setVisible (false); };
    kMax.onMenu = [this] (RackKnob& k) { showKnobMenu (k); };
    kMax.onDragStart   = [this] { proc.pMaxParams->beginChangeGesture(); };
    kMax.onDragEnd     = [this] { proc.pMaxParams->endChangeGesture(); };
    kMax.onValueChange = [this]
    {
        if (syncing)
            return;
        auto& prm = *proc.pMaxParams;
        prm.setValueNotifyingHost (prm.convertTo0to1 ((float) kMax.getValue()));
        lMax.setText (kMax.getTextFromValue (kMax.getValue()));
    };
    mRandom.addAndMakeVisible (kMax);

    lMax.setJustification (juce::Justification::centred);
    lMax.setTooltip ("每次最多改动几个参数（不限 = 全部勾选的都动）。只动 1~2 个时每次改动都能听出是哪一维在起作用；"_u8
                     "运动模式下只限制音色层。拧右边的小旋钮，双击它回到「不限」"_u8);
    lMax.setInterceptsMouseClicks (true, false);
    mRandom.addAndMakeVisible (lMax);

    bProbe.setTooltip ("体检：问出每个参数到底收什么值，认出只收端点的伪开关。会出声！跑的时候旁边的灯亮"_u8);
    bProbe.onClick = [this] { confirmProbe(); };
    mRandom.addAndMakeVisible (bProbe);

    bHistBack.setTooltip ("回到上一个状态（Ctrl+Z）。最多记 20 步，只在内存里，不进工程"_u8);
    bHistBack.onClick = [this] { doHistory (true); };
    bHistFwd.setTooltip ("前进到下一个状态（Ctrl+Y）"_u8);
    bHistFwd.onClick = [this] { doHistory (false); };
    mRandom.addAndMakeVisible (bHistBack);
    mRandom.addAndMakeVisible (bHistFwd);

    lHistory.setJustification (juce::Justification::centred);
    mRandom.addAndMakeVisible (lHistory);

    bRoll.onClick = [this] { doRoll(); };
    mRandom.addAndMakeVisible (bRoll);

    lResult.setTextSize (9.5f);
    lResult.setJustification (juce::Justification::topLeft);
    mRandom.addAndMakeVisible (lResult);

    //== MOTION ==================================================================
    addAndMakeVisible (mMotion);

    swMotion.setTooltip ("运动模式：按「上扬 / 下降 / 先扬后抑」这种音乐意图生成变体，而不是逐参数掷骰子。"_u8
                         "拨上去 = 开，大按键变成「生成变体」"_u8);
    swMotion.onChange = [this] (int pos) { setMotionOn (pos == 0); };
    mMotion.addAndMakeVisible (swMotion);

    {
        std::vector<SegmentedControl::Item> items;
        for (int i = 0; i < kNumMotionSemantics; ++i)
        {
            const auto sem = (MotionSemantic) i;
            items.push_back ({ motionSemanticLabel (sem), motionSemanticLabel (sem) + "："_u8 + semanticHint (sem),
                               [sem] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
                               { theme::drawSemanticGlyph (g, sem, r, c, 1.6f); } });
        }
        selSemantic.setItems (std::move (items));
    }
    selSemantic.setLightColour (rk::cyan);
    selSemantic.onChange = [this] (int i) { setSemantic (i); };
    mMotion.addAndMakeVisible (selSemantic);

    wireKnob (kMotion, *proc.pMotionAmt, "运动"_u8);
    kMotion.setDoubleClickReturnValue (true, 0.65);
    kMotion.setTooltip ({});
    mMotion.addAndMakeVisible (kMotion);

    wireKnob (kTone, *proc.pToneAmt, "音色"_u8);
    kTone.setDoubleClickReturnValue (true, 0.35);
    mMotion.addAndMakeVisible (kTone);

    bLock.setTooltip ("把当前状态存成生成变体的出发点。已设定时会覆盖旧的（#N 会 +1）"_u8);
    bLock.onClick = [this] { lockBase(); };
    bRestore.setTooltip ("精确回到基座（用子插件的完整状态块还原）"_u8);
    bRestore.onClick = [this] { restoreBase(); };
    mMotion.addAndMakeVisible (bLock);
    mMotion.addAndMakeVisible (bRestore);

    lSemantic.setTextSize (10.0f);
    mMotion.addAndMakeVisible (lSemantic);
    lProfile.setTextSize (9.0f);
    lProfile.setJustification (juce::Justification::topLeft);
    lProfile.setInterceptsMouseClicks (true, false);
    mMotion.addAndMakeVisible (lProfile);

    //== PARAMETERS ==============================================================
    addAndMakeVisible (mParams);

    teFilter.getProperties().set ("rkLcd", true);
    teFilter.getProperties().set ("prSearch", true);
    teFilter.setIndents (26, 0);
    teFilter.setJustification (juce::Justification::centredLeft);
    teFilter.setFont (rk::uiFont (12.0f));
    teFilter.setColour (juce::TextEditor::textColourId, rk::yellow);
    teFilter.setColour (juce::CaretComponent::caretColourId, rk::yellow);
    teFilter.setColour (juce::TextEditor::highlightColourId, rk::yellow.withAlpha (0.3f));
    teFilter.setMultiLine (false);
    teFilter.setReturnKeyStartsNewLine (false);
    teFilter.setTextToShowWhenEmpty ("筛选参数名   Ctrl+F"_u8, rk::yellow.withAlpha (0.4f));
    teFilter.setText (proc.filterText, false);
    teFilter.onTextChange = [this] { applyFilter (teFilter.getText(), onlyMatching); };
    teFilter.onEscapeKey  = [this]
    {
        teFilter.clear();
        applyFilter (teFilter.getText(), onlyMatching);
        grabKeyboardFocus();
    };
    mParams.addAndMakeVisible (teFilter);

    onlyMatching = proc.onlyMatching;
    bOnly.setTooltip ("只显示名字匹配关键词的参数（灯亮 = 开）"_u8);
    bOnly.onClick = [this]
    {
        onlyMatching = ! onlyMatching;
        applyFilter (teFilter.getText(), onlyMatching);
    };
    bCheck.setTooltip ("把全部匹配关键词的参数勾上（包括被「仅匹配」藏起来的）"_u8);
    bCheck.onClick = [this] { checkMatching (true); };
    bUncheck.setTooltip ("把全部匹配关键词的参数取消勾选——「凡是叫这个的都别动」"_u8);
    bUncheck.onClick = [this] { checkMatching (false); };
    bListMenu.setTooltip ("全选 / 全不选 / 反选（只作用于当前可见的行）"_u8);
    bListMenu.onClick = [this] { showListMenu (bListMenu); };

    for (auto* b : { &bOnly, &bCheck, &bUncheck, &bListMenu })
        mParams.addAndMakeVisible (*b);

    segView.setItems ({ { "全部"_u8,   "显示全部参数"_u8, {} },
                        { "已勾选"_u8, "只看勾选了的——回答「我到底勾了哪些」"_u8, {} },
                        { "已改动"_u8, "只看上一次操作真的改动了的"_u8, {} } });
    segView.setLightColour (rk::yellow);
    segView.onChange = [this] (int i)
    {
        list.setViewMode (i == 1 ? ParamListView::ViewMode::CheckedOnly
                        : i == 2 ? ParamListView::ViewMode::ChangedOnly
                                 : ParamListView::ViewMode::All);
        updateStatus();
    };
    mParams.addAndMakeVisible (segView);

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
    mParams.addAndMakeVisible (list);

    //== MEMORY / MORPH ==========================================================
    addAndMakeVisible (mMemory);
    memory.onStore  = [this] (int i) { storeSnapshot (i); };
    memory.onRecall = [this] (int i) { recallSnapshot (i); };
    memory.onClear  = [this] (int i) { clearSnapshot (i); };
    mMemory.addAndMakeVisible (memory);

    addAndMakeVisible (mMorph);
    swMorph.setTooltip ("上 = 1D（已用快照按序号排成一条线），下 = 2D（四角固定绑快照 1/2/3/4）"_u8);
    swMorph.onChange = [this] (int pos)
    {
        setMorphModeIndex (pos);
        scope.setMode (pos == 1 ? MorphMode::TwoD : MorphMode::OneD);
    };
    mMorph.addAndMakeVisible (swMorph);

    lWeights.setJustification (juce::Justification::centred);
    mMorph.addAndMakeVisible (lWeights);

    scope.setMode (proc.getMorphMode());
    scope.setPosition (proc.pMorphX->get(), proc.pMorphY->get());
    scope.onDragStart       = [this] { morphDragStart(); };
    scope.onPositionChanged = [this] (float x, float y) { morphMoved (x, y); };
    scope.onDragEnd         = [this] { morphDragEnd(); };
    scope.setTooltip ("拖动亮点在快照之间连续过渡（会被 DAW 录成 Morph X / Y 自动化）。双击回中"_u8);
    mMorph.addAndMakeVisible (scope);

    //== 覆盖层（线缆 → 气泡 → 输入框 → 浏览器，后加的在上面）=====================
    addAndMakeVisible (cables);
    addChildComponent (tip);

    valueEditor.setJustification (juce::Justification::centred);
    valueEditor.setFont (rk::uiFont (13.0f));
    valueEditor.setMultiLine (false);
    valueEditor.onReturnKey = [this]
    {
        if (auto* k = editingKnob)
        {
            const double v = k->getValueFromText (valueEditor.getText());
            if (k->onDragStart != nullptr) k->onDragStart();
            k->setValue (v, juce::sendNotificationSync);
            if (k->onDragEnd != nullptr) k->onDragEnd();
        }
        editingKnob = nullptr;
        valueEditor.setVisible (false);
        grabKeyboardFocus();
    };
    valueEditor.onEscapeKey = [this] { editingKnob = nullptr; valueEditor.setVisible (false); grabKeyboardFocus(); };
    valueEditor.onFocusLost = [this] { editingKnob = nullptr; valueEditor.setVisible (false); };
    addChildComponent (valueEditor);

    browser.onChoose = [this] (int slot, const juce::PluginDescription& d)
    {
        grabKeyboardFocus();
        loadDescription (slot, d);
    };
    browser.onClose = [this] { grabKeyboardFocus(); };
    browser.onScan  = [this] { showScanWindow(); };
    addChildComponent (browser);

    meterTimer.startTimerHz (30);
}

RackEditor::~RackEditor()
{
    meterTimer.stopTimer();

    if (viewDirtyAt != 0)
        rk::saveView (viewFile());
}

juce::File RackEditor::viewFile() const
{
    // 和外观偏好放在一起（见 PluginEditor::skinFile 的说明），去掉架构后缀
    if (auto* props = proc.getScanner().getProperties())
    {
        const auto settings = props->getFile();
        auto name = settings.getFileNameWithoutExtension();

        for (const auto* arch : { "-x86", "-x64" })
            if (name.endsWith (arch))
                name = name.dropLastCharacters (4);

        return settings.getSiblingFile (name + ".rackview");
    }

    return {};
}

//==============================================================================
void RackEditor::wireKnob (RackKnob& k, juce::AudioParameterFloat& p, const juce::String& label)
{
    k.label = label;
    k.setRange (0.0, 1.0, 0.001);
    k.textFromValueFunction = [] (double v) { return percentText (v); };
    k.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getDoubleValue() / 100.0; };
    k.onTip  = [this] (RackKnob& kn, bool show) { if (show) tip.show (kn.tipText(), getLocalArea (&kn, kn.getLocalBounds())); else tip.setVisible (false); };
    k.onMenu = [this] (RackKnob& kn) { showKnobMenu (kn); };
    bindSlider (k, p);
}

void RackEditor::wireRangeKnobs()
{
    // 硬件上"一个范围"就是两颗旋钮，两颗互相推着走（下限拧过上限，上限被一起推上去）
    wireKnob (kLo, *proc.pRangeLo, "下限"_u8);
    wireKnob (kHi, *proc.pRangeHi, "上限"_u8);

    for (auto* k : { &kLo, &kHi })
    {
        k->textFromValueFunction = [] (double v) { return juce::String (v, 2); };
        k->valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getDoubleValue(); };
        k->onDragStart = [this] { proc.pRangeLo->beginChangeGesture(); proc.pRangeHi->beginChangeGesture(); };
        k->onDragEnd   = [this] { proc.pRangeLo->endChangeGesture();   proc.pRangeHi->endChangeGesture(); };
    }

    kLo.setDoubleClickReturnValue (true, 0.0);
    kHi.setDoubleClickReturnValue (true, 1.0);
    kLo.getProperties().set ("rkQuick", "0,0.25,0.5");
    kHi.getProperties().set ("rkQuick", "0.5,0.75,1");

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

void RackEditor::showKnobMenu (RackKnob& k)
{
    // Rack 的参数右键菜单：标题是"名字：当前值"，下面是输入数值 / 初始化，再加几个常用值
    juce::PopupMenu m;
    m.addSectionHeader (k.tipText());

    juce::Component::SafePointer<RackEditor> safe (this);
    RackKnob* kp = &k;

    m.addItem ("输入数值…"_u8, [safe, kp] { if (auto* s = safe.getComponent()) s->editKnobValue (*kp); });

    if (k.isDoubleClickReturnEnabled())
        m.addItem ("初始化 → "_u8 + k.getTextFromValue (k.getDoubleClickReturnValue()) + "（双击）"_u8, [safe, kp]
        {
            if (safe == nullptr) return;
            if (kp->onDragStart != nullptr) kp->onDragStart();
            kp->setValue (kp->getDoubleClickReturnValue(), juce::sendNotificationSync);
            if (kp->onDragEnd != nullptr) kp->onDragEnd();
        });

    const auto quick = juce::StringArray::fromTokens (k.getProperties()["rkQuick"].toString(), ",", {});
    if (! quick.isEmpty())
    {
        m.addSeparator();
        for (const auto& q : quick)
        {
            const double v = q.getDoubleValue();
            m.addItem (k.getTextFromValue (v), true, std::abs (k.getValue() - v) < 1.0e-6, [safe, kp, v]
            {
                if (safe == nullptr) return;
                if (kp->onDragStart != nullptr) kp->onDragStart();
                kp->setValue (v, juce::sendNotificationSync);
                if (kp->onDragEnd != nullptr) kp->onDragEnd();
            });
        }
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&k));
}

void RackEditor::editKnobValue (RackKnob& k)
{
    editingKnob = &k;
    tip.setVisible (false);

    valueEditor.setBounds (getLocalArea (&k, k.getLocalBounds()).withSizeKeepingCentre (100, 24));
    valueEditor.setText (k.getTextFromValue (k.getValue()), false);
    valueEditor.setVisible (true);
    valueEditor.toFront (true);
    valueEditor.grabKeyboardFocus();
    valueEditor.selectAll();
}

//==============================================================================
void RackEditor::paint (juce::Graphics& g)
{
    g.fillAll (rk::uiBg);
}

void RackEditor::resized()
{
    auto area = getLocalBounds();
    menuBar.setBounds (area.removeFromTop (kMenuH));

    // 整台机架按窗口缩放（Rack 的 zoom）：两排 3U、85 HP，默认窗口正好 100%
    zoom = juce::jmax (0.5f, juce::jmin (((float) area.getWidth() - 4.0f) / kRowW,
                                         ((float) area.getHeight() - 8.0f) / (2.0f * kRowH)));
    const float z = zoom;

    const int rowW = juce::roundToInt (kRowW * z);
    const int rowH = juce::roundToInt (kRowH * z);
    const int x0 = area.getX() + (area.getWidth() - rowW) / 2;
    const int y0 = area.getY() + (area.getHeight() - 2 * rowH) / 2;

    backdrop.setBounds (area);
    backdrop.zoom   = z;
    backdrop.rowTop = (float) (y0 - area.getY());
    backdrop.repaint();

    chain.setZoom (z);
    chain.setBounds (x0, y0, rowW, rowH);
    kCeiling.setZoom (z);
    kCeiling.setBounds (chain.ceilingKnobArea().translated (chain.getX(), chain.getY()));

    // —— 第二排：RANDOM 13 │ MOTION 13 │ PARAMETERS 36 │ MEMORY 8 │ MORPH 15（HP）
    {
        int x = x0;
        auto place = [&] (RackModule& m, int hp)
        {
            const int w = juce::roundToInt ((float) hp * 15.0f * z);
            m.setZoom (z);
            m.clearDecor();
            m.setBounds (x, y0 + rowH, w, rowH);
            x += w;
        };

        place (mRandom, 13);
        place (mMotion, 13);
        place (mParams, juce::roundToInt ((float) (x0 + rowW - x) / (15.0f * z)) - 23);
        place (mMemory, 8);
        mMorph.setZoom (z);
        mMorph.clearDecor();
        mMorph.setBounds (x, y0 + rowH, x0 + rowW - x, rowH);
    }

    auto R = [z] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * z, y * z, w * z, h * z); };
    auto C = [z] (float cx, float cy, float size)     { return juce::Rectangle<float> (size * z, size * z).withCentre ({ cx * z, cy * z }).toNearestInt(); };
    auto P = [z] (float x, float y)                   { return juce::Point<float> (x * z, y * z); };

    for (auto* k : { &kAmount, &kLo, &kHi, &kMax, &kMotion, &kTone })
        k->setZoom (z);
    for (auto* b : { &bProbe, &bHistBack, &bHistFwd, &bRoll, &bLock, &bRestore, &bOnly, &bCheck, &bUncheck, &bListMenu })
        b->setZoom (z);
    for (auto* l : { &lMax, &lHistory, &lResult, &lSemantic, &lProfile, &lWeights })
        l->setZoom (z);
    swMotion.setZoom (z);
    swMorph.setZoom (z);
    selSemantic.setZoom (z);
    segView.setZoom (z);
    memory.setZoom (z);
    scope.setZoom (z);
    list.setZoom (z);

    //-- RANDOM ------------------------------------------------------------------
    {
        auto& m = mRandom;
        m.addLabel (R (0, 40, 195, 12), "强度"_u8);
        kAmount.setBounds (C (97.5f, 86, 76));

        m.addLabel (R (18, 120, 64, 12), "下限"_u8);
        m.addLabel (R (113, 120, 64, 12), "上限"_u8);
        kLo.setBounds (C (50, 152, 44));
        kHi.setBounds (C (145, 152, 44));

        m.addLabel (R (8, 184, 40, 12), "只动"_u8, 10.0f, juce::Justification::centredLeft);
        lMax.setBounds (R (46, 181, 94, 18).toNearestInt());
        kMax.setBounds (C (163, 190, 30));

        ledProbe = m.addLed (P (14, 220), 3.0f * z, rk::yellow);
        bProbe.setBounds (C (32, 220, 26));
        m.addLabel (R (4, 230, 56, 12), "体检"_u8);

        bHistBack.setBounds (C (98, 220, 26));
        lHistory.setBounds (R (112, 211, 46, 18).toNearestInt());
        bHistFwd.setBounds (C (172, 220, 26));
        m.addLabel (R (86, 230, 24, 12), juce::String::fromUTF8 ("\xe2\x97\x80"));
        m.addLabel (R (112, 230, 46, 12), "历史"_u8);
        m.addLabel (R (160, 230, 24, 12), juce::String::fromUTF8 ("\xe2\x96\xb6"));

        bRoll.setBounds (C (97.5f, 268, 52));
        lblRoll = m.addLabel (R (10, 290, 175, 12), "随机化 · R"_u8, 10.5f);

        lResult.setBounds (R (8, 305, 179, 41).toNearestInt());
    }

    //-- MOTION ------------------------------------------------------------------
    {
        auto& m = mMotion;
        swMotion.setBounds (C (22, 54, 34));
        m.addLabel (R (38, 42, 112, 12), "运动模式"_u8, 10.5f, juce::Justification::centredLeft);
        m.addLabel (R (38, 56, 120, 11), "拨上去 = 开"_u8, 8.5f, juce::Justification::centredLeft, false, true);
        ledMotion = m.addLed (P (176, 54), 3.4f * z, rk::cyan);

        selSemantic.setBounds (R (6, 74, 183, 72).toNearestInt());

        m.addLabel (R (15, 150, 80, 12), "运动"_u8);
        m.addLabel (R (100, 150, 80, 12), "音色"_u8);
        kMotion.setBounds (C (55, 186, 52));
        kTone.setBounds (C (140, 186, 52));

        bLock.setBounds (C (55, 224, 26));
        bRestore.setBounds (C (140, 224, 26));
        lblLock = m.addLabel (R (10, 234, 90, 12), "设为基座"_u8, 9.5f);
        m.addLabel (R (95, 234, 90, 12), "还原基座"_u8, 9.5f);

        lSemantic.setBounds (R (8, 252, 179, 36).toNearestInt());
        lProfile.setBounds (R (8, 292, 179, 54).toNearestInt());
    }

    //-- PARAMETERS --------------------------------------------------------------
    {
        auto& m = mParams;
        const float W = (float) m.getWidth() / z;

        lblCount = m.addLabel (R (12, 18, 180, 14), {}, 9.5f, juce::Justification::centredLeft, false, true);
        segView.setBounds (R (W - 206, 16, 196, 22).toNearestInt());

        teFilter.setBounds (R (12, 45, W - 302, 24).toNearestInt());

        const float bx = W - 278;
        ledOnly = m.addLed (P (bx, 57), 3.0f * z, rk::yellow);
        bOnly.setBounds (C (bx + 14, 57, 26));
        m.addLabel (R (bx + 24, 51, 48, 12), "仅匹配"_u8, 9.5f, juce::Justification::centredLeft);
        bCheck.setBounds (C (bx + 84, 57, 26));
        m.addLabel (R (bx + 94, 51, 52, 12), "勾选匹配"_u8, 9.5f, juce::Justification::centredLeft);
        bUncheck.setBounds (C (bx + 156, 57, 26));
        m.addLabel (R (bx + 166, 51, 52, 12), "取消匹配"_u8, 9.5f, juce::Justification::centredLeft);
        bListMenu.setBounds (C (bx + 228, 57, 26));
        m.addLabel (R (bx + 238, 51, 32, 12), "更多"_u8, 9.5f, juce::Justification::centredLeft);

        list.setBounds (R (8, 78, W - 16, 268).toNearestInt());
    }

    //-- MEMORY / MORPH ------------------------------------------------------------
    memory.setBounds (R (4, 38, 112, 308).toNearestInt());

    {
        auto& m = mMorph;
        const float W = (float) m.getWidth() / z;
        swMorph.setBounds (C (22, 58, 34));
        m.addLabel (R (34, 44, 26, 12), "1D", 9.5f, juce::Justification::centredLeft);
        m.addLabel (R (34, 62, 26, 12), "2D", 9.5f, juce::Justification::centredLeft);
        lWeights.setBounds (R (62, 46, W - 70, 22).toNearestInt());
        scope.setBounds (R (8, 76, W - 16, 270).toNearestInt());
    }

    cables.setZoom (z);
    cables.setBounds (area);
    browser.setBounds (getLocalBounds());

    for (auto* m : { &mRandom, &mMotion, &mParams, &mMemory, &mMorph })
        m->repaint();

    updateStatus();
}

//==============================================================================
void RackEditor::buildMenus()
{
    juce::Component::SafePointer<RackEditor> safe (this);

    auto item = [] (juce::PopupMenu& m, const juce::String& text, const juce::String& shortcut, bool enabled, bool ticked,
                    std::function<void()> action)
    {
        juce::PopupMenu::Item it (text);
        it.shortcutKeyDescription = shortcut;
        it.isEnabled = enabled;
        it.isTicked  = ticked;
        it.action    = std::move (action);
        m.addItem (std::move (it));
    };

    std::vector<RackMenuBar::Item> items;

    //-- 插件 ----------------------------------------------------------------------
    items.push_back ({ "插件"_u8, [this, item] (juce::PopupMenu& m)
    {
        const int  f   = proc.getFocusedSlot();
        const bool has = proc.getHosted() != nullptr;

        item (m, has ? "给链位 "_u8 + juce::String (f + 1) + " 换一个插件…"_u8 : "给链位 "_u8 + juce::String (f + 1) + " 装一个插件…"_u8,
              "Enter", true, false, [this, f] { openBrowser (f); });
        item (m, host.isHostedWindowOpen (f) ? "关闭插件界面"_u8 : "打开插件界面"_u8, {}, has, false,
              [this] { toggleHostedWindow(); });
        item (m, "旁通"_u8, "Ctrl+E", has, proc.isSlotBypassed (f), [this, f] { setSlotBypass (f, ! proc.isSlotBypassed (f)); });
        item (m, "卸载"_u8, {}, has, false, [this, f] { doUnload (f); });
        m.addSeparator();
        item (m, "扫描插件…"_u8, {}, true, false, [this] { showScanWindow(); });
        item (m, "插件功能标签…"_u8, {}, true, false, [this] { showTagWindow(); });
        m.addSeparator();
        item (m, "参数体检…"_u8, {}, has && ! proc.getProbe().isRunning(), false, [this] { confirmProbe(); });
    } });

    //-- 链 ------------------------------------------------------------------------
    items.push_back ({ "链"_u8, [this, item] (juce::PopupMenu& m)
    {
        const int  f   = proc.getFocusedSlot();
        const bool has = proc.getHosted() != nullptr;

        item (m, "存链…"_u8, {}, proc.getNumLoadedSlots() > 0, false, [this] { promptSaveChainPreset(); });

        juce::PopupMenu load;
        const auto files = proc.listChainPresets();
        if (files.isEmpty())
            load.addItem ("（还没有存过链预设）"_u8, false, false, nullptr);
        for (const auto& file : files)
            load.addItem (file.getFileNameWithoutExtension(), [this, file] { loadChainPresetFile (file); });
        load.addSeparator();
        load.addItem ("打开预设文件夹…"_u8, [] { ParamRouletteProcessor::getChainPresetDir().revealToUser(); });
        m.addSubMenu ("读链"_u8, load);

        m.addSeparator();
        item (m, "当前模块左移"_u8, "Ctrl+Left", has && f > 0, false, [this] { moveFocused (-1); });
        item (m, "当前模块右移"_u8, "Ctrl+Right", has && f < kMaxChain - 1, false, [this] { moveFocused (+1); });
    } });

    //-- 快照 ----------------------------------------------------------------------
    items.push_back ({ "快照"_u8, [this, item] (juce::PopupMenu& m)
    {
        juce::PopupMenu recall, store, clear;
        for (int i = 0; i < kNumSlots; ++i)
        {
            const bool used = proc.getBank().isUsed (i);
            item (recall, "快照 "_u8 + juce::String (i + 1), juce::String (i + 1), used, i == proc.getBank().getWinner(),
                  [this, i] { recallSnapshot (i); });
            item (store, "快照 "_u8 + juce::String (i + 1), "Shift+" + juce::String (i + 1), proc.getNumLoadedSlots() > 0, false,
                  [this, i] { storeSnapshot (i); });
            item (clear, "快照 "_u8 + juce::String (i + 1), {}, used, false, [this, i] { clearSnapshot (i); });
        }
        m.addSubMenu ("召回"_u8, recall);
        m.addSubMenu ("存入整条链"_u8, store);
        m.addSubMenu ("清空"_u8, clear);
        m.addSeparator();
        const int mode = proc.pMorphMode->getIndex();
        item (m, "Morph：1D"_u8, {}, true, mode == 0, [this] { setMorphModeIndex (0); scope.setMode (MorphMode::OneD); });
        item (m, "Morph：2D"_u8, {}, true, mode == 1, [this] { setMorphModeIndex (1); scope.setMode (MorphMode::TwoD); });
    } });

    //-- 视图 ----------------------------------------------------------------------
    items.push_back ({ "视图"_u8, [this, item] (juce::PopupMenu& m)
    {
        juce::PopupMenu skins;
        for (const auto s : { Skin::grid, Skin::hardware, Skin::rack })
            item (skins, skinDisplayName (s), {}, true, s == Skin::rack, [this, s] { if (s != Skin::rack) host.requestSkin (s); });
        m.addSubMenu ("外观"_u8, skins);

        m.addSectionHeader ("机架"_u8);
        auto& v = rk::view();
        item (m, "深色面板"_u8, {}, true, v.darkPanels, [this] { rk::view().darkPanels = ! rk::view().darkPanels; viewChanged (false); });

        int id = 9001;
        auto slider = [this, &m, &id] (const juce::String& label, float value, float def, std::function<void (float)> set, bool cablesOnly)
        {
            m.addCustomItem (id++, std::make_unique<RackMenuSlider> (label, value, def, [this, set, cablesOnly] (float x)
            {
                set (x);
                viewChanged (cablesOnly);
            }));
        };

        const rk::View defaults;
        slider ("线缆不透明度"_u8, v.cableOpacity, defaults.cableOpacity, [] (float x) { rk::view().cableOpacity = x; }, true);
        slider ("线缆张力"_u8, v.cableTension, defaults.cableTension, [] (float x) { rk::view().cableTension = x; }, true);
        slider ("房间亮度"_u8, v.roomBrightness, defaults.roomBrightness, [] (float x) { rk::view().roomBrightness = x; }, false);
        slider ("灯光晕"_u8, v.halo, defaults.halo, [] (float x) { rk::view().halo = x; }, false);

        m.addSeparator();
        item (m, "恢复默认"_u8, {}, true, false, [this] { rk::view() = rk::View {}; viewChanged (false); });
    } });

    //-- 帮助 ----------------------------------------------------------------------
    items.push_back ({ "帮助"_u8, [item] (juce::PopupMenu& m)
    {
        m.addSectionHeader ("快捷键（点一下界面空白处，键盘才有效）"_u8);
        const std::pair<juce::String, const char*> keys[] =
        {
            { "随机化 / 生成变体"_u8, "R" },              { "历史：上一个 / 下一个"_u8, "Ctrl+Z / Ctrl+Y" },
            { "召回快照"_u8, "1 - 4" },                   { "把整条链存进快照"_u8, "Shift+1 - 4" },
            { "打开插件浏览器"_u8, "Enter" },             { "旁通当前模块"_u8, "Ctrl+E" },
            { "当前模块左移 / 右移"_u8, "Ctrl+Left / Right" }, { "筛选参数"_u8, "Ctrl+F" },
        };
        for (const auto& [what, key] : keys)
            item (m, what, key, true, false, nullptr);

        m.addSectionHeader ("鼠标"_u8);
        for (const auto& t : { "拖模块：调整串联顺序（其余的依次让位）"_u8,
                               "旋钮：上下拖；Ctrl 微调、Shift 粗调；双击复位；右键输入数值"_u8,
                               "右键模块：打开界面 / 更换 / 旁通 / 左右移 / 卸载"_u8,
                               "视图菜单：深色面板、线缆、房间亮度"_u8 })
            item (m, t, {}, true, false, nullptr);
    } });

    menuBar.setItems (std::move (items));
    juce::ignoreUnused (safe);
}

void RackEditor::viewChanged (bool cablesOnly)
{
    viewDirtyAt = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());

    if (cablesOnly)
    {
        cables.repaint();
        return;
    }

    backdrop.repaint();
    repaint();
    for (auto* c : getChildren())
        c->repaint();
    for (auto* m : { &mRandom, &mMotion, &mParams, &mMemory, &mMorph })
        for (auto* c : m->getChildren())
            c->repaint();
}

//==============================================================================
bool RackEditor::keyPressed (const juce::KeyPress& k)
{
    if (browser.isVisible())
        return browser.keyPressed (k);

    const auto mods = k.getModifiers();
    const auto code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode());
    const int  f    = proc.getFocusedSlot();

    if (mods.isCommandDown() && ! mods.isAltDown())
    {
        if (code == 'E')
        {
            if (proc.isSlotLoaded (f))
                setSlotBypass (f, ! proc.isSlotBypassed (f));
            return true;
        }
        if (k.getKeyCode() == juce::KeyPress::leftKey)  { moveFocused (-1); return true; }
        if (k.getKeyCode() == juce::KeyPress::rightKey) { moveFocused (+1); return true; }
    }

    if (k.getKeyCode() == juce::KeyPress::returnKey && ! mods.isAnyModifierKeyDown())
    {
        openBrowser (f);
        return true;
    }

    return EditorBase::keyPressed (k);
}

//==============================================================================
void RackEditor::openBrowser (int chainSlot)
{
    tip.setVisible (false);

    auto* p = proc.getSlotPlugin (chainSlot);
    browser.setBounds (getLocalBounds());
    browser.open (chainSlot, p != nullptr ? p->getPluginDescription().createIdentifierString() : juce::String());
}

void RackEditor::doMove (int from, int to)
{
    const auto name = proc.getSlotName (from);

    if (proc.moveSlot (from, to))
    {
        host.hostedWindowsMoved (from, to);
        setNote (name + " 从链位 "_u8 + juce::String (from + 1) + " 挪到了链位 "_u8 + juce::String (to + 1));
    }
    else
    {
        setNote ("正在加载插件，等它装完再挪"_u8);
    }
}

void RackEditor::moveFocused (int delta)
{
    const int f  = proc.getFocusedSlot();
    const int to = juce::jlimit (0, kMaxChain - 1, f + delta);

    if (proc.isSlotLoaded (f) && to != f)
        doMove (f, to);
}

void RackEditor::showRackModuleMenu (int slot)
{
    const bool loaded = proc.isSlotLoaded (slot);
    juce::Component::SafePointer<RackEditor> safe (this);

    juce::PopupMenu m;
    m.addSectionHeader (loaded ? proc.getSlotName (slot) + "   ·   链位 "_u8 + juce::String (slot + 1)
                               : "链位 "_u8 + juce::String (slot + 1) + "（空）"_u8);

    if (loaded)
    {
        const bool open = host.isHostedWindowOpen (slot);
        m.addItem (open ? "关闭插件界面"_u8 : "打开插件界面"_u8, [safe, slot, open]
        {
            if (auto* s = safe.getComponent())
            {
                if (open) { s->host.closeHostedWindows (slot); s->updateStatus(); }
                else      { s->host.openHostedWindow (slot); }
            }
        });
        m.addItem ("更换插件…"_u8, [safe, slot] { if (auto* s = safe.getComponent()) s->openBrowser (slot); });

        juce::PopupMenu::Item bypass ("旁通"_u8);
        bypass.shortcutKeyDescription = "Ctrl+E";
        bypass.isTicked = proc.isSlotBypassed (slot);
        bypass.action = [safe, slot] { if (auto* s = safe.getComponent()) s->setSlotBypass (slot, ! s->proc.isSlotBypassed (slot)); };
        m.addItem (std::move (bypass));

        m.addSeparator();
        for (const int d : { -1, +1 })
        {
            juce::PopupMenu::Item mv (d < 0 ? "左移"_u8 : "右移"_u8);
            mv.shortcutKeyDescription = d < 0 ? "Ctrl+Left" : "Ctrl+Right";
            mv.isEnabled = juce::isPositiveAndBelow (slot + d, kMaxChain);
            mv.action = [safe, slot, d] { if (auto* s = safe.getComponent()) s->doMove (slot, slot + d); };
            m.addItem (std::move (mv));
        }
        m.addSeparator();
        m.addItem ("卸载"_u8, [safe, slot] { if (auto* s = safe.getComponent()) s->doUnload (slot); });
    }
    else
    {
        m.addItem ("装一个插件…"_u8, [safe, slot] { if (auto* s = safe.getComponent()) s->openBrowser (slot); });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chain).withMousePosition());
}

void RackEditor::showProgramMenu (int slot)
{
    auto* p = proc.getSlotPlugin (slot);
    if (p == nullptr || p->getNumPrograms() <= 1)
        return;

    juce::Component::SafePointer<RackEditor> safe (this);

    juce::PopupMenu m;
    m.addSectionHeader (p->getName() + "  ·  program（换了会清空运动模式的基座）"_u8);

    const int cur = p->getCurrentProgram();
    for (int i = 0; i < p->getNumPrograms(); ++i)
    {
        auto name = p->getProgramName (i).trim();
        if (name.isEmpty())
            name = "(未命名)"_u8;

        m.addItem (juce::String (i) + "   " + name, true, i == cur, [safe, i, slot]
        {
            if (auto* s = safe.getComponent())
            {
                // selectPreset 作用在聚焦的链位上；点 program 屏的时候已经把聚焦切过来了，再保险一次
                s->proc.setFocusedSlot (slot);
                s->selectPreset (i);
            }
        });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chain).withMousePosition());
}

//==============================================================================
void RackEditor::focusFilter()
{
    teFilter.grabKeyboardFocus();
    teFilter.selectAll();
}

void RackEditor::rebuildSkin()
{
    list.setSource (proc.getModel(), proc.getChecked());

    if (teFilter.getText() != proc.filterText)
        teFilter.setText (proc.filterText, false);

    onlyMatching = proc.onlyMatching;
    list.setFilter (proc.filterText, proc.onlyMatching);
    list.rebuild();
}

void RackEditor::refreshSnapshotTiles()
{
    memory.setTiles (makeSnapshotTiles(), proc.getBank().getWinner(), proc.getNumLoadedSlots() > 0);
    scope.repaint();
}

void RackEditor::syncMotionUI()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);
    const auto s = proc.getMotionSettings();

    swMotion.setPosition (proc.isMotionMode() ? 0 : 1);
    selSemantic.setSelected (juce::jlimit (0, kNumMotionSemantics - 1, (int) s.semantic));
}

void RackEditor::syncFromParameters()
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    // 用户正按着的控件不回写，否则自动化和手会互相拽
    if (! kAmount .isMouseButtonDown()) kAmount .setValue (proc.pAmount->get(),    juce::dontSendNotification);
    if (! kMotion .isMouseButtonDown()) kMotion .setValue (proc.pMotionAmt->get(), juce::dontSendNotification);
    if (! kTone   .isMouseButtonDown()) kTone   .setValue (proc.pToneAmt->get(),   juce::dontSendNotification);
    if (! kCeiling.isMouseButtonDown()) kCeiling.setValue (proc.pCeiling->get(),   juce::dontSendNotification);
    if (! kMax    .isMouseButtonDown()) kMax    .setValue (proc.pMaxParams->get(), juce::dontSendNotification);

    if (! kLo.isMouseButtonDown() && ! kHi.isMouseButtonDown())
    {
        kLo.setValue (juce::jmin (proc.pRangeLo->get(), proc.pRangeHi->get()), juce::dontSendNotification);
        kHi.setValue (juce::jmax (proc.pRangeLo->get(), proc.pRangeHi->get()), juce::dontSendNotification);
    }

    lMax.setText (kMax.getTextFromValue (kMax.getValue()));

    swMorph.setPosition (proc.pMorphMode->getIndex());
    scope.setMode (proc.getMorphMode());
    scope.setPosition (proc.pMorphX->get(), proc.pMorphY->get());

    // 权重读数：和示波器同一份 computeWeights
    {
        const auto w = proc.getBank().computeWeights (proc.getMorphMode(), proc.pMorphX->get(), proc.pMorphY->get());
        juce::String txt;
        for (int s = 0; s < kNumSlots; ++s)
            if (const float weight = w.weightOf (s); weight > 0.005f)
                txt << (txt.isEmpty() ? "" : "  ") << (s + 1) << ":" << juce::roundToInt (weight * 100.0f) << "%";
        lWeights.setText (proc.getBank().numUsed() >= 2 ? txt : juce::String ("--"));
    }
}

void RackEditor::updateChainExtras()
{
    std::array<RackChainRow::Extra, kMaxChain> ex;

    for (int i = 0; i < kMaxChain; ++i)
    {
        auto* p = proc.getSlotPlugin (i);
        if (p == nullptr)
            continue;

        auto& e = ex[(size_t) i];
        const auto d = p->getPluginDescription();
        e.brand = d.manufacturerName.trim().isNotEmpty() ? d.manufacturerName.trim() : d.pluginFormatName;

        if (p->getNumPrograms() > 1)
        {
            const int cp = p->getCurrentProgram();
            e.hasPrograms = true;
            e.program = juce::String (cp) + "  " + p->getProgramName (cp).trim();
        }

        e.editorOpen = host.isHostedWindowOpen (i);

        // 参数图：每根柱子是一个参数（多了就分桶取平均）。值直接问插件——
        // 只有聚焦的那一位的模型缓存是新的，别的位要现读
        const auto& model   = proc.getSlotModel (i);
        const auto& checked = proc.getSlotChecked (i);
        const auto& params  = p->getParameters();

        int nChecked = 0;
        for (const bool c : checked)
            if (c)
                ++nChecked;

        e.info = juce::String (model.size()) + " 参数 · 勾 "_u8 + juce::String (nChecked);

        const int n  = model.size();
        const int nb = juce::jmin (n, 48);
        e.bars.assign ((size_t) nb, 0.0f);
        e.flags.assign ((size_t) nb, 0);

        for (int b = 0; b < nb; ++b)
        {
            const int lo = b * n / nb;
            const int hi = juce::jmax (lo + 1, (b + 1) * n / nb);
            float sum = 0.0f;
            bool anyChanged = false, allUnchecked = true;

            for (int k = lo; k < hi; ++k)
            {
                const auto& m = model.getReference (k);
                sum += juce::isPositiveAndBelow (m.index, params.size()) ? params[m.index]->getValue() : m.value;
                anyChanged   = anyChanged || m.changed;
                allUnchecked = allUnchecked && ! (k < checked.size() && checked[k]);
            }

            e.bars[(size_t) b]  = sum / (float) (hi - lo);
            e.flags[(size_t) b] = (juce::uint8) ((anyChanged ? 1 : 0) | (allUnchecked ? 2 : 0));
        }
    }

    chain.setExtras (ex);
}

void RackEditor::updateStatus()
{
    const bool has      = proc.getHosted() != nullptr;
    const bool motionOn = proc.isMotionMode();

    //-- 菜单栏右边的状态文字：新消息亮 3 秒；"一个都没勾"变橙
    menuBar.setStatus (buildStatusText(),
                       (has && list.getNumChecked() == 0 && ! proc.getModel().isEmpty()) ? rk::orange
                       : isNoteFresh() ? rk::uiFg : rk::ui (0.6f));

    //-- RANDOM
    mRandom.setLabel (lblRoll, motionOn ? "生成变体 · R"_u8 : "随机化 · R"_u8, ! has);
    bRoll.setLightColour (motionOn ? rk::cyan : rk::red);
    bRoll.setTooltip (motionOn ? "按当前语义从基座生成一个变体（R）"_u8 : "掷一次（R）"_u8);
    bRoll.setEnabled (has);

    const bool probing = proc.getProbe().isRunning();
    mRandom.setLed (ledProbe, probing ? 1.0f : 0.0f);
    bProbe.setEnabled (has && ! probing);

    kAmount.setEnabled (has && ! motionOn);
    kLo    .setEnabled (has && ! motionOn);
    kHi    .setEnabled (has && ! motionOn);
    kMax   .setEnabled (has);

    {
        bool suspicious = false;
        lResult.setText (resultText (suspicious));
        lResult.setColour (suspicious ? rk::orange : rk::yellow);
    }

    bHistBack.setEnabled (has && proc.canHistoryBack());
    bHistFwd .setEnabled (has && proc.canHistoryForward());
    lHistory.setText (historyText());

    //-- 信号链
    refreshChainUI();
    updateChainExtras();
    kCeiling.setEnabled (proc.pGuardOn->get());

    //-- MOTION
    swMotion   .setEnabled (has);
    selSemantic.setEnabled (has && motionOn);
    kMotion    .setEnabled (has && motionOn);
    kTone      .setEnabled (has && motionOn);
    bLock      .setEnabled (has && motionOn);
    bRestore   .setEnabled (has && motionOn && proc.hasMotionBase());
    mMotion.setLabel (lblLock, proc.hasMotionBase() ? "重设基座"_u8 : "设为基座"_u8, ! (has && motionOn));
    mMotion.setLed (ledMotion, has && motionOn ? 1.0f : 0.0f);

    {
        const auto s = proc.getMotionSettings().semantic;
        lSemantic.setText (motionSemanticLabel (s) + " · "_u8 + semanticHint (s) + "\n"
                           + (has && motionOn ? "基座："_u8 + proc.getMotionBaseLabel()
                                              : (has ? juce::String ("运动模式关着——左上的开关拨上去"_u8) : juce::String())));
        lSemantic.setColour (has && motionOn ? rk::yellow : rk::yellow.withAlpha (0.45f));

        bool warn = false;
        const auto prof = profileText (warn);
        lProfile.setText (prof);
        lProfile.setTooltip (prof);
        lProfile.setColour (warn ? rk::orange : rk::yellow.withAlpha (0.6f));
    }

    //-- PARAMETERS
    mParams.setLabel (lblCount, has ? juce::String (proc.getModel().size()) + " 个参数 · "_u8 + proc.getHostedName()
                                    : juce::String(), true);
    mParams.setLed (ledOnly, onlyMatching ? 1.0f : 0.0f);

    const bool terms = list.hasFilterTerms();
    bCheck   .setEnabled (has && terms);
    bUncheck .setEnabled (has && terms);
    bOnly    .setEnabled (has);
    teFilter .setEnabled (has);
    segView  .setEnabled (has);
    bListMenu.setEnabled (has);
}

//==============================================================================
void RackEditor::rolled()
{
    rollFlashAt = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());
    bRoll.setLight (1.0f);
}

void RackEditor::meterTick()
{
    const auto now = juce::Time::getMillisecondCounter();

    if (! levelsFrozen)
    {
        // 峰值保持 + 指数回落（Rack 的灯也是平滑过的，不然 30 Hz 下会一闪一闪）
        for (int i = 0; i < ParamRouletteProcessor::numMeters; ++i)
            meters[(size_t) i] = juce::jmax (proc.consumeMeterPeak (i), meters[(size_t) i] * 0.86f);

        midiLight = proc.consumeMidiActivity() ? 1.0f : midiLight * 0.8f;

        std::array<float, kMaxChain> s;
        for (int i = 0; i < kMaxChain; ++i)
            s[(size_t) i] = meters[(size_t) ParamRouletteProcessor::meterSlot (i)];

        chain.setLevels (s, meters[ParamRouletteProcessor::meterInL], meters[ParamRouletteProcessor::meterInR],
                         meters[ParamRouletteProcessor::meterOutL], meters[ParamRouletteProcessor::meterOutR], midiLight);

        float sig = 0.0f;
        for (const auto& c : chain.cables())
            sig += rk::levelToBrightness (c.signal) * 7.0f + (float) c.colourIndex;
        if (std::abs (sig - plugSignature) > 0.05f)
        {
            plugSignature = sig;
            cables.repaintPlugs();
        }
    }

    if (rollFlashAt != 0)
    {
        const auto dt = now - rollFlashAt;
        const float b = dt < 320 ? 1.0f - (float) dt / 320.0f : 0.0f;
        bRoll.setLight (b);
        if (b <= 0.0f)
            rollFlashAt = 0;
    }

    if (viewDirtyAt != 0 && now - viewDirtyAt > 600)
    {
        rk::saveView (viewFile());
        viewDirtyAt = 0;
    }
}

#if PR_TEST_HOOKS
void RackEditor::setLevelsForTesting (float level)
{
    levelsFrozen = true;

    std::array<float, kMaxChain> s;
    for (int i = 0; i < kMaxChain; ++i)
        s[(size_t) i] = proc.isSlotLoaded (i) && ! proc.isSlotBypassed (i) ? level * (1.0f - 0.18f * (float) i) : 0.0f;

    chain.setLevels (s, level * 0.8f, level * 0.7f, level * 0.9f, level * 0.85f, 1.0f);
    cables.repaint();
}
#endif

} // namespace pr
