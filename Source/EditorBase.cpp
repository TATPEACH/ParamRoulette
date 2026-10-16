#include "EditorBase.h"
#include "GridLookAndFeel.h"

namespace pr
{

juce::String skinDisplayName (Skin s)
{
    switch (s)
    {
        case Skin::grid:     return "Grid"_u8;
        case Skin::hardware: return "硬件"_u8;
        case Skin::rack:     return "Rack"_u8;
    }
    return {};
}

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

//==============================================================================
EditorBase::EditorBase (ParamRouletteProcessor& p, EditorHost& h, juce::LookAndFeel_V4& lf)
    : proc (p), host (h), lnf (lf)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
}

EditorBase::~EditorBase()
{
    proc.removeChangeListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

void EditorBase::start()
{
    if (started)
        return;

    started = true;
    proc.addChangeListener (this);
    rebuildAll();
    startTimerHz (20);
}

//==============================================================================
void EditorBase::mouseDown (const juce::MouseEvent&)
{
    // 点空白处把键盘焦点拿回来，快捷键才有效（筛选框获得过焦点之后尤其需要）。
    grabKeyboardFocus();
}

bool EditorBase::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode());

    if (mods.isCommandDown() && ! mods.isAltDown())
    {
        if (code == 'Z' && ! mods.isShiftDown())                 { doHistory (true);  return true; }
        if (code == 'Y' || (code == 'Z' && mods.isShiftDown()))  { doHistory (false); return true; }
        if (code == 'F')
        {
            focusFilter();
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
                if (proc.getNumLoadedSlots() > 0)
                    storeSnapshot (i);
            }
            else if (proc.getBank().isUsed (i))
            {
                recallSnapshot (i);
            }
            return true;
        }
    }

    return false;
}

//==============================================================================
void EditorBase::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (proc.getStructureGeneration() != lastGeneration)
    {
        rebuildAll();
        return;
    }

    paramList().refresh();
    refreshSnapshotTiles();
    syncMotionUI();
    updateStatus();
}

void EditorBase::timerCallback()
{
    if (proc.getStructureGeneration() != lastGeneration)
    {
        rebuildAll();
    }
    else if (proc.consumeHostedValuesDirty())
    {
        // processor 的定时器只推 morph，不刷显示值——这一步只能在这边做。
        proc.refreshModelValues();
        paramList().refresh();
    }

    // morph 是 processor 的定时器在推，它不发 change 消息——快照上"当前是哪一个"的高亮
    // 只能在这里跟（以前漏了：拖 morph 的时候快照方块上的高亮一直停在旧的那个上）。
    // 只在真的变了时刷，不然 pad 会被 20Hz 地整块重画。
    if (const int w = proc.getBank().getWinner(); w != lastWinner)
    {
        lastWinner = w;
        refreshSnapshotTiles();
    }

    syncFromParameters();
    updateStatus();
}

void EditorBase::rebuildAll()
{
    lastGeneration = proc.getStructureGeneration();
    rebuildSkin();
    refreshSnapshotTiles();
    syncMotionUI();
    updateStatus();
}

//==============================================================================
void EditorBase::fillPresetBox (juce::ComboBox& box)
{
    const juce::ScopedValueSetter<bool> guard (syncing, true);

    box.clear (juce::dontSendNotification);

    const int n = proc.getHostedNumPrograms();

    // 只有一个 program 的插件（大多数 VST3）没得选，别在界面上摆个死控件
    if (n <= 1)
    {
        box.setEnabled (false);
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        auto name = proc.getHostedProgramName (i).trim();
        if (name.isEmpty())
            name = "(未命名)"_u8;

        box.addItem (juce::String (i) + "  " + name, i + 1);
    }

    box.setEnabled (true);
    box.setSelectedItemIndex (juce::jmax (0, proc.getHostedProgram()), juce::dontSendNotification);
}

void EditorBase::syncPresetBox (juce::ComboBox& box)
{
    // 子插件自己的界面上也能换 preset（fauna 就有），那边换了这边要跟上
    if (box.isEnabled())
    {
        const int p = proc.getHostedProgram();
        if (p >= 0 && box.getSelectedItemIndex() != p)
            box.setSelectedItemIndex (p, juce::dontSendNotification);
    }
}

std::array<SnapshotTiles::Tile, 4> EditorBase::makeSnapshotTiles() const
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

    return t;
}

