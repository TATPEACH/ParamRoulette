#pragma once

#include "ParamModel.h"
#include "Randomizer.h"
#include "SnapshotBank.h"
#include "HistoryRing.h"
#include "MotionEngine.h"
#include "ContourIO.h"
#include "ChainRunner.h"
#include "OutputGuard.h"
#include "HostedSlot.h"
#include "PluginScanner.h"

namespace pr
{

/** 宿主本体。

    它同时是三样东西：
      1. 一个正常的 VST3 效果器 / 乐器（对 DAW 而言）
      2. 一个 VST3 宿主（对被托管的子插件而言）
      3. 随机化 + 快照插值的状态持有者

    **线程约定**（整个项目最容易出错的地方，写在这里免得每处再解释一遍）：
      - `processBlock` 在音频线程，只读 `hosted`，绝不写参数。
      - 所有 `setValueNotifyingHost`（随机化、morph、体检）都在消息线程。
        DAW 自动化打进来时只标脏，实际动手在 `handleAsyncUpdate` / `timerCallback`。
      - 换插件走 `installHosted`，顺序是
        `probe.cancel()` → 关子插件窗口 → suspend → 取 callbackLock → 换指针 →
        放锁 → prepare → 解 suspend → **在锁外**析构旧实例。
*/
class ParamRouletteProcessor final : public juce::AudioProcessor,
                                     public juce::ChangeBroadcaster,
                                     private juce::AudioProcessorListener,
                                     private juce::AudioProcessorParameter::Listener,
                                     private juce::AsyncUpdater,
                                     private juce::Timer
{
public:
    ParamRouletteProcessor();
    ~ParamRouletteProcessor() override;

    //==============================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                             { return true; }

    const juce::String getName() const override                 { return JucePlugin_Name; }
    bool   acceptsMidi() const override                         { return true; }
    bool   producesMidi() const override                        { return true; }
    bool   isMidiEffect() const override                        { return false; }
    double getTailLengthSeconds() const override;

    int    getNumPrograms() override                            { return 1; }
    int    getCurrentProgram() override                         { return 0; }
    void   setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override            { return "Default"; }
    void   changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int size) override;

    //==============================================================================
    // 托管。**一条链，最多 kMaxChain 个链位**，串联跑。
    //
    // 「链位」和「快照」是两个不同的东西，别混：链位是信号路径上的位置（6 个），
    // 快照是存下来的音色（4 个，kNumSlots）。
    //
    // 界面上的一切操作（随机化 / 快照 / 运动模式 / 列表）都作用在**当前聚焦的
    // 链位**上。所以 getModel() 之类的取值接口全部转发到 chain[focused]，
    // PluginEditor 那千把行几乎不用改。
    using LoadCallback = std::function<void (const juce::String& error)>;

    /** 异步加载到指定链位。慢插件（Serum 之类要读几百兆采样）不会卡住界面。
        回调在消息线程上跑；error 为空表示成功。 */
    void loadPluginIntoSlot (int chainSlot, const juce::PluginDescription&, LoadCallback onDone = {});
    void unloadSlot (int chainSlot);

    /** 加载到**当前聚焦的**链位。老代码路径，保持不变。 */
    void loadPlugin (const juce::PluginDescription& desc, LoadCallback onDone = {})
    { loadPluginIntoSlot (focused, desc, std::move (onDone)); }

   #if PR_TEST_HOOKS
    /** **只给 Tests/UiSnapshot.cpp 用。** 同步装一个现成的实例（假插件），
        跳过格式管理器的异步加载——截图工具要的是确定性的画面，
        不是"本机碰巧装了什么插件"。正式构建里这个函数不存在。 */
    void installForTesting (int chainSlot, std::unique_ptr<juce::AudioPluginInstance> p,
                            const juce::PluginDescription& d)
    { installHosted (chainSlot, std::move (p), d); }

    /** 固定随机种子。截图工具靠它出**逐像素可复现**的图——改共享的界面组件之后，
        拿新旧截图逐像素比，就能确认另一套皮肤一个像素都没变。 */
    void seedForTesting (juce::int64 seed) { rng.setSeed (seed); }
   #endif

    void loadPluginById (const juce::String& identifier, LoadCallback onDone = {});
    void unloadPlugin() { unloadSlot (focused); }

    bool isLoading() const noexcept                             { return loading; }

    //== 链位 ==================================================================
    int  getFocusedSlot() const noexcept                        { return focused; }
    void setFocusedSlot (int chainSlot);

