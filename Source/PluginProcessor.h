#pragma once

#include "ParamModel.h"
#include "Randomizer.h"
#include "SnapshotBank.h"
#include "MotionEngine.h"
#include "ContourIO.h"
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
    // 托管
    using LoadCallback = std::function<void (const juce::String& error)>;

    /** 异步加载。慢插件（Serum 之类要读几百兆采样）不会卡住界面。
        回调在消息线程上跑；error 为空表示成功。 */
    void loadPlugin (const juce::PluginDescription& desc, LoadCallback onDone = {});
    void loadPluginById (const juce::String& identifier, LoadCallback onDone = {});
    void unloadPlugin();

    bool isLoading() const noexcept                             { return loading; }
    juce::AudioPluginInstance* getHosted() const noexcept       { return hosted.get(); }
    const juce::PluginDescription& getHostedDescription() const noexcept { return hostedDesc; }
    juce::String getHostedName() const;
    juce::String getHostedId() const;

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
    // 状态（editor 直接读写这几个）
    PluginScanner&          getScanner()  noexcept              { return scanner; }
    juce::Array<ParamInfo>& getModel()    noexcept              { return model; }
    juce::Array<bool>&      getChecked()  noexcept              { return checked; }
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
    // 运动模式
    bool isMotionMode() const noexcept              { return motionMode; }
    void setMotionMode (bool on);

    const MotionProfile&  getMotionProfile()  const noexcept { return motionProfile; }
    MotionSettings        getMotionSettings() const noexcept { return motionSettings; }
    void                  setMotionSettings (MotionSettings);
    MotionResult          getLastMotion()     const noexcept { return lastMotion; }

    /** 上一次生成改写了几条包络曲线。0 = 这个插件的轮廓不是可识别格式，
        或者语义是「保持」。界面用它告诉用户曲线到底动没动。 */
    int getLastContoursWritten() const noexcept { return lastContoursWritten; }

    /** 加载时探到的可识别轮廓组数。>0 表示这个插件的包络曲线能被改写
        （走状态块，不是走参数）。0 表示只能动参数层。 */
    int getContourGroups() const noexcept { return contourGroups; }

    /** 锁定基座：把当前状态抓成一份快照，之后每次生成都从它出发。
        不锁的话连点十次会越走越远——那是随机游走，不是"生成变体"。 */
    bool lockMotionBase();
    bool restoreMotionBase();
    bool hasMotionBase() const noexcept             { return motionBase.used; }

    /** 基座的身份标签，形如 `bird 5 #3`。

        **界面必须显示它。** 之前只有一个"基座已锁定"，导致"我到底锁没锁上、
        锁的是哪一个"完全看不出来——出问题时既没法自查也没法报告。
        `#N` 每次重锁 +1，所以点一下「重设基座」这个数就该变；不变就是没生效。 */
    juce::String getMotionBaseLabel() const;

    /** 从工程状态里恢复基座。只在 applyPendingState 的加载回调里用——
        必须在 installHosted 把基座清掉**之后**才轮到它。 */
    void restoreMotionBaseFromTree (const juce::ValueTree&);

    /** 把当前勾选状态写进 PropertiesFile，下次加载同一个插件时还原。 */
    void saveExclusionsNow();

    /** 体检。跑完通过 ChangeBroadcaster 通知界面刷新。 */
    void startInertProbe (std::function<void (int numInert, int numEndpointOnly)> onDone = {});

    //==============================================================================
    // 暴露给 DAW 自动化的参数。构造时一次性建好——VST3 的参数表不能中途增删。
    juce::AudioParameterFloat*  pMorphX    = nullptr;
    juce::AudioParameterFloat*  pMorphY    = nullptr;
    juce::AudioParameterFloat*  pAmount    = nullptr;
    juce::AudioParameterFloat*  pRangeLo   = nullptr;
    juce::AudioParameterFloat*  pRangeHi   = nullptr;
    juce::AudioParameterBool*   pRandomize = nullptr;
    juce::AudioParameterChoice* pMorphMode = nullptr;

    MorphMode getMorphMode() const noexcept
    {
        return pMorphMode->getIndex() == 1 ? MorphMode::TwoD : MorphMode::OneD;
    }

    //==============================================================================
    /** editor 装的钩子：换插件前必须**同步**关掉子插件的浮动窗口。
        窗口归 editor 所有（JUCE 的规矩：界面的东西归界面管），但销毁时机归这里定。 */
    std::function<void()> closeHostedWindow;

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

    void installHosted (std::unique_ptr<juce::AudioPluginInstance> next,
                        const juce::PluginDescription& desc);
    void prepareHosted (juce::AudioPluginInstance& p, double sr, int bs);
    void rebuildModel();
    void applyStoredExclusions();
    void applyMorphNow();
    void applyPendingState();

    //==============================================================================
    PluginScanner scanner;
    SnapshotBank  bank;
    InertProbe    probe;
    juce::Random  rng;

    std::unique_ptr<juce::AudioPluginInstance> hosted;
    juce::PluginDescription                    hostedDesc;
    bool                                       loading = false;

    juce::Array<ParamInfo> model;
    juce::Array<bool>      checked;
    RandomResult           lastResult;
    int                    lastChangedCount = 0;

    // 运动模式。基座是一份完整快照（值 + 子插件状态块），所以"回到基座"是精确的。
    bool           motionMode = false;
    MotionProfile  motionProfile;
    MotionSettings motionSettings;
    MotionResult   lastMotion;
    Snapshot       motionBase;
    int            motionBaseProgram = -1;  ///< 锁定基座时插件在哪个 program
    juce::String   motionBaseName;          ///< 锁定时那个 program 的名字
    int            motionBaseGeneration = 0;///< 重锁次数，界面上显示成 #N
    int            lastContoursWritten = 0;
    int            contourGroups = 0;

    /** 子插件要的通道数比宿主给的多时用的暂存区（合成器挂在效果器版上会这样）。
        在 prepareHosted 里分配好，音频线程只读不分配。 */
    juce::AudioBuffer<float> hostedScratch;
    int                      hostedChannels = 0;

    std::atomic<bool> morphDirty        { false };
    std::atomic<bool> randomPending     { false };
    std::atomic<bool> randomWasDown     { false };
    std::atomic<bool> latencyDirty      { false };
    std::atomic<bool> hostedValuesDirty { false };

    int idxMorphX = -1, idxMorphY = -1, idxRandomize = -1, idxMorphMode = -1;

    int structureGeneration = 0;

    juce::ValueTree pendingState;   ///< setStateInformation 收下、等消息线程处理

    static constexpr int kMorphHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRouletteProcessor)
    JUCE_DECLARE_WEAK_REFERENCEABLE (ParamRouletteProcessor)
};

} // namespace pr
