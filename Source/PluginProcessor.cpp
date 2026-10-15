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
    static const juce::Identifier maxParams { "maxParams" };   // v3 起；v2 的工程里没有，缺省 0

    static const juce::Identifier desc      { "desc" };
    static const juce::Identifier state     { "state" };

    // —— v3 起新增。
    // `slot` 是**版本判据**：v2 只有一个 HOSTED 且没有这个属性，
    // 所以"没有 slot 属性的 HOSTED 节点"就是老工程，装进链位 0。
    static const juce::Identifier slot      { "slot" };
    static const juce::Identifier bypass    { "bypass" };
    static const juce::Identifier focused   { "focused" };
    static const juce::Identifier ceiling   { "ceiling" };
    static const juce::Identifier guardOn   { "guardOn" };
    static const juce::Identifier spans     { "spans" };      // "172=0.03,88=0.02"
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

    // 后加的三个。追加在末尾是安全的：VST3 宿主按参数 ID 匹配自动化，不按下标。
    addParameter (pMotionAmt = new juce::AudioParameterFloat (juce::ParameterID { "motionAmt", 1 },
                                                             "Motion", 0.0f, 1.0f, 0.65f));
    addParameter (pToneAmt   = new juce::AudioParameterFloat (juce::ParameterID { "toneAmt", 1 },
                                                             "Tone",   0.0f, 1.0f, 0.35f));
    // 上限 64：再多就没有"只动几个"的意义了，而且给滑条一个有限量程才好拖。
    addParameter (pMaxParams = new juce::AudioParameterInt (juce::ParameterID { "maxParams", 1 },
                                                           "Max Params", 0, 64, 0));

    // 链尾安全网。**默认开着**：随机化把反馈量拉满炸出 +40 dB 或 NaN 是完全
    // 现实的结果，而爆音伤耳朵和监听设备。要关是用户的决定，不该是默认。
    addParameter (pCeiling = new juce::AudioParameterFloat (juce::ParameterID { "ceiling", 1 },
                                                           "Ceiling", -24.0f, 0.0f, -1.0f));
    addParameter (pGuardOn = new juce::AudioParameterBool (juce::ParameterID { "guardOn", 1 },
                                                          "Output Guard", true));

    outputGuard.setCeilingDb (pCeiling->get());
    outputGuard.setEnabled (pGuardOn->get());

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
    // -1 = 全关。
    if (closeHostedWindow != nullptr)
        closeHostedWindow (-1);

    for (auto& s : chain)
    {
        if (s.plugin != nullptr)
        {
            s.plugin->removeListener (this);
            s.plugin->releaseResources();
        }
        s.plugin.reset();
    }

    scanner.savePluginList();
}

//==============================================================================
void ParamRouletteProcessor::prepareSlot (HostedSlot& s, double sampleRate, int samplesPerBlock)
{
    if (s.plugin == nullptr)
    {
        s.scratch.setSize (0, 0);
        s.channels = 0;
        return;
    }

    prepareHosted (*s.plugin, sampleRate, samplesPerBlock);

    s.channels = juce::jmax (s.plugin->getTotalNumInputChannels(),
                             s.plugin->getTotalNumOutputChannels());
    s.scratch.setSize (juce::jmax (1, s.channels), juce::jmax (1, samplesPerBlock),
                       false, true, true);
}

void ParamRouletteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock sl (getCallbackLock());

    for (auto& s : chain)
        prepareSlot (s, sampleRate, samplesPerBlock);

    outputGuard.prepare (sampleRate, getTotalNumOutputChannels());
    outputGuard.setCeilingDb (pCeiling->get());
    outputGuard.setEnabled (pGuardOn->get());

    updateChainLatency();
}

void ParamRouletteProcessor::releaseResources()
{
    const juce::ScopedLock sl (getCallbackLock());

    for (auto& s : chain)
    {
        if (s.plugin != nullptr)
            s.plugin->releaseResources();

        s.scratch.setSize (0, 0);
    }

    outputGuard.reset();
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

    // 串联的机制在 ChainRunner 里（那边有无头测试：串联次序、旁通、空位、
    // 延迟求和、声道不足走暂存区、暂存区不够时跳过而不越界）。
    //
    // MIDI 用同一个 buffer 串下去，所以前一位的 MIDI 输出会进到后一位——
    // 这正是真实效果器链的行为。
    std::array<ChainLink, kMaxChain> links;
    for (size_t i = 0; i < links.size(); ++i)
        links[i] = chain[i].link();

    const auto r = runChain (links.data(), (int) links.size(), buffer, midi);

    if (r.skipped > 0)
    {
        // 只可能是 prepareToPlay 还没跑过。宁可静音也不放出半条链的结果。
        buffer.clear();
    }

    // 链尾安全网。**总是跑**——它内部会在 guard 关掉时只做 NaN 清洗。
    // 随机化一个全通的反馈量炸出 +40 dB 或 NaN 是完全现实的结果，
    // 而爆音伤耳朵、NaN 会顺着总线永久污染下游插件。
    outputGuard.process (buffer);

    // 输出电平（给 VU 表）。量的是安全网**之后**的信号——就是用户真正听到的那一份。
    {
        float rms = 0.0f, pk = 0.0f;
        for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
        {
            rms = juce::jmax (rms, buffer.getRMSLevel (ch, 0, numSamples));
            pk  = juce::jmax (pk,  buffer.getMagnitude (ch, 0, numSamples));
        }

        meterRms.store (rms, std::memory_order_relaxed);
        if (pk > meterPeak.load (std::memory_order_relaxed))
            meterPeak.store (pk, std::memory_order_relaxed);
        meterBlocks.fetch_add (1, std::memory_order_relaxed);
    }
}