void EditorBase::refreshChainUI()
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

    chainView().setState (s, proc.getFocusedSlot(), gd);
}

//==============================================================================
juce::String EditorBase::buildStatusText()
{
    if (proc.isLoading())
        return "正在加载插件…"_u8;

    juce::String s;
    auto& list = paramList();

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

juce::String EditorBase::historyText() const
{
    const int n = proc.getHistoryCount();
    return n > 1 ? (juce::String (proc.getHistoryPos() + 1) + " / " + juce::String (n))
                 : juce::String ("—"_u8);
}

void EditorBase::setNote (const juce::String& n)
{
    note = n;
    noteTime = juce::Time::getMillisecondCounter();
    updateStatus();
}

bool EditorBase::isNoteFresh() const
{
    return note.isNotEmpty() && juce::Time::getMillisecondCounter() - noteTime < 3000;
}

juce::String EditorBase::resultText (bool& suspicious) const
{
    const bool has      = proc.getHosted() != nullptr;
    const bool motionOn = proc.isMotionMode();
    const auto r        = proc.getLastResult();

    suspicious = ! motionOn && r.looksSuspicious();

    juce::String res;

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

    return res;
}

juce::String EditorBase::profileText (bool& warn) const
{
    // 运动层的角色识别情况。认不出包络就直说——不然用户会以为语义没生效是 bug。
    const bool has      = proc.getHosted() != nullptr;
    const bool motionOn = proc.isMotionMode();

    warn = motionOn && has && ! proc.getMotionProfile().hasMotion();

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

    return prof;
}

//==============================================================================
void EditorBase::doRoll()
{
    if (proc.getHosted() == nullptr)
    {
        setNote ("当前链位没有插件——先在信号链上点一个空位装一个"_u8);
        return;
    }

    note.clear();
    proc.randomizeNow();    // 已经在消息线程上，直接来
    rolled();
}

void EditorBase::showSkinMenu (juce::Component& anchor, Skin current)
{
    juce::PopupMenu m;
    m.addSectionHeader ("外观（功能完全一样，随时换）"_u8);

    const std::pair<Skin, juce::String> all[] =
    {
        { Skin::grid,     "Grid —— 深色画布 + 圆角模块"_u8 },
        { Skin::hardware, "硬件 —— 藏青面板、旋钮、VU 表"_u8 },
        { Skin::rack,     "Rack —— VCV 式机架，能拖模块调顺序"_u8 },
    };

    for (const auto& [s, text] : all)
        m.addItem (1 + (int) s, text, true, s == current);

    juce::Component::SafePointer<EditorBase> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor), [safe, current] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || r <= 0 || (Skin) (r - 1) == current)
            return;

        self->host.requestSkin ((Skin) (r - 1));
    });
}

void EditorBase::doHistory (bool back)
{
    if (back)
        setNote (proc.historyBack() ? "← 回到上一个状态"_u8 : "已经是最早的记录了"_u8);
    else
        setNote (proc.historyForward() ? "→ 前进到下一个状态"_u8 : "已经是最新的了"_u8);
}

void EditorBase::showPluginMenu (int chainSlot, juce::Component* anchor)
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

    juce::Component::SafePointer<EditorBase> safe (this);

    auto opts = juce::PopupMenu::Options().withTargetComponent (anchor != nullptr ? anchor : &chainView());
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

void EditorBase::showModuleMenu (int chainSlot)
{
    const bool loaded = proc.isSlotLoaded (chainSlot);
    const bool open   = host.isHostedWindowOpen (chainSlot);

    juce::PopupMenu m;
    m.addSectionHeader ("链位 "_u8 + juce::String (chainSlot + 1)
                        + (loaded ? " · "_u8 + proc.getSlotName (chainSlot) : juce::String()));
    m.addItem (1, open ? "关闭插件界面"_u8 : "打开插件界面"_u8, loaded);
    m.addItem (2, loaded ? "更换插件…"_u8 : "装一个插件…"_u8);
    m.addItem (3, "旁通"_u8, loaded, proc.isSlotBypassed (chainSlot));
    m.addSeparator();
    m.addItem (4, "卸载"_u8, loaded);

    juce::Component::SafePointer<EditorBase> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chainView()).withMousePosition(),
                     [safe, chainSlot, open] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr)
            return;

        switch (r)
        {
            case 1:
                if (open) { self->host.closeHostedWindows (chainSlot); self->updateStatus(); }
                else      { self->host.openHostedWindow (chainSlot); }
                break;
            case 2: self->choosePlugin (chainSlot, nullptr); break;
            case 3: self->proc.setSlotBypassed (chainSlot, ! self->proc.isSlotBypassed (chainSlot)); break;
            case 4: self->doUnload (chainSlot); break;
            default: break;
        }
    });
}

