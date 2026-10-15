#pragma once

#include "HwTheme.h"
#include "GridLookAndFeel.h"   // ui::variant / icon / tint 这几个属性两套皮肤共用

namespace pr
{

/** 硬件皮肤的 LookAndFeel：所有 JUCE 自带控件换成实体设备的样子——
    按键是有厚度的橡胶键帽（按下去会沉 1px）、带 LED 的自锁键、下拉框和插件名是
    荧光屏窗口、菜单是深色金属面板、提示是一张米白纸条。

    控件变体沿用 Grid 皮肤那套组件属性（`ui::variant` / `ui::icon` / `ui::tint`），
    再加几个硬件专用的（`hwui::` 下面）。所以同一行 `ui::variant (b, primary)`
    在两套皮肤里都有意义：Grid 是实心强调色胶囊，硬件是一颗会发光的大键。 */
class HardwareLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel();

    //== 按键 ======================================================================
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool highlighted, bool down) override;

    //== 下拉 / 菜单 ===============================================================
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawComboBoxTextWhenNothingSelected (juce::Graphics&, juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height,
                                             const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area,
                                       bool isHighlighted, const juce::PopupMenu::Item&,
                                       const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>& area,
                                                const juce::String& sectionName,
                                                const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                               int standardMenuItemHeight, int& idealWidth,
                                               int& idealHeight, const juce::PopupMenu::Options&) override;
    int  getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override;

    //== 文本 ======================================================================
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    //== 旋钮 ======================================================================
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    //== 杂项 ======================================================================
    int  getDefaultScrollbarWidth() override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    juce::AlertWindow* createAlertWindow (const juce::String& title, const juce::String& message,
                                          const juce::String& button1, const juce::String& button2,
                                          const juce::String& button3, juce::MessageBoxIconType,
                                          int numButtons, juce::Component* associatedComponent) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea,
                       juce::TextLayout&) override;
    int  getAlertWindowButtonHeight() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name,
                                int columnId, int width, int height, bool over, bool down,
                                int columnFlags) override;

    void drawCornerResizer (juce::Graphics&, int w, int h, bool over, bool dragging) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardwareLookAndFeel)
};

//==============================================================================
/** 硬件皮肤专用的控件属性。 */
namespace hwui
{
    /** 旋钮帽的样式（红指针 / 黄指针 / 灰滚花……）。 */
    void knob (juce::Slider&, hw::Knob);
    hw::Knob knobOf (const juce::Slider&);

    /** 旋钮外圈的数字刻度（"0,1,2,…,10"）。不设就只有一圈点。 */
    void scale (juce::Slider&, const juce::StringArray& labels);

    /** 按键上带一颗 LED，自锁（toggle 为真）时点亮。颜色默认琥珀。 */
    void led (juce::Button&, juce::Colour);

    /** 这个按钮画成一块荧光屏（顶栏的插件名就是）。 */
    void display (juce::Button&);
}

} // namespace pr
