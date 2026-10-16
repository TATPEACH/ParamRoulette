#pragma once

#include "HwTheme.h"
#include "Widgets.h"

namespace pr
{

/** 硬件旋钮。外观由 HardwareLookAndFeel 画（样式见 `hwui::knob`），这里只改**手感**：

      - **相对拖动**：按下去不会跳到鼠标位置，从当前值开始走。上 / 右 = 加
      - **拖动时隐藏光标**，鼠标可以一直拖不撞屏幕边——实体旋钮没有"拧到头了光标还在动"
      - **Shift 微调**：同样的手移动距离，变化量是 1/5
      - **双击 / Ctrl+点 / Alt+点 复位**、**滚轮**一格一格调

    JUCE 自带的旋钮拖动是"绝对"的：值 = 按下时的值 + 拖动距离 / 灵敏度，中途按 Shift 改
    灵敏度会让值整段跳一下。所以拖动自己重写，每帧只累加这一帧的位移。 */
class HwKnob final : public juce::Slider
{
public:
    HwKnob();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** 满量程要拖多少像素（Shift 下 ×5）。 */
    void setPixelsPerRange (float px) noexcept { pixelsPerRange = px; }

private:
    bool  dragging = false;
    float proportion = 0.0f;
    juce::Point<float> last;
    float pixelsPerRange = 260.0f;
};

//==============================================================================
/** 拨杆开关（带一颗状态 LED）。点一下拨上 / 拨下，拨杆有 ~90ms 的翻转动画。

    外观自己画（不走 LookAndFeel）：它是硬件皮肤独有的控件，没有 Grid 对应物。 */
class HwBatToggle final : public juce::ToggleButton,
                          private juce::Timer
{
public:
    HwBatToggle();

    void setLedColour (juce::Colour c) { led = c; repaint(); }

    /** 拨杆上下两端印的字（默认「开 / 关」）。 */
    void setLegends (const juce::String& onText, const juce::String& offText);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    void timerCallback() override;

    float anim = 0.0f;          ///< 0 = 拨下，1 = 拨上
    bool  animInit = false;
    juce::Colour led { hw::ledCyan };
    juce::String onLegend, offLegend;
};

//==============================================================================
/** 指针式 VU 表。

    **弹道照着 VU 表的标准来**：0 VU 的阶跃大约 300ms 到位、过冲 1~1.5%。用一个二阶系统
    （ωn ≈ 13 rad/s、ζ ≈ 0.78）在界面线程上积分，比"数值低通一下"更像真的指针——
    指针有惯性，冲过头一点再回来。

    刻度位置也按真的 VU 表：偏转量 ∝ 电压（不是 dB），所以 −20 挤在左边、0 VU 在 71% 处、
    +3 在满偏。校准：0 VU = −18 dBFS RMS（数字设备最常见的对齐电平）。

    表盘（刻度、字、红区）只在尺寸变化时画一次进缓存，每帧只画指针。 */
class HwVuMeter final : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::Timer
{
public:
    /** 每帧问一次电平：RMS 和峰值，线性幅度。 */
    using LevelSource = std::function<void (float& rms, float& peak)>;

    explicit HwVuMeter (LevelSource);

    void setCaption (const juce::String&);

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

    /** 截图工具用：直接把指针放到某个读数上（跳过弹道）。 */
    void setNeedleForTesting (float deflection) { needle = deflection; velocity = 0.0f; repaint(); }

    static float deflectionForVu (float vu);   ///< VU 读数 → 0..1 的偏转

private:
    void timerCallback() override;
    juce::Rectangle<float> faceArea() const;
    juce::Point<float> pivot() const;
    float needleAngle (float deflection) const;

    LevelSource source;
    juce::String caption { "VU" };

    float needle = 0.0f, velocity = 0.0f;
    juce::uint32 lastTick = 0, peakUntil = 0;
    bool peakLit = false;

    std::unique_ptr<juce::Component> face;     ///< 表盘（缓存成图的子组件）

    static constexpr float kSweep = 0.78f;      ///< 指针半边扫角（弧度，约 45°）
};

//==============================================================================
/** 两位七段数码管 + ▲▼ 两颗小键。「每次只动 N 个」用它。

    0 显示成「--」（不限）——"00"会被读成"一个都不动"。
    交互：▲▼ 点按、滚轮、在数码管上上下拖、双击数码管回到「--」。 */
class HwDigitStepper final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    HwDigitStepper();