    bool         isSlotLoaded (int chainSlot) const;
    juce::String getSlotName (int chainSlot) const;

    /** 旁通一个链位。旁通是**位置**的属性，换插件不会重置它。 */
    bool isSlotBypassed (int chainSlot) const;
    void setSlotBypassed (int chainSlot, bool);

    /** 把链位 `from` 上的插件挪到 `to`，中间的依次让位（插入语义，见 `moveChainPosition`）。

        挪的是**整个链位**：插件、勾选表、基座、历史、旁通都跟着它走——
        在机架里拖一个模块，用户以为挪的是"这个效果器"，它的旁通状态当然也跟着。
        （"旁通是位置的属性"那条说的是**换插件**不重置它，和这里不矛盾。）
        每份快照里那一位的状态也一起挪，聚焦跟着插件走。

        异步加载还没回来的时候拒绝（返回 false）：加载回调是按链位号装的，
        中途挪动会让它装到错的位置上。必须在消息线程上调用。 */
    bool moveSlot (int from, int to);

    /** 链上一共装了几个插件。界面用它决定要不要显示链相关的东西。 */
    int getNumLoadedSlots() const;

    //== 当前聚焦链位的转发 ====================================================
    juce::AudioPluginInstance* getHosted() const noexcept       { return cur().plugin.get(); }
    const juce::PluginDescription& getHostedDescription() const noexcept { return cur().desc; }
    juce::String getHostedName() const;
    juce::String getHostedId() const;

    /** 指定链位的插件实例。界面开浮动窗口要用（每链位一个窗口）。 */
    juce::AudioPluginInstance* getSlotPlugin (int chainSlot) const;

    /** 任意链位的参数模型 / 勾选表（只读）。Rack 外观的每个模块上都画着
        自己那个插件的参数图，不能只看聚焦的那一位。 */
    const juce::Array<ParamInfo>& getSlotModel (int chainSlot) const;
    const juce::Array<bool>&      getSlotChecked (int chainSlot) const;


    //==============================================================================
    // 子插件的 preset / program。
    //
    // 这一层不能省：轮廓改的是**当前 program** 的那条记录，而 fauna 的
    // program 0 叫 "blank"（四条包络内容一模一样，改了也听不出差别）。
    // 没有选择器就只能在 blank 上转圈。
    int          getHostedNumPrograms() const;
    int          getHostedProgram() const;
    juce::String getHostedProgramName (int index) const;

    /** 换 program。会**清掉运动模式的基座**——基座记的是另一个 program 的参数和
        轮廓，留着只会让下次生成被 program 校验挡掉。下次「生成变体」会自动锁新的。
        必须在消息线程上调用。 */
    void setHostedProgram (int index);

    //==============================================================================
    // 状态（editor 直接读写这几个）。全部指向**当前聚焦的链位**。
    PluginScanner&          getScanner()  noexcept              { return scanner; }
    juce::Array<ParamInfo>& getModel()    noexcept              { return cur().model; }
    juce::Array<bool>&      getChecked()  noexcept              { return cur().checked; }
    SnapshotBank&           getBank()     noexcept              { return bank; }
    InertProbe&             getProbe()    noexcept              { return probe; }
    RandomResult            getLastResult() const noexcept      { return lastResult; }

    /** 上一次操作实际改动了几个参数（按**读回来的值**算，不是按写了几次算）。
        插件把写入吸附或弹回时，"写了几次"是虚高的。 */
    int getLastChangedCount() const noexcept                    { return lastChangedCount; }

    /** 子插件自己动了参数（它的界面被拖了 / DAW 自动化）。
        editor 用它决定要不要重新读一遍显示值，省掉每帧几百次字符串格式化。 */
    bool consumeHostedValuesDirty() noexcept { return hostedValuesDirty.exchange (false); }

    /** 只在**结构**变化时 +1：换插件、卸载、模型重建。
        随机化 / 存快照也会发 ChangeBroadcaster，但那时候这个数不变——
        editor 拿它区分"整张表要重建"和"只要重画一遍"。 */
    int getStructureGeneration() const noexcept { return structureGeneration; }

    void refreshModelValues();

    /** 掷一次。运动模式开着就走 MotionEngine，否则走普通逐参数随机化。
        DAW 的 Randomize 参数和界面按钮都进这一个口子，所以两边行为必然一致。 */
    void randomizeNow();

    void captureSlot (int slot);
    void recallSlot (int slot);
    void clearSlot (int slot);

