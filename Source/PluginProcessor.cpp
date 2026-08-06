#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pr
{

namespace ids
{
    static const juce::Identifier ROOT      { "ParamRoulette" };
    static const juce::Identifier HOSTED    { "Hosted" };
    static const juce::Identifier SNAPSHOTS { "SNAPSHOTS" };   // 必须和 SnapshotBank.cpp 一致
    static const juce::Identifier BASE      { "MotionBase" };  // 运动模式的基座快照

    static const juce::Identifier version   { "version" };
    static const juce::Identifier amount    { "amount" };
    static const juce::Identifier lo        { "lo" };
    static const juce::Identifier hi        { "hi" };
    static const juce::Identifier morphMode { "morphMode" };
    static const juce::Identifier morphX    { "morphX" };
    static const juce::Identifier morphY    { "morphY" };
    static const juce::Identifier filter    { "filter" };
    static const juce::Identifier onlyMatch { "onlyMatching" };
    static const juce::Identifier excluded  { "excluded" };

    static const juce::Identifier motionOn  { "motionMode" };
    static const juce::Identifier semantic  { "motionSemantic" };
    static const juce::Identifier motionAmt { "motionAmount" };
    static const juce::Identifier toneAmt   { "toneAmount" };

    static const juce::Identifier desc      { "desc" };
    static const juce::Identifier state     { "state" };
}

namespace
{
    juce::Array<int> parseIndexList (const juce::String& csv)
    {
        juce::Array<int> out;
        for (const auto& tok : juce::StringArray::fromTokens (csv, ",", {}))
            if (tok.isNotEmpty())
                out.add (tok.getIntValue());
        return out;
    }
}

//==============================================================================
juce::AudioProcessor::BusesProperties ParamRouletteProcessor::makeBuses()
{
   #if JucePlugin_IsSynth
    // 乐器版：没有音频输入，只出声。托管一个合成器时用这个。
    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #else
    return BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #endif
}

//==============================================================================
ParamRouletteProcessor::ParamRouletteProcessor()
    : juce::AudioProcessor (makeBuses())
{
    addParameter (pMorphX  = new juce::AudioParameterFloat (juce::ParameterID { "morphX",  1 },
                                                            "Morph X",  0.0f, 1.0f, 0.0f));
    addParameter (pMorphY  = new juce::AudioParameterFloat (juce::ParameterID { "morphY",  1 },
                                                            "Morph Y",  0.0f, 1.0f, 0.0f));
    addParameter (pAmount  = new juce::AudioParameterFloat (juce::ParameterID { "amount",  1 },
                                                            "Amount",   0.0f, 1.0f, 1.0f));
    addParameter (pRangeLo = new juce::AudioParameterFloat (juce::ParameterID { "rangeLo", 1 },
                                                            "Range Lo", 0.0f, 1.0f, 0.0f));
    addParameter (pRangeHi = new juce::AudioParameterFloat (juce::ParameterID { "rangeHi", 1 },
                                                            "Range Hi", 0.0f, 1.0f, 1.0f));
    addParameter (pRandomize = new juce::AudioParameterBool (juce::ParameterID { "roll", 1 },
                                                             "Randomize", false));
    addParameter (pMorphMode = new juce::AudioParameterChoice (juce::ParameterID { "morphMode", 1 },
                                                               "Morph Mode",
                                                               juce::StringArray { "1D", "2D" }, 0));

    idxMorphX    = pMorphX->getParameterIndex();
    idxMorphY    = pMorphY->getParameterIndex();
    idxRandomize = pRandomize->getParameterIndex();
    idxMorphMode = pMorphMode->getParameterIndex();

    // 只听会触发动作的那几个。amount/range 是随机化时现读的，不需要回调。
    for (auto* p : std::initializer_list<juce::AudioProcessorParameter*> {
             pMorphX, pMorphY, pRandomize, pMorphMode })
        p->addListener (this);
}

ParamRouletteProcessor::~ParamRouletteProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    probe.cancel();

    for (auto* p : std::initializer_list<juce::AudioProcessorParameter*> {
             pMorphX, pMorphY, pRandomize, pMorphMode })
        p->removeListener (this);

    // editor 正常先于 processor 析构、钩子那时已经清空；这里是兜底。
    if (closeHostedWindow != nullptr)
        closeHostedWindow();

    if (hosted != nullptr)
    {
        hosted->removeListener (this);
        hosted->releaseResources();
    }
    hosted.reset();

    scanner.savePluginList();
}

