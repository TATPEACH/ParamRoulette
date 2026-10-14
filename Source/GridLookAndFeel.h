#pragma once

#include "Theme.h"

namespace pr
{

/** 全插件唯一的 LookAndFeel。所有 JUCE 自带控件（按钮、下拉、菜单、输入框、
    滚动条、提示气泡、对话框、表格表头）都在这里换成 Grid 风格。

    控件的"变体"不靠子类，靠组件属性传进来——这样调用处一行就够：

        ui::variant (bRoll, ui::Variant::primary);
        ui::icon    (bRoll, theme::Icon::dice);
        ui::tint    (bRoll, theme::cyan);

    这种写法的好处是 JUCE 自己创建的按钮（AlertWindow 里的、PluginListComponent 里的）
    也会自动套上默认样式，不需要我们够得着它们的构造函数。
*/
class GridLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    GridLookAndFeel();

    //== 按钮 ======================================================================
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

    //== 滑块 ======================================================================
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int  getSliderThumbRadius (juce::Slider&) override;

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
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridLookAndFeel)
};

//==============================================================================
/** 设控件变体 / 图标 / 色调的小工具。见 GridLookAndFeel 的说明。 */
namespace ui
{
    enum class Variant
    {
        normal,     ///< 灰色胶囊
        primary,    ///< 实心强调色（随机化那个大按钮）
        ghost,      ///< 平时透明，悬停才出底色（工具栏按钮）
        chip        ///< 小胶囊，开 / 关两态（「仅匹配」这种）
    };

    void variant (juce::Component&, Variant);
    void icon    (juce::Component&, theme::Icon);
    void tint    (juce::Component&, juce::Colour);

    /** 开关样式的 ToggleButton（胶囊滑块），而不是默认的圆形勾。 */
    void asSwitch (juce::ToggleButton&);

    /** 文字靠左、图标挪到最右边（当下拉箭头用）。顶栏的插件选择器用这个。 */
    void alignLeft (juce::Component&);

    /** 文字前面画一个编号徽章（强调色圆点 + 数字）。n <= 0 不画。 */
    void badge (juce::Component&, int n);

    Variant      variantOf (const juce::Component&);
    theme::Icon  iconOf    (const juce::Component&);
    juce::Colour tintOf    (const juce::Component&, juce::Colour fallback = theme::accent);
}

/** 圆角提示气泡。JUCE 的 TooltipWindow 在构造函数里把自己设成不透明，
    圆角外面就会露出残影——作为 editor 的子组件时尤其明显。 */
class RoundTooltipWindow final : public juce::TooltipWindow
{
public:
    RoundTooltipWindow (juce::Component* parent, int delayMs)
        : juce::TooltipWindow (parent, delayMs)
    {
        setOpaque (false);
    }
};

} // namespace pr