void ParamRouletteProcessor::getOutputLevel (float& rms, float& peak) noexcept
{
    const auto blocks = meterBlocks.load (std::memory_order_relaxed);

    if (blocks != meterLastSeen)
    {
        meterLastSeen = blocks;
        meterStale = 0;
    }
    else if (++meterStale > 12)     // 界面 60Hz，≈200ms 没有新块（8192 采样的缓冲也够）
    {
        rms = peak = 0.0f;
        return;
    }

    rms  = meterRms.load (std::memory_order_relaxed);
    peak = meterPeak.exchange (0.0f, std::memory_order_relaxed);
}

double ParamRouletteProcessor::getTailLengthSeconds() const
{
    const juce::ScopedLock sl (getCallbackLock());

    // const 方法里要拿非 const 的 link()，所以自己拼一份只读的。
    std::array<ChainLink, kMaxChain> links;
    for (size_t i = 0; i < links.size(); ++i)
    {
        links[i].plugin   = chain[i].plugin.get();
        links[i].bypassed = chain[i].bypassed;
    }

    return chainTailSeconds (links.data(), (int) links.size());
}

void ParamRouletteProcessor::updateChainLatency()
{
    std::array<ChainLink, kMaxChain> links;
    for (size_t i = 0; i < links.size(); ++i)
    {
        links[i].plugin   = chain[i].plugin.get();
        links[i].bypassed = chain[i].bypassed;
    }

    setLatencySamples (chainLatencySamples (links.data(), (int) links.size()));
}

juce::AudioProcessorEditor* ParamRouletteProcessor::createEditor()
{
    return new ParamRouletteEditor (*this);
}

//==============================================================================
void ParamRouletteProcessor::loadPluginIntoSlot (int chainSlot,
                                                 const juce::PluginDescription& desc,
                                                 LoadCallback onDone)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
    {
        if (onDone != nullptr)
            onDone ("链位号超出范围"_u8);
        return;
    }

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
        [safe, desc, chainSlot, onDone] (std::unique_ptr<juce::AudioPluginInstance> inst,
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

            self->installHosted (chainSlot, std::move (inst), desc);

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

void ParamRouletteProcessor::unloadSlot (int chainSlot)
{
    installHosted (chainSlot, nullptr, {});
}

void ParamRouletteProcessor::installHosted (int chainSlot,
                                            std::unique_ptr<juce::AudioPluginInstance> next,
                                            const juce::PluginDescription& desc)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return;

    auto& slot = chain[(size_t) chainSlot];

    probe.cancel();
    stopTimer();

    // 顺序不能变：子插件的 editor 里全是指回实例的裸指针，实例先死必崩。
    // 只关这一位的窗口——同时开着别的链位的界面是正常用法（调链要对比）。
    if (closeHostedWindow != nullptr)
        closeHostedWindow (chainSlot);

    if (slot.plugin != nullptr)
        slot.plugin->removeListener (this);

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
        old = std::move (slot.plugin);
        slot.plugin = std::move (next);
        slot.channels = nextChannels;
        std::swap (slot.scratch, nextScratch);   // 三次 move，不分配
    }
    suspendProcessing (false);

    // 旧实例在锁外析构：有些插件的析构会转一圈消息循环，拿着 callbackLock 干这个必死锁。
    // nextScratch 现在装着旧缓冲，在函数尾自然释放，也在锁外。
    old.reset();

    slot.desc = desc;

    rebuildModel (chainSlot);
    ++structureGeneration;

    if (slot.plugin != nullptr)
        slot.plugin->addListener (this);

    updateChainLatency();

    // 链上还有别的插件在的话 morph 定时器要继续跑。
    if (getNumLoadedSlots() > 0)
        startTimerHz (kMorphHz);

    hostedValuesDirty.store (true, std::memory_order_relaxed);
    sendChangeMessage();
}

//==============================================================================
void ParamRouletteProcessor::setFocusedSlot (int chainSlot)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain) || chainSlot == focused)
        return;

    // 体检是对**某一个**插件跑的，换了聚焦目标就必须先停，否则它会继续
    // 往上一个插件里写探测值，而界面已经在显示另一个插件的模型了。
    probe.cancel();

    focused = chainSlot;

    refreshModelValues();
    ++structureGeneration;      // 整张参数表换了一个插件，全表重建
    sendChangeMessage();
}

bool ParamRouletteProcessor::isSlotLoaded (int chainSlot) const
{
    return juce::isPositiveAndBelow (chainSlot, kMaxChain)
        && chain[(size_t) chainSlot].plugin != nullptr;
}

juce::AudioPluginInstance* ParamRouletteProcessor::getSlotPlugin (int chainSlot) const
{
    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return nullptr;

    return chain[(size_t) chainSlot].plugin.get();
}

juce::String ParamRouletteProcessor::getSlotName (int chainSlot) const
{
    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return {};

    const auto& s = chain[(size_t) chainSlot];

    if (s.plugin != nullptr)
        return s.plugin->getName();

    return s.desc.name;
}

bool ParamRouletteProcessor::isSlotBypassed (int chainSlot) const
{
    return juce::isPositiveAndBelow (chainSlot, kMaxChain)
        && chain[(size_t) chainSlot].bypassed;
}