//==============================================================================
void ParamRouletteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (getCallbackLock());

    if (hosted == nullptr)
    {
        hostedScratch.setSize (0, 0);
        hostedChannels = 0;
        setLatencySamples (0);
        return;
    }

    prepareHosted (*hosted, sampleRate, samplesPerBlock);

    hostedChannels = juce::jmax (hosted->getTotalNumInputChannels(),
                                 hosted->getTotalNumOutputChannels());
    hostedScratch.setSize (juce::jmax (1, hostedChannels), juce::jmax (1, samplesPerBlock),
                           false, true, true);

    setLatencySamples (hosted->getLatencySamples());
}

void ParamRouletteProcessor::releaseResources()
{
    const juce::ScopedLock sl (getCallbackLock());

    if (hosted != nullptr)
        hosted->releaseResources();

    hostedScratch.setSize (0, 0);
}

bool ParamRouletteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainInputChannelSet() != out)
        return false;
   #endif

    return true;
}

void ParamRouletteProcessor::prepareHosted (juce::AudioPluginInstance& p, double sr, int bs)
{
    // 直接按宿主自己的通道数配置子插件。子插件拒绝这个布局时（比如它只能 5.1），
    // setPlayConfigDetails 会保留它自己的通道数，processBlock 里的 scratch 分支会兜住。
    p.setPlayConfigDetails (getTotalNumInputChannels(), getTotalNumOutputChannels(), sr, bs);
    p.prepareToPlay (sr, bs);
}

//==============================================================================
void ParamRouletteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    auto* p = hosted.get();

    if (p == nullptr)
        return;                 // 效果器版=直通；乐器版=静音。都是对的。

    const int need = hostedChannels;
    const int have = buffer.getNumChannels();

    if (need <= have)
    {
        p->processBlock (buffer, midi);
        return;
    }

    // 子插件要的声道比宿主给的多（典型：把合成器挂进效果器版）。
    // 直接把宿主的 buffer 丢过去它会越界读写，必须走暂存区。
    if (hostedScratch.getNumChannels() < need || hostedScratch.getNumSamples() < numSamples)
    {
        // 只可能是 prepareToPlay 还没跑过。宁可不出声也不越界。
        buffer.clear();
        return;
    }

    for (int ch = 0; ch < need; ++ch)
    {
        if (ch < have) hostedScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        else           hostedScratch.clear (ch, 0, numSamples);
    }

    // 这个 AudioBuffer 只是包住已有指针，不分配。
    juce::AudioBuffer<float> view (hostedScratch.getArrayOfWritePointers(), need, 0, numSamples);
    p->processBlock (view, midi);

    for (int ch = 0; ch < have; ++ch)
        buffer.copyFrom (ch, 0, hostedScratch, ch, 0, numSamples);
}

double ParamRouletteProcessor::getTailLengthSeconds() const
{
    const juce::ScopedLock sl (getCallbackLock());
    return hosted != nullptr ? hosted->getTailLengthSeconds() : 0.0;
}

juce::AudioProcessorEditor* ParamRouletteProcessor::createEditor()
{
    return new ParamRouletteEditor (*this);
}

//==============================================================================
void ParamRouletteProcessor::loadPlugin (const juce::PluginDescription& desc, LoadCallback onDone)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (loading)
    {
        if (onDone != nullptr)
            onDone ("正在加载别的插件，等一下"_u8);
        return;
    }

    loading = true;
    sendChangeMessage();        // 界面可以显示"加载中…"

    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const int    bs = getBlockSize()  > 0   ? getBlockSize()  : 512;

    juce::WeakReference<ParamRouletteProcessor> safe (this);

    scanner.getFormatManager().createPluginInstanceAsync (
        desc, sr, bs,
        [safe, desc, onDone] (std::unique_ptr<juce::AudioPluginInstance> inst,
                              const juce::String& error)
        {
            auto* self = safe.get();

            if (self == nullptr)
                return;         // 宿主没了；inst 随 unique_ptr 一起析构

            self->loading = false;

            if (inst == nullptr)
            {
                self->sendChangeMessage();
                if (onDone != nullptr)
                    onDone (error.isNotEmpty() ? error : juce::String ("加载失败"_u8));
                return;
            }

            self->installHosted (std::move (inst), desc);

            if (onDone != nullptr)
                onDone ({});
        });
}

