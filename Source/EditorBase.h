#pragma once

#include "PluginProcessor.h"
#include "ParamListView.h"
#include "ChainStrip.h"
#include "Widgets.h"

namespace pr
{

/** 界面皮肤。两套界面共用同一个 processor、同一套行为，只是长相和交互手感不同。 */
enum class Skin
{
    grid     = 0,   ///< Bitwig The Grid 风格（深色画布 + 圆角模块）
    hardware = 1    ///< 音频硬件拟物风（藏青面板 + 原木边框 + 旋钮 / VU 表 / 荧光屏）
};

/** editor 外壳（`ParamRouletteEditor`）提供给皮肤的服务。

    **窗口归外壳管，不归皮肤管**（子插件的浮动窗口、扫描、标签）：换皮肤时皮肤整个
    被删掉重建，要是窗口跟着皮肤走，用户只是换个外观，开着的插件界面就全被关掉了，
    扫描也会断在一半。 */
class EditorHost
{
public:
    virtual ~EditorHost() = default;

    virtual void openHostedWindow (int chainSlot) = 0;
    virtual void closeHostedWindows (int chainSlot) = 0;     ///< -1 = 全关
    virtual bool isHostedWindowOpen (int chainSlot) const = 0;

    virtual void showScanWindow() = 0;
    virtual void showTagWindow() = 0;    ///< 插件功能标签窗口。它是"随机效果器链"的数据录入口，见 PluginTagView

    /** 换一套皮肤。**异步**执行——调用者通常就是皮肤自己的按钮，
        不能在它的点击回调里把它连同整棵组件树删掉。 */
    virtual void requestSkin (Skin) = 0;

    /** 两套 LookAndFeel 都归外壳、和 editor 同寿（换皮肤时还开着的工具窗口挂在上面）。 */
    virtual juce::LookAndFeel_V4& lookAndFeelFor (Skin) = 0;
};

/** 每个运动语义的一句话说明——直接写在选择器旁边，不用去翻手册。 */
juce::String semanticHint (MotionSemantic);

//==============================================================================
/** 两套皮肤共用的**行为**：菜单、体检、链预设、快捷键、状态栏文案、参数写回……

    只管"做什么"，不管"长什么样"。皮肤（`GridEditor` / `HardwareEditor`）持有自己的
    控件、负责布局和绘制，把控件的回调接到这里的动作函数上。这样一个功能只写一遍，
    两套界面的行为不会慢慢分叉（"Grid 版能做、硬件版忘了接"）。

    **它自己不持有任何状态**——模型 / 勾选 / 快照 / filter 文本全在 processor 上。

    刷新分两档，别混：
      - `getStructureGeneration()` 变了 = 换插件 / 换链位 / 重建模型 / 恢复工程 → `rebuildAll()`
      - 只是值动了（子插件被拖、morph 在推）→ `refreshModelValues()` + 列表重画

    processor 的定时器**只管 morph**，不刷显示值。所以这边必须自己轮询
    `consumeHostedValuesDirty()`，否则子插件界面上的改动在列表里看不到。 */
class EditorBase : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    EditorBase (ParamRouletteProcessor&, EditorHost&, juce::LookAndFeel_V4&);
    ~EditorBase() override;

    /** 外壳在设好尺寸之后调一次：挂上监听、第一次 rebuild、启动定时器。
        不能放在构造函数里——那时虚函数还指不到子类。 */
    void start();

    /** 外壳通知：某个子插件窗口开了 / 被用户关掉了，或者标签窗口关了（标签可能改了）。 */
    void windowsChanged() { updateStatus(); }

    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

protected:
    //== 皮肤必须实现的 ===========================================================
    virtual void rebuildSkin() = 0;             ///< 结构变了（列表重新指数据源、preset 表重建……）
    virtual void updateStatus() = 0;            ///< 文字 / 启用状态 / 指示灯
    virtual void syncFromParameters() = 0;      ///< 可自动化参数 → 控件
    virtual void syncMotionUI() = 0;
    virtual void refreshSnapshotTiles() = 0;
    virtual void focusFilter() = 0;             ///< Ctrl+F