void ParamRouletteProcessor::setSlotBypassed (int chainSlot, bool shouldBypass)
{
    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return;

    auto& s = chain[(size_t) chainSlot];

    if (s.bypassed == shouldBypass)
        return;

    // bool 的写入是原子的，音频线程读到旧值最多是多跑/少跑一块，不会崩。
    // 为此拿 callbackLock 得不偿失。
    s.bypassed = shouldBypass;

    updateChainLatency();       // 旁通的那一位不再贡献延迟
    sendChangeMessage();
}

int ParamRouletteProcessor::getNumLoadedSlots() const
{
    int n = 0;
    for (const auto& s : chain)
        if (s.plugin != nullptr)
            ++n;
    return n;
}

//==============================================================================
juce::String ParamRouletteProcessor::getHostedName() const
{
    return getSlotName (focused);
}

juce::String ParamRouletteProcessor::getHostedId() const
{
    const auto& d = cur().desc;

    if (d.name.isEmpty() && d.fileOrIdentifier.isEmpty())
        return {};

    return d.createIdentifierString();
}

void ParamRouletteProcessor::rebuildModel (int chainSlot)
{
    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return;

    auto& slot = chain[(size_t) chainSlot];

    // 换了插件，这一位的模型 / 勾选 / 基座 / 历史全部作废（但**不动旁通**：
    // 那是用户对这个位置的设置）。
    slot.resetForNewPlugin();

    if (chainSlot == focused)
    {
        lastResult = {};
        lastMotion = {};
    }

    // 快照存的是整条链的状态，某一位换了插件就对不上了。
    bank.clearAll();
    bank.resetWinner();

    if (slot.plugin == nullptr)
        return;

    slot.model = buildModel (*slot.plugin);

    slot.checked.ensureStorageAllocated (slot.model.size());
    for (const auto& info : slot.model)
        slot.checked.add (! info.defaultSkip());

    applyStoredExclusions (chainSlot);
    applyStoredSpans (chainSlot);

    // 角色发现只看名字，不看当前值，所以建完模型就能算，不用等用户开运动模式。
    slot.profile = detectProfile (slot.model);

    // 这个插件的包络轮廓是不是可识别格式？只在加载时问一次——
    // 状态块可能有几百 KB，不该每次随机化都去数一遍。
    {
        juce::MemoryBlock probeState;
        slot.plugin->getStateInformation (probeState);
        slot.contourGroups = findContourGroups (probeState).size();
    }
}

void ParamRouletteProcessor::applyStoredExclusions (int chainSlot)
{
    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return;

    auto& slot = chain[(size_t) chainSlot];
    const auto& d = slot.desc;

    if (d.name.isEmpty() && d.fileOrIdentifier.isEmpty())
        return;

    const auto id = d.createIdentifierString();

    // 没记过就用默认排除规则；记过的话它在**两个方向**上都说了算——
    // 包括用户把 wet/dry 这种默认排除项手动勾上的决定。
    if (id.isEmpty() || ! scanner.hasExclusions (id))
        return;

    const auto ex = scanner.loadExclusions (id);

    for (int i = 0; i < slot.model.size(); ++i)
        slot.checked.set (i, ! ex.contains (slot.model.getReference (i).index));
}

void ParamRouletteProcessor::applyStoredSpans (int chainSlot)
{
    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return;

    auto& slot = chain[(size_t) chainSlot];
    const auto& d = slot.desc;

    if (d.name.isEmpty() && d.fileOrIdentifier.isEmpty())
        return;

    const auto id = d.createIdentifierString();

    if (id.isEmpty() || ! scanner.hasSpans (id))
        return;

    const auto spans = scanner.loadSpans (id);

    for (auto& info : slot.model)
        if (const auto it = spans.find (info.index); it != spans.end())
            info.span = it->second;
}

void ParamRouletteProcessor::saveSpansNow()
{
    const auto id = getHostedId();
    if (id.isEmpty())
        return;

    std::map<int, float> spans;
    for (const auto& info : cur().model)
        if (info.hasCustomSpan())
            spans[info.index] = info.span;

    scanner.saveSpans (id, spans);
}

void ParamRouletteProcessor::setParamSpan (int modelIndex, float span)
{
    auto& m = cur().model;

    if (! juce::isPositiveAndBelow (modelIndex, m.size()))
        return;

    // 负数统一成 −1（= 用角色默认档）。上限 1.0：再大就超出归一化量程了。
    m.getReference (modelIndex).span = (span < 0.0f) ? -1.0f : juce::jmin (1.0f, span);

    saveSpansNow();
    sendChangeMessage();
}

void ParamRouletteProcessor::saveExclusionsNow()
{
    const auto id = getHostedId();
    if (id.isEmpty())
        return;

    const auto& m = cur().model;
    const auto& c = cur().checked;

    juce::Array<int> ex;
    for (int i = 0; i < m.size(); ++i)
        if (i < c.size() && ! c[i])
            ex.add (m.getReference (i).index);

    scanner.saveExclusions (id, ex);
}

//==============================================================================
void ParamRouletteProcessor::refreshModelValues()
{
    auto& s = cur();

    if (s.plugin != nullptr)
        refreshValues (*s.plugin, s.model);
}

