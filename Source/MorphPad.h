#pragma once

#include "SnapshotBank.h"
#include "Theme.h"

namespace pr
{

/** 1D 滑条 / 2D pad 二合一的 morph 控件。

    **它只画、只报坐标，不算插值也不碰插件。** 真正的权重和写回都在
    `SnapshotBank` 里；这里画出来的刻度必须和 `computeWeights` 算的完全一致，
    否则用户看到的位置和听到的声音会对不上。两条硬约定：

      - **1D**：已用 slot 按**序号顺序**压实成一条线，第 i 个已用的落在
        `i / (N-1)`。所以只存了 slot 1 和 slot 3 时，线的左端是 1、右端是 3，
        中间**什么都没有**——刻度必须按 N 现算，不能钉死在四分位上。
      - **2D**：四角固定绑 slot 0..3，摆位 `0──1 / 2──3`，y 向下增长。

    位置本身不存在这里，存在 processor 的 `Morph X` / `Morph Y` 参数上
    （那才是能被 DAW 自动化的东西）。这个组件只是它的一个视图。
*/
class MorphPad : public juce::Component
{
public:
    explicit MorphPad (const SnapshotBank& bankRef);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void      setMode (MorphMode m);
    MorphMode getMode() const noexcept { return mode; }

    /** 外部同步位置（DAW 自动化、状态恢复、参数联动）。
        **不**触发 onPositionChanged——否则会和参数改动打成死循环。 */
    void setPosition (float x, float y);

    float getMorphX() const noexcept { return posX; }
    float getMorphY() const noexcept { return posY; }

    /** 拖动中不接受外部同步：用户的手比自动化优先。 */
    bool isDragging() const noexcept { return dragging; }

    //==============================================================================
    std::function<void()>                  onDragStart;    ///< beginChangeGesture
    std::function<void (float x, float y)> onPositionChanged;
    std::function<void()>                  onDragEnd;      ///< endChangeGesture

protected:
    // 子类（硬件版的示波器）只换 paint()，坐标换算 / 拖动 / 双击回中都用这里的。
    virtual juce::Rectangle<float> padArea() const;    ///< 子类可以换边距（Rack 的示波器没有底部读数条）
    void  positionFromMouse (const juce::MouseEvent&);
    void  paintOneD (juce::Graphics&, juce::Rectangle<float>);
    void  paintTwoD (juce::Graphics&, juce::Rectangle<float>);
    void  paintSlotDot (juce::Graphics&, juce::Point<float> centre, int slot, float radius);
    void  paintHandle (juce::Graphics&, juce::Point<float> centre);
    void  paintReadout (juce::Graphics&, juce::Rectangle<float> strip);
    juce::String hintText() const;

    const SnapshotBank& bank;

    MorphMode mode = MorphMode::OneD;
    float posX = 0.0f;
    float posY = 0.0f;
    bool  dragging = false;

    static constexpr int   kMargin    = 12;   ///< 给角标留的边距
    static constexpr int   kReadoutH  = 22;
    static constexpr float kHandleR   = 9.0f;
    static constexpr float kSlotR     = 9.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphPad)
};

} // namespace pr
