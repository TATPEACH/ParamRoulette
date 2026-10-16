#pragma once

#include "RackTheme.h"
#include "Widgets.h"
#include "HwWidgets.h"   // HwMemoryBank：快照键的"先按写入再按数字"手势

namespace pr
{

/** Rack 的旋钮。手感照着 VCV Rack 2（`Knob.cpp`）：

      - **上下拖**（只认竖直方向），按下时**光标藏起来**，拖多远都不会撞屏幕边
      - **Ctrl 微调**（1/10）、**Shift 粗调**（×4）、Ctrl+Shift 再细（1/100）——Rack 的 getModSpeed
      - **双击**回到默认值，**右键**弹参数菜单（输入数值 / 初始化）
      - 悬停或拖动时在旋钮上方冒一个气泡显示「名字：数值」——Rack 面板上不印数值，靠这个看

    光影固定，只有盖子上的指示线转（`rk::paintKnob`）。 */
class RackKnob final : public juce::Slider
{
public:
    explicit RackKnob (rk::Knob style = rk::Knob::round);

    void setStyle (rk::Knob s)    { style = s; repaint(); }
    void setZoom (float z)        { zoom = z; repaint(); }

    /** 气泡和右键菜单里显示的名字。 */
    juce::String label;

    /** 悬停 / 拖动 → 显示气泡；离开 → 收起。editor 负责画那个气泡。 */
    std::function<void (RackKnob&, bool show)> onTip;

    /** 右键。editor 弹参数菜单。 */
    std::function<void (RackKnob&)> onMenu;

    juce::String tipText()        { return label + juce::String ("："_u8) + getTextFromValue (getValue()); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void valueChanged() override;

    rk::Knob style;
    float zoom = 1.0f;
    bool  dragging = false;
    float proportion = 0.0f;
    float lastY = 0.0f;
};

//==============================================================================
/** 面板上的按键。两种：TL1105（黑色小圆键）和 VCVBezel（金属圈里一颗能亮的灯）。
    灯的亮度由外面设（`setLight`），按键本身只管按下 / 弹起和点击回调。 */
class RackButton final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    enum class Kind { tl1105, bezel };

    RackButton (Kind, juce::Colour lightColour = rk::green, float diameterAt100 = 0.0f);

    void setZoom (float z)              { zoom = z; repaint(); }
    void setLight (float b);
    void setLightColour (juce::Colour c) { lightColour = c; repaint(); }

    std::function<void()> onClick;
    std::function<void()> onMenu;       ///< 右键（可选）

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void enablementChanged() override                   { repaint(); }

private:
    Kind kind;
    juce::Colour lightColour;
    float diameter;
    float zoom = 1.0f;
    float light = 0.0f;
    bool  pressed = false;
};

//==============================================================================
/** CKSS 拨动开关（竖的，2 档或 3 档）。点一下拨到下一档（3 档的到底了绕回去），
    也可以在上面上下拖。档位 0 = 最上面。 */
class RackSwitch final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    explicit RackSwitch (int positions = 2);

    void setZoom (float z)  { zoom = z; repaint(); }
    void setPosition (int p, juce::NotificationType = juce::dontSendNotification);
    int  getPosition() const noexcept { return position; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void enablementChanged() override                   { repaint(); }

private:
    int positions, position = 0;
    float zoom = 1.0f;
    float dragStartY = 0.0f;
    int   dragStartPos = 0;
};

//==============================================================================
/** 一组带指示灯的选项（Eurorack 上常见的"模式 + 一排灯"）。行为全是 SegmentedControl 的，
    只换布局（`columns` 列的网格）和画法（亮着灯的就是当前选项）。 */
class RackLedRadio final : public SegmentedControl
{
public:
    explicit RackLedRadio (int columns = 1) : cols (juce::jmax (1, columns)) {}

    void setZoom (float z)            { zoom = z; repaint(); }
    void setLightColour (juce::Colour c) { lightColour = c; repaint(); }

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> segment (int i) const override;

    int cols;
    float zoom = 1.0f;
    juce::Colour lightColour = rk::yellow;
};

//==============================================================================
/** LED 屏上的一段字（黄字，Rack 的 LedDisplay）。多行用 \n 分开，每行各自省略号截断。
    屏幕是发光体，房间调暗了它照样亮。 */
class RackLcd final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    RackLcd();

    void setZoom (float z)                       { zoom = z; repaint(); }
    void setText (const juce::String& t)         { if (t != text) { text = t; repaint(); } }
    void setColour (juce::Colour c)              { if (c != colour) { colour = c; repaint(); } }
    void setTextSize (float px)                  { size = px; repaint(); }
    void setJustification (juce::Justification j) { just = j; repaint(); }
    const juce::String& getText() const noexcept { return text; }

    void paint (juce::Graphics&) override;

private:
    juce::String text;
    juce::Colour colour = rk::yellow;
    juce::Justification just = juce::Justification::centredLeft;
    float zoom = 1.0f, size = 11.0f;
};

//==============================================================================
/** Rack 窗口顶上那条菜单栏：左边一排文字菜单，右边是状态文字（Rack 在那里显示帧率和 CPU）。 */
class RackMenuBar final : public juce::Component
{
public:
    struct Item
    {
        juce::String title;
        std::function<void (juce::PopupMenu&)> build;
    };

    void setItems (std::vector<Item>);
    void setStatus (const juce::String& text, juce::Colour);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** 打开第 i 个菜单（键盘 / 外部触发用）。 */
    void openMenu (int i);

private:
    juce::Rectangle<int> itemArea (int i) const;
    int itemAt (juce::Point<int>) const;

    std::vector<Item> items;
    juce::String status;
    juce::Colour statusColour = rk::ui (0.6f);
    int hover = -1, open = -1;
};

//==============================================================================
/** 菜单里的滑条（Rack 的「视图」菜单：线缆不透明度 / 张力、房间亮度……）。
    拖的时候菜单不关，值实时生效——拖着线缆张力就能看到线一点点绷直。 */
class RackMenuSlider final : public juce::PopupMenu::CustomComponent
{
public:
    RackMenuSlider (juce::String label, float value, float defaultValue,
                    std::function<void (float)> onChange);

    void getIdealSize (int& w, int& h) override  { w = 260; h = 28; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void setFromX (float x);

    juce::String label;
    float value, defaultValue;
    std::function<void (float)> onChange;
};

//==============================================================================
/** 参数气泡（Rack 的 ParamTooltip）：旋钮上方一块深色小牌子，写着「名字：数值」。 */
class RackTip final : public juce::Component
{
public:
    RackTip() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); }

    void show (const juce::String& text, juce::Rectangle<int> targetInParent);
    void paint (juce::Graphics&) override;

private:
    juce::String text;
};

//==============================================================================
/** 快照模块：竖排四颗灯键 + 最上面一颗「写入」键。手势、菜单、提示全部继承
    HwMemoryBank（先按写入、再按数字 = 覆盖；空键直接存；右键菜单），只换布局和画法。 */
class RackMemory final : public HwMemoryBank
{
public:
    void setZoom (float z) { zoom = z; repaint(); }
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> keyRect (int i) const override;
    float zoom = 1.0f;
};

} // namespace pr