void ParamRouletteProcessor::randomizeNow()
{
    auto& slot = cur();

    if (slot.plugin == nullptr)
        return;

    auto& plugin = *slot.plugin;

    // 压历史必须在**任何**改动之前，而且在 refreshModelValues 之前也没关系——
    // captureSnapshot 直接问插件要值和状态块，不看我们的 model 缓存。
    pushHistory();

    // 先记下改动前的值，随机化之后才能在界面上显示"旧 → 新"。
    // 值本身可能已经陈旧（子插件界面被拖过），先刷一遍再记。
    refreshModelValues();
    snapshotPrevValues (slot.model);

    const auto ms = getMotionSettings();

    if (motionMode)
    {
        // 基座没锁就先锁一份当前状态。用户按下"掷"的那一刻手上的音色就是他要的
        // 出发点——逼他先点一下「设为基座」只是多一道仪式。
        //
        // 走 lockMotionBase() 而不是就地 captureSnapshot：基座相关的字段有四个
        // （快照 / program / 名字 / 计数），分两处赋值迟早会漏掉一个。
        if (! slot.base.used)
            lockMotionBase();

        // **顺序不能反**：轮廓走的是 setStateInformation，它会把整个状态
        // （包括所有参数值）覆盖成状态块里的内容。所以先改曲线写回，再改参数。
        //
        // 三个关键点：
        //   - 未改动的字节从**基座的状态块**取，不从当前状态取。从当前值出发的话
        //     反复生成会累积漂移，那是随机游走不是生成变体。
        //   - fauna 只改 EG4（组 3）：实测 EG3 那条对音高没有可听差别，
        //     动它只是无谓地改掉用户的 preset。
        //   - program 变了就整个中止：用户中途换了 preset，基座已经对不上。
        const int onlyGroup = slot.profile.looksLikeFauna ? 3 : -1;

        const auto cr = randomiseContours (plugin, ms.semantic, ms.motion, rng,
                                           &slot.base.childState, onlyGroup,
                                           slot.baseProgram);

        slot.lastContoursWritten = cr.written;

        if (cr.failed)
        {
            // 写到一半失败：把基座整份写回去，别留下半套状态。
            restoreSnapshot (slot.base, plugin);
            refreshModelValues();
            lastMotion = {};
            lastMotion.note = "曲线写入失败（preset 变了？）——已回到基座"_u8;
            sendChangeMessage();
            return;
        }

        lastMotion = applyMotion (plugin, slot.model, slot.checked, slot.profile,
                                  slot.base.values, ms, rng, cr.stages);

        if (cr.written > 0)
            lastMotion.note << "  ·  曲线 "_u8 << cr.written << " 条 / "_u8 << cr.stages << " 段"_u8;

        // 运动层和普通随机化共用"上次结果"那一行，别让旧数字留在界面上骗人
        lastResult = { lastMotion.touched, lastMotion.moved };
    }
    else
    {
        RandomSettings s;
        s.amount    = pAmount->get();
        s.lo        = pRangeLo->get();
        s.hi        = pRangeHi->get();
        s.maxParams = pMaxParams->get();   // 和运动模式音色层共用同一个控件
        s.normalise();

        lastResult = randomize (plugin, slot.model, slot.checked, s, rng);
        lastMotion = {};
    }

    refreshModelValues();
    lastChangedCount = markChangedValues (slot.model);

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
    auto* p = cur().plugin.get();
    return p != nullptr ? p->getNumPrograms() : 0;
}

int ParamRouletteProcessor::getHostedProgram() const
{
    auto* p = cur().plugin.get();
    return p != nullptr ? p->getCurrentProgram() : -1;
}

juce::String ParamRouletteProcessor::getHostedProgramName (int index) const
{
    auto* p = cur().plugin.get();

    if (p == nullptr || ! juce::isPositiveAndBelow (index, p->getNumPrograms()))
        return {};

    return p->getProgramName (index);
}

void ParamRouletteProcessor::setHostedProgram (int index)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto& slot = cur();
    auto* p = slot.plugin.get();

    if (p == nullptr || ! juce::isPositiveAndBelow (index, p->getNumPrograms()))
        return;

    if (index == p->getCurrentProgram())
        return;

    // 不取 callbackLock：换 program 在 VST2 里是 effSetProgram，可能读采样、分配内存，
    // 拿着锁做这种事会拖出一大段音频空洞。宿主们（含 REAPER）都是这么调的。
    p->setCurrentProgram (index);

    // 基座失效：它记的是另一个 program 的参数值和轮廓。
    // **不自动重锁**——锁定是用户的决定；下次「生成变体」会自动锁当前状态。
    slot.base = Snapshot {};
    slot.baseProgram = -1;
    slot.baseName.clear();
    slot.baseGeneration = 0;

    refreshModelValues();
    ++structureGeneration;      // 178 个值全变了，整表重画
    sendChangeMessage();
}

MotionSettings ParamRouletteProcessor::getMotionSettings() const
{
    // 每次从参数现读。缓存一份成员的话，DAW 画在 Motion / Tone 上的自动化曲线
    // 就只能改到那份缓存之外的东西——也就是根本不生效。
    MotionSettings s;
    s.semantic = motionSemantic;
    s.motion   = pMotionAmt->get();
    s.tone     = pToneAmt->get();
    s.maxTone  = pMaxParams->get();
    s.normalise();
    return s;
}

void ParamRouletteProcessor::setMotionSettings (MotionSettings s)
{
    s.normalise();

    motionSemantic = s.semantic;
    *pMotionAmt    = s.motion;
    *pToneAmt      = s.tone;
    *pMaxParams    = s.maxTone;

    sendChangeMessage();
}