    virtual ParamListView& paramList() = 0;
    virtual ChainStrip&    chainView() = 0;

    //== 刷新 =====================================================================
    void rebuildAll();
    void refreshChainUI();                      ///< 把链位 / 安全网状态塞进 chainView()

    std::array<SnapshotTiles::Tile, 4> makeSnapshotTiles() const;
    void fillPresetBox (juce::ComboBox&);       ///< 重建子插件 program 列表
    void syncPresetBox (juce::ComboBox&);       ///< 子插件自己换了 program，这边跟上

    //== 文案 =====================================================================
    juce::String buildStatusText();
    juce::String historyText() const;           ///< "3 / 12" 或 "—"

    /** 状态栏上那句"刚刚发生了什么"。新消息会高亮几秒再淡回普通颜色。 */
    void setNote (const juce::String&);
    bool isNoteFresh() const;

    /** 「上次随机：改动 12 / 写入 40」那两行。suspicious = 该标成警告色。 */
    juce::String resultText (bool& suspicious) const;

    /** 运动层的角色识别情况 + 曲线能不能写。warn = 认不出包络，该标成警告色。 */
    juce::String profileText (bool& warn) const;

    //== 动作 =====================================================================
    void doRoll();
    void doHistory (bool back);

    void showPluginMenu (int chainSlot, juce::Component* anchor);
    void showModuleMenu (int chainSlot);
    void loadDescription (int chainSlot, const juce::PluginDescription&);
    void doUnload (int chainSlot);
    void toggleHostedWindow();

    void showScanWindow()  { host.showScanWindow(); }
    void showTagWindow()   { host.showTagWindow(); }

    void applyFilter (const juce::String& text, bool onlyMatching);
    void showListMenu (juce::Component& anchor);
    void checkMatching (bool value);

    void confirmProbe();
    void startProbe();
    void offerUncheckInert (int numInert);

    void promptSaveChainPreset();
    void showChainPresetMenu (juce::Component& anchor);

    void selectPreset (int index);
    void setSlotBypass (int chainSlot, bool bypassed);
    void setGuardOn (bool on);
    void setMotionOn (bool on);
    void setSemantic (int index);
    void setMaxParams (int n);
    void setMorphModeIndex (int index);
    void lockBase();
    void restoreBase();

    void storeSnapshot (int i);
    void recallSnapshot (int i);
    void clearSnapshot (int i);

    void morphDragStart();
    void morphMoved (float x, float y);
    void morphDragEnd();

    /** 旋钮 ↔ 可自动化参数：拖动开始 / 结束发手势，值变了写回（回写中不写）。 */
    void bindSlider (juce::Slider&, juce::AudioParameterFloat&);

    /** 一次性的参数写入（开关、档位）：完整走一遍 begin → set → end，DAW 才录得到。 */
    static void writeParam (juce::RangedAudioParameter&, float plainValue);

    //==============================================================================
    ParamRouletteProcessor& proc;
    EditorHost&             host;
    juce::LookAndFeel_V4&   lnf;     ///< 归外壳，见 EditorHost::lookAndFeelFor

    bool         syncing = false;   ///< 参数 → 界面回写中，别再反向推回去
    juce::String note;              ///< 最近一次动作的说明（加载失败 / 体检结论 / 存了哪个快照）
    juce::uint32 noteTime = 0;      ///< note 写入的时刻，用来做"新消息高亮几秒"

    /** NaN 警告的显示截止时间（毫秒计数）。
        latch 是读一次就清的，所以必须自己把它撑住几秒——否则那行字会在
        20 Hz 的刷新里闪一帧就没了，用户根本来不及看见。 */
    juce::uint32 guardWarnUntil = 0;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    int lastGeneration = -1;
    int lastWinner = -2;     ///< 上次刷快照高亮时的 winner（-1 = 没有，-2 = 还没刷过）
    bool started = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorBase)
};

} // namespace pr