    //==============================================================================
    // 链预设：把整条链（每位的插件 + 状态 + 勾选表）存成一个文件。
    //
    // 和工程状态是两回事：工程状态跟着 .rpp 走，链预设是**跨工程复用**的——
    // "我调出来一条好听的 modern rumble 链"应该能在下一首歌里直接调出来。
    static juce::File getChainPresetDir();

    /** 存成 `<name>.prchain`。返回错误信息，空串 = 成功。 */
    juce::String saveChainPreset (const juce::String& name);

    /** 读一个链预设。**异步**——要逐个加载插件，完成后走 onDone。 */
    void loadChainPreset (const juce::File&, LoadCallback onDone = {});

    /** 目录里现有的链预设，按名字排序。 */
    juce::Array<juce::File> listChainPresets() const;

    //==============================================================================
    // 随机化历史（「上一个 / 下一个」）。
    //
    // 没有它的话，随机到好声音**之前**那一版就永久没了——而"上一个其实更好"
    // 是这类工具最常见的懊悔。插件内部没有 undo，这是替代品。
    //
    // 不进工程状态：20 份子插件状态块塞进 .rpp 会让工程文件膨胀到不可接受。
    bool historyBack();
    bool historyForward();
    bool canHistoryBack()    const noexcept { return cur().history.canBack(); }
    bool canHistoryForward() const noexcept { return cur().history.canForward(); }

    /** 当前在第几个状态上（0 起）。 */
    int getHistoryPos() const noexcept { return cur().history.getPos(); }

    /** 一共有几个状态可回。 */
    int getHistoryCount() const noexcept { return cur().history.getCount(); }

    //==============================================================================
    // 运动模式
    bool isMotionMode() const noexcept              { return motionMode; }
    void setMotionMode (bool on);

    const MotionProfile&  getMotionProfile()  const noexcept { return cur().profile; }

    /** 运动 / 音色强度和「每次只动 N 个」现在**存在可自动化参数里**，不是普通成员。
        所以这个函数每次从参数读，不缓存——否则 DAW 画的自动化曲线不会生效。 */
    MotionSettings        getMotionSettings() const;
    void                  setMotionSettings (MotionSettings);
    MotionResult          getLastMotion()     const noexcept { return lastMotion; }

    /** 上一次生成改写了几条包络曲线。0 = 这个插件的轮廓不是可识别格式，
        或者语义是「保持」。界面用它告诉用户曲线到底动没动。 */
    int getLastContoursWritten() const noexcept { return cur().lastContoursWritten; }

    /** 加载时探到的可识别轮廓组数。>0 表示这个插件的包络曲线能被改写
        （走状态块，不是走参数）。0 表示只能动参数层。 */
    int getContourGroups() const noexcept { return cur().contourGroups; }

    /** 锁定基座：把当前状态抓成一份快照，之后每次生成都从它出发。
        不锁的话连点十次会越走越远——那是随机游走，不是"生成变体"。 */
    bool lockMotionBase();
    bool restoreMotionBase();
    bool hasMotionBase() const noexcept             { return cur().base.used; }

    /** 基座的身份标签，形如 `bird 5 #3`。

        **界面必须显示它。** 之前只有一个"基座已锁定"，导致"我到底锁没锁上、
        锁的是哪一个"完全看不出来——出问题时既没法自查也没法报告。
        `#N` 每次重锁 +1，所以点一下「重设基座」这个数就该变；不变就是没生效。 */
    juce::String getMotionBaseLabel() const;

    /** 从工程状态里恢复某个链位的基座。只在 applyPendingState 的加载回调里用——
        必须在 installHosted 把基座清掉**之后**才轮到它。 */
    void restoreMotionBaseFromTree (int chainSlot, const juce::ValueTree&);

    /** 把当前勾选状态写进 PropertiesFile，下次加载同一个插件时还原。 */
    void saveExclusionsNow();

    /** 设某个参数的音色层抖动幅度（负数 = 用角色默认档）。会立刻持久化。
        `modelIndex` 是**模型下标**，不是参数下标。 */
    void setParamSpan (int modelIndex, float span);

    /** 把当前的逐参数幅度写进 PropertiesFile。 */
    void saveSpansNow();

    /** 体检。跑完通过 ChangeBroadcaster 通知界面刷新。 */
    void startInertProbe (std::function<void (int numInert, int numEndpointOnly)> onDone = {});