bool ParamRouletteProcessor::lockMotionBase()
{
    auto& slot = cur();

    if (slot.plugin == nullptr)
        return false;

    slot.base = captureSnapshot (*slot.plugin, "基座"_u8);
    slot.baseProgram = slot.plugin->getCurrentProgram();
    slot.baseName = getHostedProgramName (slot.baseProgram);
    ++slot.baseGeneration;

    sendChangeMessage();
    return true;
}

juce::String ParamRouletteProcessor::getMotionBaseLabel() const
{
    const auto& slot = cur();

    if (! slot.base.used)
        return "未设定"_u8;

    juce::String s;

    if (slot.baseName.isNotEmpty())
        s << slot.baseName;
    else if (slot.baseProgram >= 0)
        s << "program "_u8 << slot.baseProgram;
    else
        s << "当前状态"_u8;

    // #N 是重锁计数。点一次「重设基座」这个数就该 +1；不变就说明没生效。
    s << " #" << slot.baseGeneration;
    return s;
}

bool ParamRouletteProcessor::restoreMotionBase()
{
    auto& slot = cur();

    if (slot.plugin == nullptr || ! slot.base.used)
        return false;

    pushHistory();

    const bool ok = restoreSnapshot (slot.base, *slot.plugin);

    refreshModelValues();
    sendChangeMessage();
    return ok;
}

void ParamRouletteProcessor::restoreMotionBaseFromTree (int chainSlot, const juce::ValueTree& t)
{
    if (! juce::isPositiveAndBelow (chainSlot, kMaxChain))
        return;

    auto& slot = chain[(size_t) chainSlot];

    // 只是把基座读回来，**不**写进插件。恢复工程时插件的状态已经从
    // HOSTED/state 还原过了，再写一遍基座会把用户存工程前的改动抹掉。
    slot.base = t.hasType (ids::BASE) ? snapshotFromValueTree (t) : Snapshot {};
    slot.baseProgram = (slot.base.used && slot.plugin != nullptr)
                         ? slot.plugin->getCurrentProgram() : -1;

    // 标签故意写成一眼能认出来的样子。宿主在意料之外的时机重放状态时
    // （撤销、桥进程重启、复制 FX），基座会被这里换掉——界面上看到
    // "工程恢复 #0" 就说明发生了这件事，而不是"我锁的基座没生效"。
    slot.baseName = slot.base.used ? "工程恢复"_u8 : juce::String();
    slot.baseGeneration = 0;
}

void ParamRouletteProcessor::captureSlot (int slot)
{
    const auto targets = makeSnapTargets();

    if (getNumLoadedSlots() == 0)
        return;

    bank.capture (slot, targets.data(), (int) targets.size());
    sendChangeMessage();
}

void ParamRouletteProcessor::recallSlot (int slot)
{
    if (getNumLoadedSlots() == 0)
        return;

    pushHistory();

    const auto targets = makeSnapTargets();
    bank.recall (slot, targets.data(), (int) targets.size());

    refreshModelValues();
    sendChangeMessage();
}

std::array<SnapTarget, kMaxChain> ParamRouletteProcessor::makeSnapTargets()
{
    std::array<SnapTarget, kMaxChain> t;

    for (size_t i = 0; i < t.size(); ++i)
    {
        t[i].plugin  = chain[i].plugin.get();
        t[i].model   = &chain[i].model;
        t[i].checked = &chain[i].checked;
    }

    return t;
}

//==============================================================================
// 历史。游标算术全在 HistoryRing 里（那边可以被无头探针测），这边只管
// "抓当前状态"和"还原一份"这两件需要真插件的事。每链位一份历史。
void ParamRouletteProcessor::pushHistory()
{
    auto& slot = cur();

    if (slot.plugin != nullptr)
        slot.history.push (captureSnapshot (*slot.plugin, {}));
}

void ParamRouletteProcessor::applyHistoryEntry (const Snapshot& s)
{
    auto& slot = cur();

    if (slot.plugin == nullptr)
        return;

    // 和随机化一样先记旧值，这样「仅改动」视图能显示这一步翻回去动了什么。
    refreshModelValues();
    snapshotPrevValues (slot.model);

    restoreSnapshot (s, *slot.plugin);

    refreshModelValues();
    lastChangedCount = markChangedValues (slot.model);
    lastResult.moved = lastChangedCount;

    ++structureGeneration;      // 「仅改动」视图要重算可见行
    sendChangeMessage();
}

bool ParamRouletteProcessor::historyBack()
{
    auto& slot = cur();

    if (slot.plugin == nullptr)
        return false;

    // 回调只在游标位于尖端时才被调用，所以不会每次回退都白抓一份状态块。
    if (auto* s = slot.history.back ([&slot] { return captureSnapshot (*slot.plugin, {}); }))
    {
        applyHistoryEntry (*s);
        return true;
    }

    return false;
}

bool ParamRouletteProcessor::historyForward()
{
    auto& slot = cur();

    if (slot.plugin == nullptr)
        return false;

    if (auto* s = slot.history.forward())
    {
        applyHistoryEntry (*s);
        return true;
    }

    return false;
}

void ParamRouletteProcessor::clearSlot (int slot)
{
    bank.clearSlot (slot);
    sendChangeMessage();
}

void ParamRouletteProcessor::applyMorphNow()
{
    // 只存了一个快照时权重会全塌到它身上，等于每 33ms 强行召回一次，
    // 用户自己转旋钮都会被拽回去。两个起步才有"插值"可言。
    if (getNumLoadedSlots() == 0 || bank.numUsed() < 2)
        return;

    const auto targets = makeSnapTargets();
    bank.applyMorph (targets.data(), (int) targets.size(), getMorphMode(),
                     pMorphX->get(), pMorphY->get());

    hostedValuesDirty.store (true, std::memory_order_relaxed);
}

