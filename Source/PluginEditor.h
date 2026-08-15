#pragma once

#include "PluginProcessor.h"
#include "ParamListView.h"
#include "MorphPad.h"
#include "HostedPluginWindow.h"

namespace pr
{

/** 主界面。

    **它自己不持有任何状态**——model / checked / bank / filter 文本全在 processor 上。
    界面关掉再打开、或者在别的 DAW 窗口里重开，看到的还是同一份东西。

    刷新分两档，别混：
      - `getStructureGeneration()` 变了 = 换插件 / 重建模型 / 恢复工程 → `rebuildAll()`
      - 只是值动了（子插件被拖、morph 在推）→ `refreshModelValues()` + `list.refresh()`

    processor 的定时器**只管 morph**，不刷显示值。所以这边必须自己轮询
    `consumeHostedValuesDirty()`，否则子插件界面上的改动在列表里看不到。
*/
class ParamRouletteEditor final : public juce::AudioProcessorEditor,
                                  private juce::ChangeListener,
                                  private juce::Timer
{
public:
    explicit ParamRouletteEditor (ParamRouletteProcessor&);
    ~ParamRouletteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //==============================================================================
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void rebuildAll();
    void updateStatus();
    void refreshSlotButtons();
    void syncFromParameters();
    juce::String buildStatusText();

    void showPluginMenu();
    void showScanWindow();
    void closeScanWindow();
    void loadDescription (const juce::PluginDescription&);
    void toggleHostedWindow();

    /** 关掉某一位的浮动窗口；-1 = 全关。processor 换插件前会调它。 */
    void closeHostedWindows (int chainSlot);
    void doUnload();

    void applyFilter();
    void confirmProbe();
    void startProbe();
    void offerUncheckInert (int numInert);

    void pushMotionSettings();
    void syncMotionUI();
    void rebuildPresetList();

    /** 历史按钮的可用性和 N/M 计数。每帧刷——它随随机化、召回、还原基座变。 */
    void refreshHistoryUI();

    /** 链位一行：谁被聚焦、哪些装了插件、旁通状态、以及 limiter 的读数。 */
    void refreshChainUI();

    void wireSlider (juce::Slider&, juce::AudioParameterFloat&);

    //==============================================================================
    ParamRouletteProcessor& proc;

    // 第一个成员：这样它比所有子组件晚析构。dtor 里还会再 setLookAndFeel(nullptr) 一道保险。
    juce::LookAndFeel_V4 lnf;

    /** 没有这个实例的话 `setTooltip` 是死代码——JUCE 靠一个 TooltipWindow
        去轮询鼠标底下的组件并画气泡，光设文字不会有任何显示。 */
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton bPick   { "选择插件…"_u8 };
    juce::TextButton bScan   { "扫描…"_u8 };
    juce::TextButton bUI     { "界面"_u8 };
    juce::TextButton bUnload { "卸载"_u8 };
    juce::Label      lPlugin;

    /** 链位一行。

        **「链位」和「快照」是两个不同的东西**：链位是信号路径上的位置（6 个，
        串联跑），快照是存下来的音色（4 个，在窗口底部）。文案上刻意不复用
        "slot" 这个词，免得两边混起来。

        每位一个聚焦按钮（点了之后所有操作都作用在它上面）+ 一个旁通勾选。 */
    struct ChainSlotUI
    {
        juce::TextButton   focus;
        juce::ToggleButton bypass;
    };
    std::array<ChainSlotUI, kMaxChain> chainUI;

    /** 链尾安全网的读数：增益衰减，以及"子插件吐了 NaN"这种必须让用户知道的事。 */
    juce::Label lGuard;

    /** 子插件的 preset 列表。轮廓改的是当前 program 的那条记录，
        所以这个选择器不是锦上添花——没它就只能在 program 0 上转圈。 */
    juce::ComboBox   cbPreset;

    juce::Label  lAmount { {}, "强度"_u8 }, lLo { {}, "下限"_u8 }, lHi { {}, "上限"_u8 };
    juce::Slider sAmount, sLo, sHi;

    juce::TextButton bRandom { "随机化"_u8 };
    juce::TextButton bProbe  { "体检"_u8 };
    juce::Label      lResult;
    juce::ComboBox   cbMorphMode;

    /** 「每次只动 N 个」。0 = 不限。普通随机化和运动模式的音色层共用它。
        用 IncDecButtons 而不是滑条：主要用法是精确挑 1 或 2，拖滑条挑不准。 */
    juce::Label  lMaxParams { {}, "只动"_u8 };
    juce::Slider sMaxParams;

    /** 随机化历史。插件内部没有 undo，这两个按钮是替代品。
        箭头是 U+25C0 / U+25B6，**非 ASCII，必须带 `_u8`**——不带就是乱码。 */
    juce::TextButton bHistBack { "◀"_u8 };
    juce::TextButton bHistFwd  { "▶"_u8 };
    juce::Label      lHistory;      ///< "3/12"

    // 运动模式一行。开着的时候「随机化」走 MotionEngine 而不是逐参数掷。
    juce::ToggleButton tbMotion { "运动模式"_u8 };
    juce::ComboBox     cbSemantic;
    juce::Label        lMotionAmt { {}, "运动"_u8 }, lToneAmt { {}, "音色"_u8 };
    juce::Slider       sMotionAmt, sToneAmt;
    // 「设为基座」/「还原基座」两个按钮以前叫「锁定基座」/「回到基座」——
    // 四个字、结构相同、并排放，扫一眼分不出哪个是"存"哪个是"取"。
    // 现在动词不同（设 / 还原），而且已设定后第一个会变成「重设基座」。
    juce::TextButton   bLockBase    { "设为基座"_u8 };
    juce::TextButton   bRestoreBase { "还原基座"_u8 };
    juce::Label        lProfile;

    juce::TextEditor   teFilter;
    juce::ComboBox     cbView;          ///< 全部 / 仅勾选 / 仅改动
    juce::ToggleButton tbOnlyMatching { "仅匹配"_u8 };
    juce::TextButton   bCheckMatch   { "勾选匹配"_u8 };
    juce::TextButton   bUncheckMatch { "取消匹配"_u8 };
    juce::TextButton   bAll          { "全选"_u8 };
    juce::TextButton   bNone         { "全不选"_u8 };
    juce::TextButton   bInvert       { "反选"_u8 };

    juce::Label lStatus;

    ParamListView list;

    /** 一个 slot 一组：存 / 召回 / 清空。 */
    struct SlotControls
    {
        juce::TextButton capture, recall, clear;
    };
    std::array<SlotControls, kNumSlots> slotUI;

    MorphPad pad;

    /** 每链位一个浮动窗口。调链的时候要能同时看两个插件的界面对比，
        所以不是"一个窗口轮流显示"。 */
    std::array<std::unique_ptr<HostedPluginWindow>, kMaxChain> hostedWindows;
    std::unique_ptr<juce::DocumentWindow> scanWindow;

    int          lastGeneration = -1;
    bool         syncing = false;   ///< 参数 → 界面回写中，别再反向推回去
    juce::String note;              ///< 最近一次动作的说明（加载失败 / 体检结论 / 存了哪个 slot）

    /** NaN 警告的显示截止时间（毫秒计数）。
        latch 是读一次就清的，所以必须自己把它撑住几秒——否则那行字会在
        20 Hz 的刷新里闪一帧就没了，用户根本来不及看见。 */
    juce::uint32 guardWarnUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRouletteEditor)
};

} // namespace pr