    //==============================================================================
    // 暴露给 DAW 自动化的参数。构造时一次性建好——VST3 的参数表不能中途增删。
    //
    // 注意"不能中途增删"说的是**运行时**。编译期在这里追加新参数是安全的：
    // VST3 宿主按参数 ID（JUCE 拿 ParameterID 字符串哈希出来的）匹配自动化，
    // 不按下标，所以老工程画在 Morph X 上的曲线不会因为后面多了几个参数而错位。
    juce::AudioParameterFloat*  pMorphX    = nullptr;
    juce::AudioParameterFloat*  pMorphY    = nullptr;
    juce::AudioParameterFloat*  pAmount    = nullptr;
    juce::AudioParameterFloat*  pRangeLo   = nullptr;
    juce::AudioParameterFloat*  pRangeHi   = nullptr;
    juce::AudioParameterBool*   pRandomize = nullptr;
    juce::AudioParameterChoice* pMorphMode = nullptr;

    // 后加的三个。运动 / 音色强度以前是普通成员、画不了自动化——
    // 而"让音色强度跟着一条曲线走"恰好是这套语义系统最自然的用法。
    juce::AudioParameterFloat*  pMotionAmt = nullptr;
    juce::AudioParameterFloat*  pToneAmt   = nullptr;

    /** 每次最多动几个参数。0 = 不限。普通随机化和运动模式的**音色层**共用它。
        （运动层不受限，见 MotionSettings::maxTone 的说明。） */
    juce::AudioParameterInt*    pMaxParams = nullptr;

    // 链尾安全网。默认**开着**——随机化把全通反馈拉满炸出 +40 dB 或 NaN
    // 是完全现实的结果，而爆音伤耳朵、NaN 会污染整条总线。
    juce::AudioParameterFloat*  pCeiling  = nullptr;
    juce::AudioParameterBool*   pGuardOn  = nullptr;

    /** 链尾 limiter 最近一块的增益衰减（dB），给界面显示。 */
    float getGuardGrDb() const noexcept { return outputGuard.getGainReductionDb(); }

    /** 子插件吐过非法样本吗？读一次就清。
        界面必须把它显示出来——否则用户只听到突然静音，完全不知道发生了什么。 */
    bool consumeSawNonFinite() noexcept { return outputGuard.consumeSawNonFinite(); }

    /** 链尾输出电平，给硬件皮肤的 VU 表。**只在消息线程调**。

        rms 是最近一块的（两个声道取大的），peak 是自上次调用以来的最大绝对值（读一次就清）。
        音频停了（宿主旁通 / 停播放不再调 processBlock）时这两个数会卡在最后一块上，
        所以连续 ~200ms 没有新块就当作静音报 0——不然指针会定在半空。 */
    void getOutputLevel (float& rms, float& peak) noexcept;

    /** 各级的峰值电平（线性），给 Rack 外观每个模块上的信号灯用。读一次就清。
        下标：`meterInL/R` 输入，`meterSlot (i)` 第 i 个链位处理完之后，`meterOutL/R` 输出。 */
    static constexpr int meterInL  = 0;
    static constexpr int meterInR  = 1;
    static constexpr int meterSlot (int i) noexcept { return 2 + i; }
    static constexpr int meterOutL = 2 + kMaxChain;
    static constexpr int meterOutR = 3 + kMaxChain;
    static constexpr int numMeters = 4 + kMaxChain;

    float consumeMeterPeak (int index) noexcept;

    /** 自上次询问以来宿主送进来过 MIDI 吗？读一次就清。 */
    bool consumeMidiActivity() noexcept { return midiSeen.exchange (false); }

    MorphMode getMorphMode() const noexcept
    {
        return pMorphMode->getIndex() == 1 ? MorphMode::TwoD : MorphMode::OneD;
    }

    //==============================================================================
    /** editor 装的钩子：换插件前必须**同步**关掉那个链位的浮动窗口。
        窗口归 editor 所有（JUCE 的规矩：界面的东西归界面管），但销毁时机归这里定。
        参数是链位号；传 -1 表示"全关"。 */
    std::function<void (int chainSlot)> closeHostedWindow;

    /** 界面上的 filter 文本 / "仅显示匹配"，放这里是为了能进工程状态。 */
    juce::String filterText;
    bool         onlyMatching = false;

private:
    //==============================================================================
    void handleAsyncUpdate() override;
    void timerCallback() override;