void ParamRouletteProcessor::loadPluginById (const juce::String& identifier, LoadCallback onDone)
{
    if (auto d = scanner.getKnownPlugins().getTypeForIdentifierString (identifier))
        loadPlugin (*d, std::move (onDone));
    else if (onDone != nullptr)
        onDone ("插件表里没有这个插件，先扫描一遍"_u8);
}

void ParamRouletteProcessor::unloadPlugin()
{
    installHosted (nullptr, {});
}

void ParamRouletteProcessor::installHosted (std::unique_ptr<juce::AudioPluginInstance> next,
                                            const juce::PluginDescription& desc)
{
    JUCE_ASSERT_MESSAGE_THREAD

    probe.cancel();
    stopTimer();

    // 顺序不能变：子插件的 editor 里全是指回实例的裸指针，实例先死必崩。
    if (closeHostedWindow != nullptr)
        closeHostedWindow();

    if (hosted != nullptr)
        hosted->removeListener (this);

    // 新实例的 prepare 和暂存区分配都放在拿锁**之前**——分配是会阻塞的，
    // 拿着 callbackLock 分配等于让音频线程等 malloc。
    juce::AudioBuffer<float> nextScratch;
    int nextChannels = 0;

    if (next != nullptr)
    {
        prepareHosted (*next, getSampleRate() > 0.0 ? getSampleRate() : 44100.0,
                       getBlockSize() > 0 ? getBlockSize() : 512);

        nextChannels = juce::jmax (next->getTotalNumInputChannels(),
                                   next->getTotalNumOutputChannels());
        nextScratch.setSize (juce::jmax (1, nextChannels),
                             juce::jmax (1, getBlockSize() > 0 ? getBlockSize() : 512),
                             false, true, true);
    }

    std::unique_ptr<juce::AudioPluginInstance> old;

    suspendProcessing (true);
    {
        const juce::ScopedLock sl (getCallbackLock());
        old = std::move (hosted);
        hosted = std::move (next);
        hostedChannels = nextChannels;
        std::swap (hostedScratch, nextScratch);   // 三次 move，不分配
    }
    suspendProcessing (false);

    // 旧实例在锁外析构：有些插件的析构会转一圈消息循环，拿着 callbackLock 干这个必死锁。
    // nextScratch 现在装着旧缓冲，在函数尾自然释放，也在锁外。
    old.reset();

    hostedDesc = desc;

    rebuildModel();
    ++structureGeneration;

    if (hosted != nullptr)
    {
        hosted->addListener (this);
        setLatencySamples (hosted->getLatencySamples());
        startTimerHz (kMorphHz);
    }
    else
    {
        setLatencySamples (0);
    }

    hostedValuesDirty.store (true, std::memory_order_relaxed);
    sendChangeMessage();
}

//==============================================================================
juce::String ParamRouletteProcessor::getHostedName() const
{
    if (hosted != nullptr)
        return hosted->getName();

    return hostedDesc.name;
}

juce::String ParamRouletteProcessor::getHostedId() const
{
    if (hostedDesc.name.isEmpty() && hostedDesc.fileOrIdentifier.isEmpty())
        return {};

    return hostedDesc.createIdentifierString();
}

void ParamRouletteProcessor::rebuildModel()
{
    model.clear();
    checked.clear();
    lastResult = {};
    lastMotion = {};
    lastContoursWritten = 0;
    contourGroups = 0;

    // 换了插件，旧快照的参数数量和含义都对不上了。基座同理。
    bank.clearAll();
    bank.resetWinner();
    motionBase = Snapshot {};
    motionBaseProgram = -1;
    motionProfile = {};

    if (hosted == nullptr)
        return;

    model = buildModel (*hosted);

    checked.ensureStorageAllocated (model.size());
    for (const auto& info : model)
        checked.add (! info.defaultSkip());

    applyStoredExclusions();

    // 角色发现只看名字，不看当前值，所以建完模型就能算，不用等用户开运动模式。
    motionProfile = detectProfile (model);

    // 这个插件的包络轮廓是不是可识别格式？只在加载时问一次——
    // 状态块可能有几百 KB，不该每次随机化都去数一遍。
    {
        juce::MemoryBlock probeState;
        hosted->getStateInformation (probeState);
        contourGroups = findContourGroups (probeState).size();
    }
}