void EditorBase::loadDescription (int chainSlot, const juce::PluginDescription& desc)
{
    // 旧界面必须先死，实例才能换。只关这一位——别的链位的窗口该留着。
    host.closeHostedWindows (chainSlot);
    setNote ("正在加载 "_u8 + desc.name + " …"_u8);

    juce::Component::SafePointer<EditorBase> safe (this);

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

void EditorBase::doUnload (int chainSlot)
{
    host.closeHostedWindows (chainSlot);
    proc.unloadSlot (chainSlot);
    setNote ("链位 "_u8 + juce::String (chainSlot + 1) + " 已清空"_u8);
}

void EditorBase::toggleHostedWindow()
{
    // 开关的是**当前聚焦的**链位的界面。想同时看两位就切过去再点一次，
    // 两个窗口会一起留着。
    const int slot = proc.getFocusedSlot();

    if (! juce::isPositiveAndBelow (slot, kMaxChain))
        return;

    if (host.isHostedWindowOpen (slot))
    {
        host.closeHostedWindows (slot);
        updateStatus();
        return;
    }

    host.openHostedWindow (slot);
}

//==============================================================================
void EditorBase::applyFilter (const juce::String& text, bool onlyMatching)
{
    proc.filterText   = text;
    proc.onlyMatching = onlyMatching;

    paramList().setFilter (proc.filterText, proc.onlyMatching);
    updateStatus();
}

void EditorBase::checkMatching (bool value)
{
    paramList().applyToMatching (value);
    setNote (value ? "已勾选全部匹配项"_u8 : "已取消全部匹配项"_u8);
}

void EditorBase::showListMenu (juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addSectionHeader ("只作用于当前可见的 "_u8 + juce::String (paramList().getNumVisible()) + " 行"_u8);
    m.addItem (1, "全选"_u8);
    m.addItem (2, "全不选"_u8);
    m.addItem (3, "反选"_u8);
    m.addSeparator();
    m.addItem (4, "取消勾选所有「写不进」的参数"_u8);

    juce::Component::SafePointer<EditorBase> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor), [safe] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr)
            return;

        auto& list = self->paramList();

        switch (r)
        {
            case 1: list.checkAll (true);  break;
            case 2: list.checkAll (false); break;
            case 3: list.invertAll();      break;
            case 4: list.uncheckInert();   self->setNote ("已把写不进去的参数移出随机范围"_u8); break;
            default: break;
        }
    });
}

//==============================================================================
void EditorBase::confirmProbe()
{
    if (proc.getHosted() == nullptr || proc.getProbe().isRunning())
        return;

    juce::Component::SafePointer<EditorBase> safe (this);

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

void EditorBase::startProbe()
{
    if (proc.getHosted() == nullptr || proc.getProbe().isRunning())
        return;

    setNote ("体检中…别动"_u8);

    juce::Component::SafePointer<EditorBase> safe (this);

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

void EditorBase::offerUncheckInert (int numInert)
{
    juce::Component::SafePointer<EditorBase> safe (this);

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
                self->paramList().uncheckInert();
                self->setNote ("已把写不进去的参数移出随机范围"_u8);
            }
        }));
}

//==============================================================================
void EditorBase::promptSaveChainPreset()
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

    juce::Component::SafePointer<EditorBase> safe (this);

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

void EditorBase::showChainPresetMenu (juce::Component& anchor)
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

    juce::Component::SafePointer<EditorBase> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
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

        self->loadChainPresetFile (files[choice - 1]);
    });
}

void EditorBase::loadChainPresetFile (const juce::File& file)
{
    setNote ("正在读 "_u8 + file.getFileNameWithoutExtension() + " …"_u8);

    juce::Component::SafePointer<EditorBase> safe (this);

    proc.loadChainPreset (file, [safe, file] (const juce::String& error)
    {
        if (auto* s = safe.getComponent())
        {
            s->rebuildAll();
            s->setNote (error.isEmpty() ? ("已读入 "_u8 + file.getFileNameWithoutExtension()) : error);
        }
    });
}

