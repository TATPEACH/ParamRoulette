#pragma once

#include "EditorBase.h"
#include "HardwareLookAndFeel.h"
#include "HwWidgets.h"
#include "HwPanels.h"

namespace pr
{

/** 硬件拟物皮肤。

    整台"设备"装在一个原木框里，藏青烤漆面板上分三层（1280×820 起）：

        ┌ 商标 · [荧光屏：链位 / 插件名 ▾] [preset ▾] 界面 卸载 ······ Grid 外观 扫描 标签 ┐
        ├ 信号链：IN ═ [1][2][3][4][5][6] ═ [输出保护] ═ OUT     VU 表        存链 读链 ┤
        ├═══════════════════════════════════════════════════════════════════════ 接缝 ═┤
        │ 随机化（红色大旋钮 / 上下限 / 只动 N / 体检 / 掷）│ 参数（荧光屏列表）│ 快照 · Morph │
        │ 运动语义（拨杆 / 六档旋钮 / 运动 · 音色 / 基座）  │                   │ 数字键 + 示波器│
        ├ [荧光屏状态行 ························································] 快捷键 ┤

    **行为和 Grid 版完全一样**（同一个 `EditorBase`）。硬件版多出来的只有三样东西：
    VU 表（读链尾输出电平）、天花板旋钮（Grid 版只能在 DAW 自动化里改）、
    以及"按住写入键再选格子"的快照写入手势。 */
class HardwareEditor final : public EditorBase
{
public:
    HardwareEditor (ParamRouletteProcessor&, EditorHost&);
    ~HardwareEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

   #if PR_TEST_HOOKS
    /** 截图工具用：把 VU 指针定在某个读数上（截图里没有音频在跑）。 */
    void setVuForTesting (float reading) { vu.setNeedleForTesting (HwVuMeter::deflectionForVu (reading)); }
   #endif

private:
    //==============================================================================
    /** 静态的那一层：原木框、面板、接缝、螺丝、商标、分区丝印和铭牌、各块屏幕的凹框。
        缓存成图，只在尺寸变化时重画（木纹和漆面噪点都不便宜）。 */
    class Backdrop final : public juce::Component
    {
    public:
        struct Section
        {
            juce::Rectangle<int> bounds;
            juce::String         title, english;
        };

        struct Legend
        {
            juce::Rectangle<int> bounds;
            juce::String         text;
            bool                 latin = false;    ///< 纯 ASCII 用工程字体
            juce::Justification  just { juce::Justification::centred };
        };

        Backdrop() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }

        juce::Rectangle<int> faceplate, rackBay, logo;
        juce::Point<int>     rackTitle;
        std::vector<Section>                sections;
        std::vector<juce::Rectangle<int>>   screens;     ///< 荧光屏 / 示波器的凹框
        std::vector<Legend>                 legends;
        int seamAt = 0;                                  ///< 上下两块面板之间的接缝（y）

        void paint (juce::Graphics&) override;
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


    void wireKnob (HwKnob&, juce::AudioParameterFloat&, const juce::String& label, hw::Knob,
                   const juce::StringArray& scale = {});
    void wireRangeKnobs();
    void styleReadout (HwReadout&, float pt, juce::Colour, juce::Justification = juce::Justification::centredLeft);

    //==============================================================================
    RoundTooltipWindow tooltips { this, 650 };
    Backdrop backdrop;

    //== 顶栏 ======================================================================
    juce::TextButton bPlugin;           ///< 荧光屏：链位号 + 插件名，点开换插件
    juce::ComboBox   cbPreset;
    juce::TextButton bUI     { "界面"_u8 };
    juce::TextButton bUnload { "卸载"_u8 };
    juce::TextButton bSkin   { "外观"_u8 };
    juce::TextButton bScan   { "扫描"_u8 };
    juce::TextButton bTags   { "标签"_u8 };

    //== 信号链 ====================================================================
    HwChainRack      chain;
    HwKnob           kCeiling;          ///< 安全网的天花板（dBFS）
    HwVuMeter        vu;
    juce::TextButton bChainSave { "存链"_u8 };
    juce::TextButton bChainLoad { "读链"_u8 };

    //== 随机化 ====================================================================
    HwKnob           kAmount, kLo, kHi;
    HwDigitStepper   stepMax;
    juce::TextButton bProbe { "体检"_u8 };
    juce::TextButton bRoll;
    juce::TextButton bHistBack, bHistFwd;
    HwReadout        lHistory, lResult;

    //== 运动语义 ==================================================================
    HwBatToggle      tbMotion;
    HwRotarySelector selSemantic;
    HwReadout        lSemantic;
    HwKnob           kMotion, kTone;
    juce::TextButton bLockBase    { "设为基座"_u8 };
    juce::TextButton bRestoreBase { "还原基座"_u8 };
    HwReadout        lBase, lProfile;

    //== 参数 ======================================================================
    juce::TextEditor teFilter;
    juce::TextButton bOnlyMatching { "仅匹配"_u8 };
    HwInterlock      segView;
    juce::TextButton bCheckMatch   { "勾选匹配"_u8 };
    juce::TextButton bUncheckMatch { "取消匹配"_u8 };
    juce::TextButton bListMenu;
    HwParamList      list;

    //== 快照 / morph ==============================================================
    HwInterlock      segMorphMode;
    HwMemoryBank     memory;
    HwMorphScope     scope;

    //== 状态行 ====================================================================
    HwReadout        lStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareEditor)
};

} // namespace pr