void ParamRouletteProcessor::applyStoredExclusions()
{
    const auto id = getHostedId();

    // 没记过就用默认排除规则；记过的话它在**两个方向**上都说了算——
    // 包括用户把 wet/dry 这种默认排除项手动勾上的决定。
    if (id.isEmpty() || ! scanner.hasExclusions (id))
        return;

    const auto ex = scanner.loadExclusions (id);

    for (int i = 0; i < model.size(); ++i)
        checked.set (i, ! ex.contains (model.getReference (i).index));
}

void ParamRouletteProcessor::saveExclusionsNow()
{
    const auto id = getHostedId();
    if (id.isEmpty())
        return;

    juce::Array<int> ex;
    for (int i = 0; i < model.size(); ++i)
        if (i < checked.size() && ! checked[i])
            ex.add (model.getReference (i).index);

    scanner.saveExclusions (id, ex);
}

//==============================================================================
void ParamRouletteProcessor::refreshModelValues()
{
    if (hosted != nullptr)
        refreshValues (*hosted, model);
}

void ParamRouletteProcessor::randomizeNow()
{
    if (hosted == nullptr)
        return;

    // 先记下改动前的值，随机化之后才能在界面上显示"旧 → 新"。
    // 值本身可能已经陈旧（子插件界面被拖过），先刷一遍再记。
    refreshModelValues();
    snapshotPrevValues (model);

    if (motionMode)
    {
        // 基座没锁就先锁一份当前状态。用户按下"掷"的那一刻手上的音色就是他要的
        // 出发点——逼他先点一下"锁定基座"只是多一道仪式。
        if (! motionBase.used)
        {
            motionBase = captureSnapshot (*hosted, "基座"_u8);
            motionBaseProgram = hosted->getCurrentProgram();
        }

        // **顺序不能反**：轮廓走的是 setStateInformation，它会把整个状态
        // （包括所有参数值）覆盖成状态块里的内容。所以先改曲线写回，再改参数。
        //
        // 三个关键点：
        //   - 未改动的字节从**基座的状态块**取，不从当前状态取。从当前值出发的话
        //     反复生成会累积漂移，那是随机游走不是生成变体。
        //   - fauna 只改 EG4（组 3）：实测 EG3 那条对音高没有可听差别，
        //     动它只是无谓地改掉用户的 preset。
        //   - program 变了就整个中止：用户中途换了 preset，基座已经对不上。
        const int onlyGroup = motionProfile.looksLikeFauna ? 3 : -1;

        const auto cr = randomiseContours (*hosted, motionSettings.semantic,
                                           motionSettings.motion, rng,
                                           &motionBase.childState, onlyGroup,
                                           motionBaseProgram);

        lastContoursWritten = cr.written;

        if (cr.failed)
        {
            // 写到一半失败：把基座整份写回去，别留下半套状态。
            restoreSnapshot (motionBase, *hosted);
            refreshModelValues();
            lastMotion = {};
            lastMotion.note = "曲线写入失败（preset 变了？）——已回到基座"_u8;
            sendChangeMessage();
            return;
        }

        lastMotion = applyMotion (*hosted, model, checked, motionProfile,
                                  motionBase.values, motionSettings, rng, cr.stages);

        if (cr.written > 0)
            lastMotion.note << "  ·  曲线 "_u8 << cr.written << " 条 / "_u8 << cr.stages << " 段"_u8;

        // 运动层和普通随机化共用"上次结果"那一行，别让旧数字留在界面上骗人
        lastResult = { lastMotion.touched, lastMotion.moved };
    }
    else
    {
        RandomSettings s;
        s.amount = pAmount->get();
        s.lo     = pRangeLo->get();
        s.hi     = pRangeHi->get();
        s.normalise();

        lastResult = randomize (*hosted, model, checked, s, rng);
        lastMotion = {};
    }

    refreshModelValues();
    lastChangedCount = markChangedValues (model);

    // "改了几个"用真正读回来的差异，不是随机化自己以为写了几个——
    // 插件可能把写入吸附或原地弹回，那种情况下 moved 是虚高的。
    lastResult.moved = lastChangedCount;

    ++structureGeneration;      // 「仅改动」视图要重算可见行
    sendChangeMessage();
}

