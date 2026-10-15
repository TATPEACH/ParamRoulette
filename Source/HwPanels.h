#pragma once

#include "HwTheme.h"
#include "ParamListView.h"
#include "ChainStrip.h"
#include "MorphPad.h"

namespace pr
{

/** 参数列表的荧光屏（VFD）版。交互、筛选、拖动涂抹勾选全是 `ParamListView` 的，
    这里只换画法：青绿色的荧光字、勾选是一颗"点亮的像素块"、当前值下面是一排 LED 段、
    整块屏幕盖一层很细的网格（真的 VFD 玻璃后面有一层金属网）和玻璃反光。

    列表每秒可能重画 20 次（morph 在推的时候），所以行里的字**不做**光晕——
    光晕要把每个字多画四遍，几百个格子乘上去就是实打实的 CPU。光晕只留给顶栏那几块屏。 */
class HwParamList final : public ParamListView
{
public:
    HwParamList();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void paintRow (juce::Graphics&, int displayRow, juce::Rectangle<int> area) override;
    void paintHeader (juce::Graphics&, juce::Rectangle<int> area) override;
};

//==============================================================================
/** 信号链的机架版：六块模块面板，模块之间用跳线串起来。

    每块模块：两颗小螺丝、链位号、选中灯、一条美纹纸（插件名用"记号笔"手写上去）、
    功能类别、「IN」键（灯亮 = 在工作，熄 = 旁通）、左下 IN / 右下 OUT 两个插孔。
    空位是一块空白挡板。跳线的颜色和 Grid 版的连线一样跟着**信号**走：经过一块在工作的
    模块就染成它的功能色。

    行为（单击聚焦、点空位装插件、双击开插件界面、右键菜单、旁通、开关安全网）全部
    继承自 `ChainStrip`，这里只换布局和画法。链尾的输出保护是一块带拨杆开关、GR 灯和
    NaN 灯的模块；天花板旋钮是个真控件，由 editor 摆在 `ceilingKnobArea()` 上。 */
class HwChainRack final : public ChainStrip
{
public:
    HwChainRack() = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** 输出保护模块上留给天花板旋钮的位置（本组件坐标）。 */
    juce::Rectangle<int> ceilingKnobArea() const;

private:
    juce::Rectangle<float> powerRect (juce::Rectangle<float> module) const override;

    juce::Point<float> inJack  (juce::Rectangle<float> m) const { return { m.getX() + 13.0f,     m.getBottom() - 14.0f }; }
    juce::Point<float> outJack (juce::Rectangle<float> m) const { return { m.getRight() - 13.0f, m.getBottom() - 14.0f }; }

    void paintRackModule (juce::Graphics&, int i);
    void paintGuardModule (juce::Graphics&);
    void paintPlate (juce::Graphics&, juce::Rectangle<float>, bool blank, bool isFocused, bool hover);
};

//==============================================================================
/** morph 控件的示波器版：绿色磷光屏、刻度网格、手柄是一个亮点，拖动时身后拖着余辉。

    坐标换算、拖动、双击回中、1D / 2D 的权重语义全部用 `MorphPad` 的
    （画出来的刻度必须和 `computeWeights` 一致，这条约定在基类那边）。 */
class HwMorphScope final : public MorphPad,
                           public juce::SettableTooltipClient,
                           private juce::Timer
{
public:
    explicit HwMorphScope (const SnapshotBank&);

    void paint (juce::Graphics&) override;
    void resized() override { trail.clear(); }

private:
    void timerCallback() override;

    void paintGraticule (juce::Graphics&, juce::Rectangle<float>);
    void paintScopeOneD (juce::Graphics&, juce::Rectangle<float>);
    void paintScopeTwoD (juce::Graphics&, juce::Rectangle<float>);
    void paintSpot (juce::Graphics&, juce::Point<float>, float radius, float intensity);
    void paintTrail (juce::Graphics&);
    void paintWeights (juce::Graphics&, juce::Rectangle<float> strip);
    juce::Point<float> handlePoint() const;

    /** 余辉：手柄最近走过的位置。只在位置真的变了时追加，静止时只剩当前点（不画），
        所以截图是确定的。 */
    struct TrailPoint { juce::Point<float> p; juce::uint32 t; };
    std::vector<TrailPoint> trail;
    MorphMode trailMode = MorphMode::OneD;    ///< 切 1D / 2D 时手柄会"跳"过去，不该拖出一道余辉

    static constexpr juce::uint32 kTrailMs = 420;
};

} // namespace pr
