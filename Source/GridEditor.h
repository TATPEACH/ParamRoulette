#pragma once

#include "EditorBase.h"
#include "MorphPad.h"
#include "GridLookAndFeel.h"

namespace pr
{

/** Grid 皮肤。

    **视觉上照着 Bitwig 的 The Grid 来**：深色点阵画布上摆几块圆角模块，
    每块模块标题用分类色；信号链画成用连线串起来的模块。布局（1240×780 起）：

        ┌ 顶栏：品牌 · 当前链位的插件 / preset / 界面 / 卸载 · 外观 · 扫描 / 标签 ─────┐
        ├ 信号链：IN → 1…6 → 输出保护 → OUT ────────────────────────────── 存链 读链 ┤
        ├ 随机化 ┐ ┌ 参数 ─────────────────────────────────────┐ ┌ 快照 · Morph ┐
        │ 运动语义│ │                                             │ │  方块 + pad  │
        ├ 状态栏 ──────────────────────────────────────────────── 快捷键提示 ┤

    行为全部在 `EditorBase` 里，这里只有控件、布局和"控件 ↔ 状态"的同步。 */
class GridEditor final : public EditorBase
{
public:
    GridEditor (ParamRouletteProcessor&, EditorHost&);
    ~GridEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

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
    void rebuildSkin() override;
    void updateStatus() override;
    void syncFromParameters() override;
    void syncMotionUI() override;
    void refreshSnapshotTiles() override;
    void focusFilter() override;
    ParamListView& paramList() override { return list; }
    ChainStrip&    chainView() override { return chain; }

    void refreshHistoryUI();
    void wireKnob (juce::Slider&, juce::AudioParameterFloat&, const juce::String& label, juce::Colour);
    void wireRange();

    //==============================================================================
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
    juce::TextButton bSkin   { "硬件外观"_u8 };
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

    static constexpr int kHeaderH = 52;
    static constexpr int kChainH  = 124;
    static constexpr int kStatusH = 30;
    static constexpr int kLeftW   = 304;
    static constexpr int kRightW  = 276;
    static constexpr int kRandomH = 222;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridEditor)
};

} // namespace pr
