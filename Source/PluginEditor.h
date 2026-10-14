#pragma once

#include "PluginProcessor.h"
#include "ParamListView.h"
#include "PluginTagView.h"
#include "MorphPad.h"
#include "HostedPluginWindow.h"
#include "GridLookAndFeel.h"
#include "Widgets.h"
#include "ChainStrip.h"

namespace pr
{

/** 主界面。

    **视觉上照着 Bitwig 的 The Grid 来**：深色点阵画布上摆几块圆角模块，
    每块模块标题用分类色；信号链画成用连线串起来的模块。布局（1240×780 起）：

        ┌ 顶栏：品牌 · 当前链位的插件 / preset / 界面 / 卸载 · 扫描 / 标签 ───────────┐
        ├ 信号链：IN → 1…6 → 输出保护 → OUT ────────────────────────────── 存链 读链 ┤
        ├ 随机化 ┐ ┌ 参数 ─────────────────────────────────────┐ ┌ 快照 · Morph ┐
        │ 运动语义│ │                                             │ │  方块 + pad  │
        ├ 状态栏 ──────────────────────────────────────────────── 快捷键提示 ┤

    **它自己不持有任何状态**——模型 / 勾选 / 快照 / filter 文本全在 processor 上。
    界面关掉再打开、或者在别的 DAW 窗口里重开，看到的还是同一份东西。

    刷新分两档，别混：
      - `getStructureGeneration()` 变了 = 换插件 / 换链位 / 重建模型 / 恢复工程 → `rebuildAll()`
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
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    //==============================================================================
    /** 背景层：画布点阵、顶栏 / 状态栏、卡片和卡片标题。

        单独做成一个组件并 `setBufferedToImage (true)`：卡片的投影是模糊运算，
        而界面每 50ms 会刷一次文字——不缓存的话每次刷新都要把背后的投影重算一遍。
        这一层只在窗口尺寸变化时才真的重画。 */
    class Backdrop final : public juce::Component
    {
    public:
        struct Card
        {
            juce::Rectangle<int> bounds;
            juce::String         title;
            juce::Colour         colour;
        };

        Backdrop() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }

        void setLayout (juce::Rectangle<int> header, juce::Rectangle<int> status,
                        juce::Rectangle<int> chainLabel, std::vector<Card> cards);
        void paint (juce::Graphics&) override;

