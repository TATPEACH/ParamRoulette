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
    void doUnload();

    void applyFilter();
    void confirmProbe();
    void startProbe();
    void offerUncheckInert (int numInert);

    void pushMotionSettings();
    void syncMotionUI();
    void rebuildPresetList();

    void wireSlider (juce::Slider&, juce::AudioParameterFloat&);

    //==============================================================================
    ParamRouletteProcessor& proc;

    // 第一个成员：这样它比所有子组件晚析构。dtor 里还会再 setLookAndFeel(nullptr) 一道保险。
    juce::LookAndFeel_V4 lnf;

    juce::TextButton bPick   { "选择插件…"_u8 };
    juce::TextButton bScan   { "扫描…"_u8 };
    juce::TextButton bUI     { "界面"_u8 };
    juce::TextButton bUnload { "卸载"_u8 };
    juce::Label      lPlugin;

    /** 子插件的 preset 列表。轮廓改的是当前 program 的那条记录，
        所以这个选择器不是锦上添花——没它就只能在 program 0 上转圈。 */
    juce::ComboBox   cbPreset;

    juce::Label  lAmount { {}, "强度"_u8 }, lLo { {}, "下限"_u8 }, lHi { {}, "上限"_u8 };
    juce::Slider sAmount, sLo, sHi;

    juce::TextButton bRandom { "随机化"_u8 };
    juce::TextButton bProbe  { "体检"_u8 };
    juce::Label      lResult;
    juce::ComboBox   cbMorphMode;

    // 运动模式一行。开着的时候「随机化」走 MotionEngine 而不是逐参数掷。
    juce::ToggleButton tbMotion { "运动模式"_u8 };
    juce::ComboBox     cbSemantic;
    juce::Label        lMotionAmt { {}, "运动"_u8 }, lToneAmt { {}, "音色"_u8 };
    juce::Slider       sMotionAmt, sToneAmt;
    juce::TextButton   bLockBase    { "锁定基座"_u8 };
    juce::TextButton   bRestoreBase { "回到基座"_u8 };
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

    std::unique_ptr<HostedPluginWindow>  hostedWindow;
    std::unique_ptr<juce::DocumentWindow> scanWindow;

    int          lastGeneration = -1;
    bool         syncing = false;   ///< 参数 → 界面回写中，别再反向推回去
    juce::String note;              ///< 最近一次动作的说明（加载失败 / 体检结论 / 存了哪个 slot）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamRouletteEditor)
};

} // namespace pr