//==============================================================================
void ParamRouletteProcessor::setMotionMode (bool on)
{
    if (motionMode == on)
        return;

    motionMode = on;
    sendChangeMessage();
}

//==============================================================================
int ParamRouletteProcessor::getHostedNumPrograms() const
{
    return hosted != nullptr ? hosted->getNumPrograms() : 0;
}

int ParamRouletteProcessor::getHostedProgram() const
{
    return hosted != nullptr ? hosted->getCurrentProgram() : -1;
}

juce::String ParamRouletteProcessor::getHostedProgramName (int index) const
{
    if (hosted == nullptr || ! juce::isPositiveAndBelow (index, hosted->getNumPrograms()))
        return {};

    return hosted->getProgramName (index);
}

void ParamRouletteProcessor::setHostedProgram (int index)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (hosted == nullptr || ! juce::isPositiveAndBelow (index, hosted->getNumPrograms()))
        return;

    if (index == hosted->getCurrentProgram())
        return;

    // 不取 callbackLock：换 program 在 VST2 里是 effSetProgram，可能读采样、分配内存，
    // 拿着锁做这种事会拖出一大段音频空洞。宿主们（含 REAPER）都是这么调的。
    hosted->setCurrentProgram (index);

    // 基座失效：它记的是另一个 program 的参数值和轮廓。
    // **不自动重锁**——锁定是用户的决定；下次「生成变体」会自动锁当前状态。
    motionBase = Snapshot {};
    motionBaseProgram = -1;

    refreshModelValues();
    ++structureGeneration;      // 178 个值全变了，整表重画
    sendChangeMessage();
}

void ParamRouletteProcessor::setMotionSettings (MotionSettings s)
{
    s.normalise();
    motionSettings = s;
    sendChangeMessage();
}

bool ParamRouletteProcessor::lockMotionBase()
{
    if (hosted == nullptr)
        return false;

    motionBase = captureSnapshot (*hosted, "基座"_u8);
    motionBaseProgram = hosted->getCurrentProgram();
    sendChangeMessage();
    return true;
}

bool ParamRouletteProcessor::restoreMotionBase()
{
    if (hosted == nullptr || ! motionBase.used)
        return false;

    const bool ok = restoreSnapshot (motionBase, *hosted);

    refreshModelValues();
    sendChangeMessage();
    return ok;
}

void ParamRouletteProcessor::restoreMotionBaseFromTree (const juce::ValueTree& t)
{
    // 只是把基座读回来，**不**写进插件。恢复工程时插件的状态已经从
    // HOSTED/state 还原过了，再写一遍基座会把用户存工程前的改动抹掉。
    motionBase = t.hasType (ids::BASE) ? snapshotFromValueTree (t) : Snapshot {};
    motionBaseProgram = (motionBase.used && hosted != nullptr) ? hosted->getCurrentProgram() : -1;
}

void ParamRouletteProcessor::captureSlot (int slot)
{
    if (hosted == nullptr)
        return;

    bank.capture (slot, *hosted);
    sendChangeMessage();
}

void ParamRouletteProcessor::recallSlot (int slot)
{
    if (hosted == nullptr)
        return;

    bank.recall (slot, *hosted);
    refreshModelValues();
    sendChangeMessage();
}

void ParamRouletteProcessor::clearSlot (int slot)
{
    bank.clearSlot (slot);
    sendChangeMessage();
}

void ParamRouletteProcessor::applyMorphNow()
{
    // 只存了一个 slot 时权重会全塌到它身上，等于每 33ms 强行召回一次，
    // 用户自己转旋钮都会被拽回去。两个起步才有"插值"可言。
    if (hosted == nullptr || bank.numUsed() < 2)
        return;

    bank.applyMorph (*hosted, model, checked, getMorphMode(),
                     pMorphX->get(), pMorphY->get());

    hostedValuesDirty.store (true, std::memory_order_relaxed);
}