    // AudioProcessorListener —— 监听**被托管的**子插件
    // ChangeDetails 写全名：AudioProcessor 和 AudioProcessorListener 都是基类，
    // 不限定的话名字查找有歧义风险。
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*,
                                const juce::AudioProcessorListener::ChangeDetails&) override;

    // AudioProcessorParameter::Listener —— 监听**自己的**参数（DAW 自动化会走这里）
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}

    static BusesProperties makeBuses();

    /** 当前聚焦的链位。所有"就一个插件"的老代码路径都走它。 */
    HostedSlot&       cur()       noexcept { return chain[(size_t) focused]; }
    const HostedSlot& cur() const noexcept { return chain[(size_t) focused]; }

    void installHosted (int chainSlot,
                        std::unique_ptr<juce::AudioPluginInstance> next,
                        const juce::PluginDescription& desc);
    void prepareHosted (juce::AudioPluginInstance& p, double sr, int bs);
    void prepareSlot (HostedSlot&, double sr, int bs);

    /** 重算链上的总延迟并报给 DAW。任何一位的延迟变了都要调。 */
    void updateChainLatency();

    void rebuildModel (int chainSlot);
    void applyStoredExclusions (int chainSlot);
    void applyStoredSpans (int chainSlot);
    void applyMorphNow();
    void applyPendingState();

    /** 工程状态和链预设共用的那棵树。见 .cpp 里的说明。 */
    juce::ValueTree buildStateTree() const;

    /** 给 SnapshotBank 用的链视图。每块现取，不缓存——插件指针会变。 */
    std::array<SnapTarget, kMaxChain> makeSnapTargets();

    /** 把一棵 ValueTree 恢复成整条链。工程状态和链预设共用这一份实现——
        两边格式故意一样，免得"存工程能还原、读预设还原不了"这种偏差。

        `restoreGlobals = false` 时只装链，不动 amount / morph / 语义那些全局设置：
        读链预设不该顺手改掉用户当前的随机化强度。 */
    void restoreChainFromTree (const juce::ValueTree&, bool restoreGlobals,
                               LoadCallback onDone = {});

    /** 把当前状态压进历史。改动状态的动作**之前**调。
        morph 拖动绝不调——30Hz 会在几秒内填满整个环。 */
    void pushHistory();

    /** 还原历史里的一份，并把"旧 → 新"和「仅改动」视图一起更新。 */
    void applyHistoryEntry (const Snapshot&);

    //==============================================================================
    PluginScanner scanner;
    SnapshotBank  bank;
    InertProbe    probe;
    juce::Random  rng;
    OutputGuard   outputGuard;

    /** 一条链。每位持有自己的插件、参数模型、勾选表、基座、历史。

        为什么每位都要一份勾选表和基座：一条链上的 EQ 和 disperser，
        "我不想随机这些"完全是两回事，共享一份等于互相打乱。 */
    std::array<HostedSlot, kMaxChain> chain;

    /** 界面上正在操作哪个链位。**不影响音频**，只决定"随机化/列表作用在谁身上"。 */
    int  focused = 0;
    bool loading = false;

    // —— 「上一次操作」的结果。全局一份就够：同一时刻只会有一个操作在跑。
    RandomResult lastResult;
    int          lastChangedCount = 0;
    MotionResult lastMotion;

    // 运动模式的开关和语义是全局的（"我现在要生成变体"是个模式，不是某个插件的属性）。
    // 基座 / profile / 轮廓那些都是**每链位**的，在 HostedSlot 里。
    //
    // 强度那两个数也不在这里——它们是可自动化参数（pMotionAmt / pToneAmt），
    // 见 getMotionSettings()。
    bool           motionMode = false;
    MotionSemantic motionSemantic = MotionSemantic::Rise;

    std::atomic<bool> morphDirty        { false };
    std::atomic<bool> randomPending     { false };
    std::atomic<bool> randomWasDown     { false };
    std::atomic<bool> latencyDirty      { false };
    std::atomic<bool> hostedValuesDirty { false };

    // 输出电平表（音频线程写，消息线程读）。meterBlocks 每块 +1，用来判断"音频还在跑吗"。
    std::atomic<float>    meterRms    { 0.0f };
    std::atomic<float>    meterPeak   { 0.0f };
    std::atomic<uint32_t> meterBlocks { 0 };

    std::array<std::atomic<float>, (size_t) numMeters> stagePeaks {};
    std::atomic<bool> midiSeen { false };
    uint32_t meterLastSeen = 0;       ///< 只有消息线程碰
    int      meterStale    = 0;

    int idxMorphX = -1, idxMorphY = -1, idxRandomize = -1, idxMorphMode = -1;

    int structureGeneration = 0;

    juce::ValueTree pendingState;   ///< setStateInformation 收下、等消息线程处理

    static constexpr int kMorphHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRouletteProcessor)
    JUCE_DECLARE_WEAK_REFERENCEABLE (ParamRouletteProcessor)
};

} // namespace pr
