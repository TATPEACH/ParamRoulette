#pragma once

#include "EditorBase.h"
#include "RackLookAndFeel.h"
#include "RackWidgets.h"
#include "RackPanels.h"
#include "RackBrowser.h"

namespace pr
{

/** Rack 皮肤（C 版）：照着 VCV Rack 2 做的一台模块化机架。

        ┌ 菜单栏：插件 链 快照 视图 帮助 ··························· 状态文字 · ParamRoulette ┐
        ├ 第一排（信号链）：IN │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ LIMIT │ OUT    ← 跳线把它们串起来  ┤
        ├ 第二排：RANDOM │ MOTION │ PARAMETERS │ MEMORY │ MORPH                             ┤

    尺寸照 Rack：1 HP = 15 px、一排 3U = 380 px，两排一共 85 HP。窗口变大变小时整台机架
    一起缩放（Rack 的 zoom），默认窗口正好是 100%。

    **行为和另外两套皮肤完全一样**（同一个 `EditorBase`）。Rack 版多出来的、因为"机架"这个
    比喻而必须有的东西：
      - **拖模块调串联顺序**（`ParamRouletteProcessor::moveSlot`）；键盘是 Ctrl+← / Ctrl+→
      - 每个模块的信号灯、IN / OUT 的电平、MIDI 灯（`consumeMeterPeak`）
      - 模块浏览器（`RackBrowser`）取代插件菜单：搜索 + 厂牌 / 功能标签筛选
      - 「视图」菜单：深色面板、线缆不透明度 / 张力、房间亮度、光晕——Rack 的那一组
      - Rack 的快捷键：Enter 开浏览器、Ctrl+E 旁通；旋钮 Ctrl 微调 / Shift 粗调 / 右键输数值 */
class RackEditor final : public EditorBase
{
public:
    RackEditor (ParamRouletteProcessor&, EditorHost&);
    ~RackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

   #if PR_TEST_HOOKS
    /** 截图工具用：截图时没有音频，把信号灯摆成一个典型的样子。 */
    void setLevelsForTesting (float level);

    /** 截图工具用：打开模块浏览器。 */
    void openBrowserForTesting (int slot) { openBrowser (slot); }
   #endif

private:
    //==============================================================================
    /** 导轨层：整块机架区的 Rack 导轨 + 底色。缓存成图（导轨上的孔一画就是几百个）。 */
    class Backdrop final : public juce::Component
    {
    public:
        Backdrop() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
        float zoom = 1.0f;
        float rowTop = 0.0f;       ///< 第一排的上沿（本组件坐标），导轨按它对齐
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

    void choosePlugin (int chainSlot, juce::Component* anchor) override { juce::ignoreUnused (anchor); openBrowser (chainSlot); }
    void rolled() override;

    void openBrowser (int chainSlot);
    void moveFocused (int delta);
    void doMove (int from, int to);
    void showRackModuleMenu (int chainSlot);
    void showProgramMenu (int chainSlot);
    void updateChainExtras();
    void meterTick();

    void wireKnob (RackKnob&, juce::AudioParameterFloat&, const juce::String& label);
    void wireRangeKnobs();
    void showKnobMenu (RackKnob&);
    void editKnobValue (RackKnob&);
    void viewChanged (bool cablesOnly);
    juce::File viewFile() const;

    void buildMenus();

    //==============================================================================
    RoundTooltipWindow tooltips { this, 650 };

    RackMenuBar  menuBar;
    Backdrop     backdrop;
    RackChainRow chain;
    RackKnob     kCeiling { rk::Knob::round };

    RackModule mRandom { "RANDOM" }, mMotion { "MOTION" }, mParams { "PARAMETERS" },
               mMemory { "MEMORY" }, mMorph { "MORPH" };

    //== RANDOM ===================================================================
    RackKnob   kAmount { rk::Knob::roundHuge }, kLo { rk::Knob::round }, kHi { rk::Knob::round }, kMax { rk::Knob::trimpot };
    RackLcd    lMax, lHistory, lResult;
    RackButton bProbe { RackButton::Kind::tl1105 }, bHistBack { RackButton::Kind::tl1105 },
               bHistFwd { RackButton::Kind::tl1105 }, bRoll { RackButton::Kind::bezel, rk::red, 34.0f };
    int ledProbe = -1, lblRoll = -1;

    //== MOTION ===================================================================
    RackSwitch   swMotion { 2 };
    RackLedRadio selSemantic { 2 };
    RackKnob     kMotion { rk::Knob::daviesWhite }, kTone { rk::Knob::daviesWhite };
    RackButton   bLock { RackButton::Kind::tl1105 }, bRestore { RackButton::Kind::tl1105 };
    RackLcd      lSemantic, lProfile;
    int ledMotion = -1, lblLock = -1;

    //== PARAMETERS ===============================================================
    juce::TextEditor teFilter;
    RackButton       bOnly { RackButton::Kind::tl1105 }, bCheck { RackButton::Kind::tl1105 },
                     bUncheck { RackButton::Kind::tl1105 }, bListMenu { RackButton::Kind::tl1105 };
    RackLedRadio     segView { 3 };
    RackParamList    list;
    bool onlyMatching = false;
    int  ledOnly = -1, lblCount = -1;

    //== MEMORY / MORPH ===========================================================
    RackMemory memory;
    RackSwitch swMorph { 2 };
    RackLcd    lWeights;
    RackScope  scope;

    //== 覆盖层 ====================================================================
    RackCableLayer   cables { chain };
    RackTip          tip;
    juce::TextEditor valueEditor;          ///< 右键 → 输入数值
    RackKnob*        editingKnob = nullptr;
    RackBrowser      browser;

    juce::TimedCallback meterTimer;
    std::array<float, (size_t) ParamRouletteProcessor::numMeters> meters {};
    float midiLight = 0.0f;
    float plugSignature = -1.0f;           ///< 插头灯亮度的指纹：没变就不重画
    juce::uint32 rollFlashAt = 0;
    juce::uint32 viewDirtyAt = 0;
    bool  levelsFrozen = false;            ///< 截图时用：别让定时器把摆好的灯清掉

    float zoom = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackEditor)
};

} // namespace pr