void ParamRouletteProcessor::startInertProbe (std::function<void (int, int)> onDone)
{
    auto& slot = cur();

    if (slot.plugin == nullptr)
    {
        if (onDone != nullptr)
            onDone (0, 0);
        return;
    }

    juce::WeakReference<ParamRouletteProcessor> safe (this);

    probe.start (*slot.plugin, slot.model, [safe, onDone] (int numInert, int numEndpointOnly)
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
    if (latencyDirty.exchange (false))
        updateChainLatency();

    if (randomPending.exchange (false))
        randomizeNow();
}

void ParamRouletteProcessor::timerCallback()
{
    if (morphDirty.exchange (false))
        applyMorphNow();

    // 链尾 limiter 的设置也在这里跟：它们是可自动化参数，用户或 DAW 随时会动。
    // 30 Hz 足够——这两个不是要做采样级自动化的东西。
    outputGuard.setCeilingDb (pCeiling->get());
    outputGuard.setEnabled (pGuardOn->get());
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
// 工程状态和链预设**共用这一棵树**。两边格式故意一样，免得出现
// "存工程能还原、读预设还原不了"这种偏差——那种偏差只会在用户真去读预设时才暴露。
juce::ValueTree ParamRouletteProcessor::buildStateTree() const
{
    juce::ValueTree v (ids::ROOT);
    v.setProperty (ids::version,   3,                      nullptr);
    v.setProperty (ids::amount,    pAmount->get(),         nullptr);
    v.setProperty (ids::lo,        pRangeLo->get(),        nullptr);
    v.setProperty (ids::hi,        pRangeHi->get(),        nullptr);
    v.setProperty (ids::morphMode, pMorphMode->getIndex(), nullptr);
    v.setProperty (ids::morphX,    pMorphX->get(),         nullptr);
    v.setProperty (ids::morphY,    pMorphY->get(),         nullptr);
    v.setProperty (ids::filter,    filterText,             nullptr);
    v.setProperty (ids::onlyMatch, onlyMatching,           nullptr);
    v.setProperty (ids::focused,   focused,                nullptr);
    v.setProperty (ids::ceiling,   pCeiling->get(),        nullptr);
    v.setProperty (ids::guardOn,   pGuardOn->get(),        nullptr);

    // 属性名保持不变，所以 v2 的工程照样读得进来——只是这三个值现在住在
    // 可自动化参数里而不是普通成员里。
    v.setProperty (ids::motionOn,  motionMode,              nullptr);
    v.setProperty (ids::semantic,  (int) motionSemantic,    nullptr);
    v.setProperty (ids::motionAmt, pMotionAmt->get(),       nullptr);
    v.setProperty (ids::toneAmt,   pToneAmt->get(),         nullptr);
    v.setProperty (ids::maxParams, pMaxParams->get(),       nullptr);

    // 勾选表和基座从 v3 起跟着各自的 HOSTED 子节点走，不再放在根上——
    // 一条链上每位的"我不想随机这些"是各自独立的。

    // —— 每链位一个 HOSTED 子节点，带 slot 属性。
    //
    // v2 只有一个 HOSTED、没有 slot 属性。读的那边靠"有没有 slot 属性"分辨，
    // 所以这里**必须**写 slot——哪怕只装了一个插件。
    for (int i = 0; i < kMaxChain; ++i)
    {
        auto& s = chain[(size_t) i];

        if (s.plugin == nullptr)
            continue;

        juce::ValueTree h (ids::HOSTED);
        h.setProperty (ids::slot, i, nullptr);
        h.setProperty (ids::bypass, s.bypassed, nullptr);

        if (auto xml = s.desc.createXml())
            h.setProperty (ids::desc, xml->toString(), nullptr);

        juce::MemoryBlock child;
        s.plugin->getStateInformation (child);
        h.setProperty (ids::state, juce::var (child), nullptr);

        // 勾选表和基座是**每链位**的，所以跟着各自的 HOSTED 节点走，
        // 不能像 v2 那样放在根上。
        juce::StringArray slotEx;
        for (int k = 0; k < s.model.size(); ++k)
            if (k < s.checked.size() && ! s.checked[k])
                slotEx.add (juce::String (s.model.getReference (k).index));

        h.setProperty (ids::excluded, slotEx.joinIntoString (","), nullptr);

        // 逐参数抖动幅度。也按插件持久化在 PropertiesFile 里，但工程里也要存一份：
        // 用户可能在这个工程里给同一个插件调了一套不同的幅度，那属于工程内容。
        juce::StringArray slotSpans;
        for (const auto& info : s.model)
            if (info.hasCustomSpan())
                slotSpans.add (juce::String (info.index) + "=" + juce::String (info.span, 4));

        if (! slotSpans.isEmpty())
            h.setProperty (ids::spans, slotSpans.joinIntoString (","), nullptr);

        h.addChild (snapshotToValueTree (s.base, ids::BASE), -1, nullptr);

        v.addChild (h, -1, nullptr);
    }

    v.addChild (bank.toValueTree(), -1, nullptr);

    return v;
}

void ParamRouletteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // 有宿主会在后台线程存工程。拿 callbackLock 挡住换插件那条路径上的指针交换。
    // （installHosted 在锁内只做三次 move，不会转消息循环，所以不存在死锁。）
    const juce::ScopedLock sl (getCallbackLock());

    // 必须走二进制流。createXml() 会把 var(MemoryBlock) 转成 base64 *字符串*，
    // 读回来就成了 String 而不是 binary，子插件状态直接丢。
    juce::MemoryOutputStream out (destData, false);
    buildStateTree().writeToStream (out);
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

    // 恢复工程：全局设置也要还原。
    restoreChainFromTree (v, true);
}

void ParamRouletteProcessor::restoreChainFromTree (const juce::ValueTree& v,
                                                   bool restoreGlobals,
                                                   LoadCallback onDone)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! v.isValid())
    {
        if (onDone != nullptr)
            onDone ("状态树无效"_u8);
        return;
    }

    if (restoreGlobals)
    {
        *pAmount    = (float) v.getProperty (ids::amount,    1.0f);
        *pRangeLo   = (float) v.getProperty (ids::lo,        0.0f);
        *pRangeHi   = (float) v.getProperty (ids::hi,        1.0f);
        *pMorphX    = (float) v.getProperty (ids::morphX,    0.0f);
        *pMorphY    = (float) v.getProperty (ids::morphY,    0.0f);
        *pMorphMode = (int)   v.getProperty (ids::morphMode, 0);

        filterText   = v.getProperty (ids::filter).toString();
        onlyMatching = (bool) v.getProperty (ids::onlyMatch, false);

        motionMode = (bool) v.getProperty (ids::motionOn, false);
        motionSemantic = (MotionSemantic) juce::jlimit (0, kNumMotionSemantics - 1,
                                                        (int) v.getProperty (ids::semantic,
                                                                             (int) MotionSemantic::Rise));

        // v2 的工程只有 motionAmt / toneAmt，没有 maxParams——缺省 0（不限制），
        // 也就是和以前的行为完全一致。
        *pMotionAmt = (float) v.getProperty (ids::motionAmt, 0.65f);
        *pToneAmt   = (float) v.getProperty (ids::toneAmt,   0.35f);
        *pMaxParams = (int)   v.getProperty (ids::maxParams, 0);

        // 链尾安全网：v2 的工程里没有这两个属性，缺省成"开着、−1 dB"。
        // 老工程升上来自动获得保护，这是对的默认。
        *pCeiling = (float) v.getProperty (ids::ceiling, -1.0f);
        *pGuardOn = (bool)  v.getProperty (ids::guardOn, true);
    }

    const int  version = (int) v.getProperty (ids::version, 2);
    const bool legacy  = version < 3;

    //== 收集要恢复的链位 ======================================================
    // 一位一位来，不能一起发：loading 是**一个**标志，并发加载会被挡掉，
    // 结果只有第一个成功、剩下的静默丢失。
    struct PendingLoad
    {
        int  slot = 0;
        bool bypassed = false;
        juce::PluginDescription desc;
        juce::MemoryBlock       childState;
        bool                    hasEx = false;
        juce::String            exCsv;
        juce::String            spansCsv;
        juce::ValueTree         baseTree;
    };

    auto jobs = std::make_shared<std::vector<PendingLoad>>();

    for (int i = 0; i < v.getNumChildren(); ++i)
    {
        const auto h = v.getChild (i);

        if (! h.hasType (ids::HOSTED))
            continue;

        PendingLoad job;

        // v2 的 HOSTED 没有 slot 属性 —— 那就是老工程，装进链位 0。
        job.slot = juce::jlimit (0, kMaxChain - 1, (int) h.getProperty (ids::slot, 0));
        job.bypassed = (bool) h.getProperty (ids::bypass, false);

        if (auto xml = juce::parseXML (h.getProperty (ids::desc).toString()))
            job.desc.loadFromXml (*xml);

        if (job.desc.fileOrIdentifier.isEmpty() && job.desc.name.isEmpty())
            continue;

        if (auto* mb = h.getProperty (ids::state).getBinaryData())
            job.childState = *mb;

        // v2 把勾选表和基座放在**根**上（那时只有一个插件）；v3 放在各自的 HOSTED 里。
        if (legacy)
        {
            job.hasEx    = v.hasProperty (ids::excluded);
            job.exCsv    = v.getProperty (ids::excluded).toString();
            job.baseTree = v.getChildWithName (ids::BASE);
        }
        else
        {
            job.hasEx    = h.hasProperty (ids::excluded);
            job.exCsv    = h.getProperty (ids::excluded).toString();
            job.spansCsv = h.getProperty (ids::spans).toString();
            job.baseTree = h.getChildWithName (ids::BASE);
        }

        jobs->push_back (std::move (job));
    }

    // 先把所有链位清空。恢复的内容可能比现在装的少，残留的插件会继续出声。
    for (int i = 0; i < kMaxChain; ++i)
        if (chain[(size_t) i].plugin != nullptr)
            unloadSlot (i);

    if (jobs->empty())
    {
        if (onDone != nullptr)
            onDone ({});
        return;
    }

    const int wantFocus = juce::jlimit (0, kMaxChain - 1, (int) v.getProperty (ids::focused, 0));
    auto snapTree = std::make_shared<juce::ValueTree> (v.getChildWithName (ids::SNAPSHOTS));
    auto failures = std::make_shared<juce::StringArray>();

    juce::WeakReference<ParamRouletteProcessor> safe (this);

    // 自引用的 shared_ptr<function>：链式异步加载，每一位完成后接着下一位。
    // 递归 lambda 在 C++17 里只能这么写。
    auto step = std::make_shared<std::function<void (size_t)>>();

    *step = [safe, jobs, snapTree, step, wantFocus, failures, onDone] (size_t index)
    {
        auto* self = safe.get();
        if (self == nullptr)
            return;

        if (index >= jobs->size())
        {
            // 全部装完才恢复快照和聚焦：快照现在是**整条链**的，
            // 中途恢复会对不上还没装好的链位。
            self->getBank().restoreFromValueTree (*snapTree);
            self->setFocusedSlot (wantFocus);

            self->refreshModelValues();
            ++self->structureGeneration;
            self->sendChangeMessage();

            if (onDone != nullptr)
                onDone (failures->isEmpty() ? juce::String()
                                            : ("这些链位没装上："_u8 + failures->joinIntoString ("、"_u8)));
            return;
        }

        const auto job = (*jobs)[index];

        self->loadPluginIntoSlot (job.slot, job.desc,
                                 [safe, job, step, index, failures] (const juce::String& error)
        {
            auto* s = safe.get();
            if (s == nullptr)
                return;

            // 某一位加载失败（插件卸载了 / 换机器了）**不能中断整条链**——
            // 剩下的还得装上，否则一个坏插件会让整个工程变空。
            if (error.isNotEmpty())
            {
                failures->add (job.desc.name.isNotEmpty() ? job.desc.name
                                                          : juce::String (job.slot + 1));
            }
            else if (auto* h = s->getSlotPlugin (job.slot))
            {
                if (job.childState.getSize() > 0)
                    h->setStateInformation (job.childState.getData(),
                                            (int) job.childState.getSize());

                // 顺序有讲究：installHosted 已经把这一位的基座清掉、
                // 勾选重置成默认了，所以这两步必须在它之后。
                if (job.hasEx)
                {
                    const auto ex = parseIndexList (job.exCsv);
                    auto& slot = s->chain[(size_t) job.slot];

                    for (int k = 0; k < slot.model.size(); ++k)
                        slot.checked.set (k, ! ex.contains (slot.model.getReference (k).index));
                }

                // 工程里存的逐参数幅度覆盖 PropertiesFile 里那份全局设定：
                // "我在这个工程里给它调过"比"我一般这么用"更具体。
                if (job.spansCsv.isNotEmpty())
                {
                    auto& slot = s->chain[(size_t) job.slot];

                    for (const auto& tok : juce::StringArray::fromTokens (job.spansCsv, ",", {}))
                    {
                        const auto eq = tok.indexOfChar ('=');
                        if (eq <= 0)
                            continue;

                        const int   pi  = tok.substring (0, eq).getIntValue();
                        const float val = tok.substring (eq + 1).getFloatValue();

                        if (val < 0.0f || val > 1.0f)
                            continue;

                        for (auto& info : slot.model)
                            if (info.index == pi)
                                info.span = val;
                    }
                }

                s->restoreMotionBaseFromTree (job.slot, job.baseTree);
                s->setSlotBypassed (job.slot, job.bypassed);
            }

            (*step) (index + 1);
        });
    };

    (*step) (0);
}