//==============================================================================
void EditorBase::selectPreset (int idx)
{
    if (syncing || idx < 0)
        return;

    proc.setHostedProgram (idx);
    setNote ("已切到 preset "_u8 + juce::String (idx) + " " + proc.getHostedProgramName (idx)
             + "（基座已清空）"_u8);
}

void EditorBase::setSlotBypass (int i, bool b)
{
    proc.setSlotBypassed (i, b);
    setNote (b ? "链位 "_u8 + juce::String (i + 1) + " 已旁通"_u8
               : "链位 "_u8 + juce::String (i + 1) + " 恢复工作"_u8);
}

void EditorBase::setGuardOn (bool on)
{
    writeParam (*proc.pGuardOn, on ? 1.0f : 0.0f);
    setNote (on ? "输出保护已打开"_u8 : "输出保护已关闭（NaN 仍会被清零）"_u8);
}

void EditorBase::setMotionOn (bool on)
{
    if (! syncing)
        proc.setMotionMode (on);
}

void EditorBase::setSemantic (int index)
{
    if (syncing)
        return;

    // 强度那两个旋钮**不经过这里**——它们直接写各自的可自动化参数。
    // 这里只管语义：它不可自动化，只能走 processor 的成员。
    auto s = proc.getMotionSettings();
    s.semantic = (MotionSemantic) juce::jlimit (0, kNumMotionSemantics - 1, index);
    proc.setMotionSettings (s);
}

void EditorBase::setMaxParams (int v)
{
    if (syncing)
        return;

    auto& prm = *proc.pMaxParams;
    prm.beginChangeGesture();
    prm.setValueNotifyingHost (prm.convertTo0to1 ((float) v));
    prm.endChangeGesture();
}

void EditorBase::setMorphModeIndex (int idx)
{
    if (syncing)
        return;

    proc.pMorphMode->beginChangeGesture();
    proc.pMorphMode->setValueNotifyingHost (proc.pMorphMode->convertTo0to1 ((float) idx));
    proc.pMorphMode->endChangeGesture();
}

void EditorBase::lockBase()
{
    setNote (proc.lockMotionBase() ? ("基座 → "_u8 + proc.getMotionBaseLabel())
                                   : juce::String ("设定失败：没有加载插件"_u8));
}

void EditorBase::restoreBase()
{
    setNote (proc.restoreMotionBase() ? ("已还原到基座 "_u8 + proc.getMotionBaseLabel())
                                      : juce::String ("还原失败：基座未设定"_u8));
}

void EditorBase::storeSnapshot (int i)
{
    proc.captureSlot (i);
    setNote ("整条链已存入快照 "_u8 + juce::String (i + 1));
}

void EditorBase::recallSnapshot (int i)
{
    proc.recallSlot (i);
    setNote ("已召回快照 "_u8 + juce::String (i + 1));
}

void EditorBase::clearSnapshot (int i)
{
    proc.clearSlot (i);
    setNote ("已清空快照 "_u8 + juce::String (i + 1));
}

void EditorBase::morphDragStart()
{
    proc.pMorphX->beginChangeGesture();
    proc.pMorphY->beginChangeGesture();
}

void EditorBase::morphMoved (float x, float y)
{
    proc.pMorphX->setValueNotifyingHost (x);
    proc.pMorphY->setValueNotifyingHost (y);
}

void EditorBase::morphDragEnd()
{
    proc.pMorphX->endChangeGesture();
    proc.pMorphY->endChangeGesture();
}

void EditorBase::bindSlider (juce::Slider& s, juce::AudioParameterFloat& p)
{
    s.setValue (p.get(), juce::dontSendNotification);

    s.onDragStart = [&p] { p.beginChangeGesture(); };
    s.onDragEnd   = [&p] { p.endChangeGesture(); };
    s.onValueChange = [this, &s, &p]
    {
        if (! syncing)
            p.setValueNotifyingHost (p.convertTo0to1 ((float) s.getValue()));
    };
}

void EditorBase::writeParam (juce::RangedAudioParameter& prm, float plainValue)
{
    prm.beginChangeGesture();
    prm.setValueNotifyingHost (prm.convertTo0to1 (plainValue));
    prm.endChangeGesture();
}

} // namespace pr
