#pragma once

#include "RackTheme.h"
#include "GridLookAndFeel.h"   // ui::variant / icon / tint 这几个属性所有皮肤共用

namespace pr
{

/** Rack 皮肤的 LookAndFeel：JUCE 自带控件换成 VCV Rack 2 暗色 UI 的样子——
    #202020 的菜单（选中项是浅一级的灰、勾在右边）、深色的提示气泡、
    浏览器里那种**浅灰底深色字**的搜索框（Rack 暗色主题的文本框就是这样）、
    圆角深灰的按钮。

    面板上的元件（旋钮、灯键、拨动开关、插孔）不走这里，它们自己画——
    那些要跟着"房间亮度"变暗，而且样式比 LookAndFeel 的接口能表达的多。

    给 TextEditor 设属性 `rkLcd` = 画成 LED 屏（面板上的筛选框）。 */
class RackLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RackLookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

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

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

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
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackLookAndFeel)
};

} // namespace pr