    void setRange (int lo, int hi)  { minV = lo; maxV = hi; setValue (value); }
    void setValue (int v, juce::NotificationType = juce::dontSendNotification);
    int  getValue() const noexcept  { return value; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> displayArea() const;
    juce::Rectangle<float> upArea() const;
    juce::Rectangle<float> downArea() const;
    void step (int delta);

    int minV = 0, maxV = 64, value = 0;
    int hover = 0, pressed = 0;     ///< +1 = ▲，-1 = ▼
    float wheelAcc = 0.0f;
    bool  dragDigits = false;
    float dragAcc = 0.0f, lastY = 0.0f;
};

//==============================================================================
/** 档位旋钮：一颗黑色鸡头旋钮，周围一圈印着各档的图形（运动语义那六条曲线）。

    交互：点某一档的图形直接拨过去；按住旋钮上下拖、或者滚轮，一格一档（有"咔哒"的
    吸附感：指针在两档之间有 ~70ms 的转动动画）。 */
class HwRotarySelector final : public juce::Component,
                               public juce::TooltipClient,
                               private juce::Timer
{
public:
    HwRotarySelector();

    void setItems (std::vector<SegmentedControl::Item>);
    void setSelected (int index, juce::NotificationType = juce::dontSendNotification);
    int  getSelected() const noexcept { return selected; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

private:
    void timerCallback() override;
    float detentAngle (int i) const;
    juce::Rectangle<float> glyphBox (int i) const;
    int  glyphAt (juce::Point<float>) const;
    bool overKnob (juce::Point<float>) const;
    void choose (int i);

    std::vector<SegmentedControl::Item> items;
    int   selected = 0, hover = -1;
    float shownAngle = 0.0f;
    bool  dragging = false;
    float dragAcc = 0.0f, wheelAcc = 0.0f;
    juce::Point<float> lastDrag;

    juce::Point<float> centre;
    float knobR = 20.0f, glyphR = 40.0f;
};

//==============================================================================
/** 四颗快照键 + 一颗「写入」键。硬件合成器存音色的那套手势：

      - **按数字键** = 召回（空键 = 直接存进去，和 Grid 版一样，省一步）
      - **按「写入」再按数字键** = 覆盖存进那一格。写入键的灯会闪，数字键的灯一起闪琥珀色，
        提醒"现在按下去是写"。6 秒内没选就自动取消；再按一次写入也取消
      - **右键数字键** = 菜单（召回 / 存入 / 清空）

    数字键上的灯：熄 = 空，暗 = 存了东西，全亮 = 当前声音就是它（召回过 / morph 权重最大）。
    键盘照旧：1–4 召回，Shift+1–4 存入。 */
class HwMemoryBank : public juce::Component,
                           public juce::TooltipClient,
                           private juce::Timer
{
public:
    HwMemoryBank();

    void setTiles (const std::array<SnapshotTiles::Tile, 4>&, int winner, bool canStore);

    std::function<void (int)> onRecall, onStore, onClear;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    bool isArmed() const noexcept { return armed; }

protected:
    // Rack 外观的快照模块是它的子类：同一套"先按写入再按数字"的手势，只换布局和画法。
    void timerCallback() override;
    void setArmed (bool);
    virtual juce::Rectangle<float> keyRect (int i) const;     ///< 0..3 = 数字键，4 = 写入键
    int  keyAt (juce::Point<float>) const;
    void showMenu (int i);

    std::array<SnapshotTiles::Tile, 4> tiles;
    int  winner = -1;
    bool canStore = false;
    bool armed = false;
    juce::uint32 armedAt = 0;
    int  hover = -1, pressed = -1;
};

//==============================================================================
/** 一块小荧光屏上的读数（状态行、历史 3/12、语义说明、基座、上次随机结果……）。

    和 juce::Label 的区别：字带一层很淡的光晕（VFD 的字是"发光"的，不是印上去的），
    支持多行（`\n` 分行，每行各自省略号截断）。屏幕外框由 editor 的背景层画。 */
class HwReadout final : public juce::Component
{
public:
    HwReadout() { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& t)                { if (t != text) { text = t; repaint(); } }
    void setColour (juce::Colour c)                     { if (c != colour) { colour = c; repaint(); } }
    void setFont (const juce::Font& f)                  { font = f; repaint(); }
    void setJustification (juce::Justification j)       { just = j; repaint(); }
    void setGlow (float g)                              { glow = g; repaint(); }
    const juce::String& getText() const noexcept        { return text; }

    void paint (juce::Graphics&) override;

private:
    juce::String text;
    juce::Colour colour { hw::vfd };
    juce::Font   font   { hw::font (12.0f) };
    juce::Justification just { juce::Justification::centredLeft };
    float glow = 0.35f;
};

//==============================================================================
/** 互锁按键组：一排方键，按下一颗、其余弹起，按下的那颗灯亮（老式录音机的功能键）。
    行为完全是 SegmentedControl 的，只换画法。 */
class HwInterlock final : public SegmentedControl
{
public:
    void paint (juce::Graphics&) override;
};

} // namespace pr