void ParamRouletteProcessor::startInertProbe (std::function<void (int, int)> onDone)
{
    if (hosted == nullptr)
    {
        if (onDone != nullptr)
            onDone (0, 0);
        return;
    }

    juce::WeakReference<ParamRouletteProcessor> safe (this);

    probe.start (*hosted, model, [safe, onDone] (int numInert, int numEndpointOnly)
    {
        if (auto* self = safe.get())
        {
            self->refreshModelValues();
            ++self->structureGeneration;   // health / kind 列都变了，整行要重画
            self->sendChangeMessage();
        }

        if (onDone != nullptr)
            onDone (numInert, numEndpointOnly);
    });
}

//==============================================================================
void ParamRouletteProcessor::handleAsyncUpdate()
{
    if (latencyDirty.exchange (false) && hosted != nullptr)
        setLatencySamples (hosted->getLatencySamples());

    if (randomPending.exchange (false))
        randomizeNow();
}

void ParamRouletteProcessor::timerCallback()
{
    if (morphDirty.exchange (false))
        applyMorphNow();
}

void ParamRouletteProcessor::parameterValueChanged (int parameterIndex, float newValue)
{
    // 这里可能在音频线程上（DAW 自动化）。只准标脏，不准动子插件参数。
    if (parameterIndex == idxRandomize)
    {
        const bool down = newValue >= 0.5f;

        if (down && ! randomWasDown.exchange (down))
        {
            randomPending.store (true);
            triggerAsyncUpdate();
        }
        else
        {
            randomWasDown.store (down);
        }
        return;
    }

    if (parameterIndex == idxMorphX || parameterIndex == idxMorphY
        || parameterIndex == idxMorphMode)
        morphDirty.store (true);
}

void ParamRouletteProcessor::audioProcessorParameterChanged (juce::AudioProcessor*, int, float)
{
    hostedValuesDirty.store (true, std::memory_order_relaxed);
}

void ParamRouletteProcessor::audioProcessorChanged (
        juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails& details)
{
    if (details.latencyChanged)
    {
        latencyDirty.store (true);
        triggerAsyncUpdate();
    }

    // 换 program、参数表变动……任何一种都可能让显示值过期，统一标脏。
    hostedValuesDirty.store (true, std::memory_order_relaxed);
}

//==============================================================================
void ParamRouletteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // 有宿主会在后台线程存工程。拿 callbackLock 挡住换插件那条路径上的指针交换。
    // （installHosted 在锁内只做三次 move，不会转消息循环，所以不存在死锁。）
    const juce::ScopedLock sl (getCallbackLock());

    juce::ValueTree v (ids::ROOT);
    v.setProperty (ids::version,   2,                      nullptr);
    v.setProperty (ids::amount,    pAmount->get(),         nullptr);
    v.setProperty (ids::lo,        pRangeLo->get(),        nullptr);
    v.setProperty (ids::hi,        pRangeHi->get(),        nullptr);
    v.setProperty (ids::morphMode, pMorphMode->getIndex(), nullptr);
    v.setProperty (ids::morphX,    pMorphX->get(),         nullptr);
    v.setProperty (ids::morphY,    pMorphY->get(),         nullptr);
    v.setProperty (ids::filter,    filterText,             nullptr);
    v.setProperty (ids::onlyMatch, onlyMatching,           nullptr);

    v.setProperty (ids::motionOn,  motionMode,                       nullptr);
    v.setProperty (ids::semantic,  (int) motionSettings.semantic,     nullptr);
    v.setProperty (ids::motionAmt, motionSettings.motion,             nullptr);
    v.setProperty (ids::toneAmt,   motionSettings.tone,               nullptr);

    juce::StringArray ex;
    for (int i = 0; i < model.size(); ++i)
        if (i < checked.size() && ! checked[i])
            ex.add (juce::String (model.getReference (i).index));

    // 这个属性总是写，哪怕是空串——"记过、而且一个都不排除"和"没记过"必须能区分。
    v.setProperty (ids::excluded, ex.joinIntoString (","), nullptr);

    if (hosted != nullptr)
    {
        juce::ValueTree h (ids::HOSTED);

        if (auto xml = hostedDesc.createXml())
            h.setProperty (ids::desc, xml->toString(), nullptr);

        juce::MemoryBlock child;
        hosted->getStateInformation (child);
        h.setProperty (ids::state, juce::var (child), nullptr);

        v.addChild (h, -1, nullptr);
    }

    v.addChild (bank.toValueTree(), -1, nullptr);
    v.addChild (snapshotToValueTree (motionBase, ids::BASE), -1, nullptr);

    // 必须走二进制流。createXml() 会把 var(MemoryBlock) 转成 base64 *字符串*，
    // 读回来就成了 String 而不是 binary，子插件状态直接丢。
    juce::MemoryOutputStream out (destData, false);
    v.writeToStream (out);
}

void ParamRouletteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    auto v = juce::ValueTree::readFromStream (in);

    if (! v.isValid() || ! v.hasType (ids::ROOT))
        return;

    pendingState = v;

    // 加载子插件、开窗口、动参数——全都必须在消息线程上。
    // 这个函数不保证在消息线程被调用（pluginval 就故意从别的线程调）。
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyPendingState();
    }
    else
    {
        juce::WeakReference<ParamRouletteProcessor> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (auto* self = safe.get())
                self->applyPendingState();
        });
    }
}

void ParamRouletteProcessor::applyPendingState()
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto v = pendingState;
    pendingState = {};

    if (! v.isValid())
        return;

    *pAmount    = (float) v.getProperty (ids::amount,    1.0f);
    *pRangeLo   = (float) v.getProperty (ids::lo,        0.0f);
    *pRangeHi   = (float) v.getProperty (ids::hi,        1.0f);
    *pMorphX    = (float) v.getProperty (ids::morphX,    0.0f);
    *pMorphY    = (float) v.getProperty (ids::morphY,    0.0f);
    *pMorphMode = (int)   v.getProperty (ids::morphMode, 0);

    filterText   = v.getProperty (ids::filter).toString();
    onlyMatching = (bool) v.getProperty (ids::onlyMatch, false);

    motionMode = (bool) v.getProperty (ids::motionOn, false);
    motionSettings.semantic = (MotionSemantic) juce::jlimit (0, kNumMotionSemantics - 1,
                                                            (int) v.getProperty (ids::semantic,
                                                                                 (int) MotionSemantic::Rise));
    motionSettings.motion = (float) v.getProperty (ids::motionAmt, 0.65f);
    motionSettings.tone   = (float) v.getProperty (ids::toneAmt,   0.35f);
    motionSettings.normalise();

    const auto hostedTree = v.getChildWithName (ids::HOSTED);

    if (! hostedTree.isValid())
    {
        unloadPlugin();
        return;
    }

    juce::PluginDescription desc;
    if (auto xml = juce::parseXML (hostedTree.getProperty (ids::desc).toString()))
        desc.loadFromXml (*xml);

    if (desc.fileOrIdentifier.isEmpty() && desc.name.isEmpty())
        return;

    juce::MemoryBlock childState;
    if (auto* mb = hostedTree.getProperty (ids::state).getBinaryData())
        childState = *mb;

    const auto snapTree = v.getChildWithName (ids::SNAPSHOTS);
    const auto baseTree = v.getChildWithName (ids::BASE);
    const bool hasEx    = v.hasProperty (ids::excluded);
    const auto exCsv    = v.getProperty (ids::excluded).toString();

    juce::WeakReference<ParamRouletteProcessor> safe (this);

    loadPlugin (desc, [safe, childState, snapTree, baseTree, hasEx, exCsv] (const juce::String& error)
    {
        auto* self = safe.get();

        if (self == nullptr || error.isNotEmpty())
            return;

        auto* h = self->getHosted();
        if (h == nullptr)
            return;

        if (childState.getSize() > 0)
            h->setStateInformation (childState.getData(), (int) childState.getSize());

        // 顺序有讲究：installHosted 已经把 bank 清空、基座清掉、勾选重置成默认了，
        // 所以这几步必须在它之后。
        if (hasEx)
        {
            const auto ex = parseIndexList (exCsv);
            auto& m = self->getModel();
            auto& c = self->getChecked();

            for (int i = 0; i < m.size(); ++i)
                c.set (i, ! ex.contains (m.getReference (i).index));
        }

        self->getBank().restoreFromValueTree (snapTree);
        self->restoreMotionBaseFromTree (baseTree);

        self->refreshModelValues();
        ++self->structureGeneration;
        self->sendChangeMessage();
    });
}

} // namespace pr

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pr::ParamRouletteProcessor();
}