//==============================================================================
juce::File ParamRouletteProcessor::getChainPresetDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("ParamRoulette")
                  .getChildFile ("chains");

    dir.createDirectory();      // 已存在时是无操作
    return dir;
}

juce::String ParamRouletteProcessor::saveChainPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name).trim();

    if (clean.isEmpty())
        return "名字不能为空"_u8;

    if (getNumLoadedSlots() == 0)
        return "链上一个插件都没有，没什么可存的"_u8;

    juce::ValueTree tree;
    {
        // 和 getStateInformation 一样的理由：挡住换插件那条路径上的指针交换。
        const juce::ScopedLock sl (getCallbackLock());
        tree = buildStateTree();
    }

    const auto file = getChainPresetDir().getChildFile (clean + ".prchain");

    // 先写临时文件再替换：写到一半崩了不会留下一个半截的预设，
    // 而半截的预设读出来是"某几位装上了、某几位没有"，很难看出是文件坏了。
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());

        if (! out.openedOk())
            return "写不了 "_u8 + file.getFullPathName();

        tree.writeToStream (out);
    }

    if (! temp.overwriteTargetFileWithTemporary())
        return "替换失败 "_u8 + file.getFullPathName();

    return {};
}

void ParamRouletteProcessor::loadChainPreset (const juce::File& file, LoadCallback onDone)
{
    JUCE_ASSERT_MESSAGE_THREAD

    juce::FileInputStream in (file);

    if (! in.openedOk())
    {
        if (onDone != nullptr)
            onDone ("读不了 "_u8 + file.getFileName());
        return;
    }

    const auto tree = juce::ValueTree::readFromStream (in);

    if (! tree.isValid() || ! tree.hasType (ids::ROOT))
    {
        if (onDone != nullptr)
            onDone ("不是 ParamRoulette 的链预设："_u8 + file.getFileName());
        return;
    }

    // restoreGlobals = false：读一条链不该顺手改掉用户当前的随机化强度 /
    // morph 位置 / 语义选择。那些是"我现在怎么用"，不是"这条链是什么"。
    restoreChainFromTree (tree, false, std::move (onDone));
}

juce::Array<juce::File> ParamRouletteProcessor::listChainPresets() const
{
    auto files = getChainPresetDir().findChildFiles (juce::File::findFiles, false, "*.prchain");

    files.sort();
    return files;
}

} // namespace pr

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pr::ParamRouletteProcessor();
}