    private:
        juce::Rectangle<int> header, status, chainLabel;
        std::vector<Card>    cards;
    };

    //==============================================================================
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void rebuildAll();
    void updateStatus();
    void refreshSnapshotTiles();
    void refreshChainUI();
    void refreshHistoryUI();
    void syncFromParameters();
    void syncMotionUI();
    void rebuildPresetList();
    juce::String buildStatusText();

    /** 状态栏上那句"刚刚发生了什么"。新消息会高亮几秒再淡回普通颜色。 */
    void setNote (const juce::String&);

    void showPluginMenu (int chainSlot, juce::Component* anchor);
    void showModuleMenu (int chainSlot);
    void showScanWindow();
    void closeScanWindow();

    /** 插件功能标签窗口。它是"随机效果器链"的数据录入口，见 PluginTagView。 */
    void showTagWindow();
    void closeTagWindow();

    void loadDescription (int chainSlot, const juce::PluginDescription&);
    void toggleHostedWindow();
    void openHostedWindow (int chainSlot);

    /** 关掉某一位的浮动窗口；-1 = 全关。processor 换插件前会调它。 */
    void closeHostedWindows (int chainSlot);
    void doUnload (int chainSlot);

    void doRoll();
    void doHistory (bool back);

    void applyFilter();
    void confirmProbe();
    void startProbe();
    void offerUncheckInert (int numInert);
    void showListMenu();

    void pushMotionSettings();
    void promptSaveChainPreset();
    void showChainPresetMenu();

    void wireKnob (juce::Slider&, juce::AudioParameterFloat&, const juce::String& label, juce::Colour);
    void wireRange();

    //==============================================================================
    ParamRouletteProcessor& proc;

    // 第一个成员：这样它比所有子组件晚析构。dtor 里还会再 setLookAndFeel(nullptr) 一道保险。
    GridLookAndFeel lnf;

    /** 没有这个实例的话 `setTooltip` 是死代码——JUCE 靠一个 TooltipWindow
        去轮询鼠标底下的组件并画气泡，光设文字不会有任何显示。 */
    RoundTooltipWindow tooltips { this, 650 };

    Backdrop backdrop;

    //== 顶栏 ======================================================================
    /** 显示当前链位的插件名，点开是插件菜单。取代上一版的「选择插件…」——
        名字本身就是按钮，既告诉你现在是谁、又是换插件的入口。 */
    juce::TextButton bPlugin;
    juce::ComboBox   cbPreset;
    juce::TextButton bUI     { "界面"_u8 };
    juce::TextButton bUnload { "卸载"_u8 };
    juce::TextButton bScan   { "扫描"_u8 };
    juce::TextButton bTags   { "标签"_u8 };

    //== 信号链 ====================================================================
    ChainStrip       chain;
    juce::TextButton bChainSave { "存链"_u8 };
    juce::TextButton bChainLoad { "读链"_u8 };

    //== 随机化 ====================================================================
    juce::Slider     kAmount;
    juce::Slider     sRange;            ///< 双滑块：下限 / 上限
    Stepper          stepMax;           ///< 每次只动 N 个
    juce::TextButton bProbe { "体检"_u8 };
    juce::TextButton bRoll;
    juce::TextButton bHistBack, bHistFwd;
    juce::Label      lHistory;
    juce::Label      lResult;

    //== 运动语义 ==================================================================
    juce::ToggleButton tbMotion;
    SegmentedControl   segSemantic;
    juce::Label        lSemantic;
    juce::Slider       kMotion, kTone;
    juce::TextButton   bLockBase    { "设为基座"_u8 };
    juce::TextButton   bRestoreBase { "还原基座"_u8 };
    juce::Label        lBase;
    juce::Label        lProfile;

    //== 参数 ======================================================================
    juce::Label      lCount;
    juce::TextEditor teFilter;
    juce::TextButton bOnlyMatching { "仅匹配"_u8 };
    SegmentedControl segView;           ///< 全部 / 仅勾选 / 仅改动
    juce::TextButton bCheckMatch   { "勾选匹配"_u8 };
    juce::TextButton bUncheckMatch { "取消匹配"_u8 };
    juce::TextButton bListMenu;
    ParamListView    list;

    //== 快照 / morph ==============================================================
    SegmentedControl segMorphMode;      ///< 1D / 2D
    SnapshotTiles    tiles;
    MorphPad         pad;

    //== 状态栏 ====================================================================
    juce::Label lStatus;
    juce::Label lHints;

    //==============================================================================
    std::array<std::unique_ptr<HostedPluginWindow>, kMaxChain> hostedWindows;
    std::unique_ptr<juce::DocumentWindow> scanWindow;
    std::unique_ptr<juce::DocumentWindow> tagWindow;

    int          lastGeneration = -1;
    bool         syncing = false;   ///< 参数 → 界面回写中，别再反向推回去
    juce::String note;              ///< 最近一次动作的说明（加载失败 / 体检结论 / 存了哪个快照）
    juce::uint32 noteTime = 0;      ///< note 写入的时刻，用来做"新消息高亮几秒"

    /** NaN 警告的显示截止时间（毫秒计数）。
        latch 是读一次就清的，所以必须自己把它撑住几秒——否则那行字会在
        20 Hz 的刷新里闪一帧就没了，用户根本来不及看见。 */
    juce::uint32 guardWarnUntil = 0;

    static constexpr int kHeaderH = 52;
    static constexpr int kChainH  = 124;
    static constexpr int kStatusH = 30;
    static constexpr int kLeftW   = 304;
    static constexpr int kRightW  = 276;
    static constexpr int kRandomH = 222;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRouletteEditor)
};

} // namespace pr
