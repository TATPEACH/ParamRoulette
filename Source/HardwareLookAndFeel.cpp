#include "HardwareLookAndFeel.h"

namespace pr
{

using theme::Icon;
using theme::Weight;

//==============================================================================
namespace hwui
{
    static const juce::Identifier kKnob    { "hwKnob" };
    static const juce::Identifier kScale   { "hwScale" };
    static const juce::Identifier kLed     { "hwLed" };
    static const juce::Identifier kDisplay { "hwDisplay" };

    void knob (juce::Slider& s, hw::Knob k)  { s.getProperties().set (kKnob, (int) k); s.repaint(); }
    hw::Knob knobOf (const juce::Slider& s)  { return (hw::Knob) (int) s.getProperties().getWithDefault (kKnob, (int) hw::Knob::knurledGrey); }
    void scale (juce::Slider& s, const juce::StringArray& labels) { s.getProperties().set (kScale, labels.joinIntoString (",")); s.repaint(); }
    void led (juce::Button& b, juce::Colour c) { b.getProperties().set (kLed, (juce::int64) c.getARGB()); b.repaint(); }
    void display (juce::Button& b)           { b.getProperties().set (kDisplay, true); b.repaint(); }

    static juce::StringArray scaleOf (const juce::Slider& s)
    {
        const auto v = s.getProperties()[kScale].toString();
        return v.isEmpty() ? juce::StringArray() : juce::StringArray::fromTokens (v, ",", {});
    }

    static bool hasLed (const juce::Component& c)       { return c.getProperties().contains (kLed); }
    static juce::Colour ledOf (const juce::Component& c) { return juce::Colour ((juce::uint32) (juce::int64) c.getProperties()[kLed]); }
    static bool isDisplay (const juce::Component& c)    { return (bool) c.getProperties()[kDisplay]; }
}

namespace
{
    /** 键帽的几何：键槽里一块有厚度的帽子。按下时整块沉 1.5px、侧面变薄。 */
    struct Cap
    {
        juce::Rectangle<float> well, top;
        float radius, depth;
    };

    Cap capFor (const juce::Button& b, bool down)
    {
        const bool primary = ui::variantOf (b) == ui::Variant::primary;
        Cap c;
        c.radius = primary ? 6.0f : 4.0f;
        c.well   = b.getLocalBounds().toFloat().reduced (0.5f);
        c.depth  = down ? 1.0f : (primary ? 3.5f : 2.5f);
        c.top    = c.well.reduced (2.0f, 2.0f).withTrimmedBottom (c.depth).translated (0.0f, down ? 1.5f : 0.0f);
        return c;
    }

    juce::Colour capColour (const juce::Button& b)
    {
        const bool primary = ui::variantOf (b) == ui::Variant::primary;
        auto c = primary ? ui::tintOf (b, hw::capRed) : hw::rubber;

        if (! b.isEnabled())
            c = c.withMultipliedSaturation (0.25f).darker (primary ? 0.55f : 0.2f);

        return c;
    }

    juce::Rectangle<float> circle (juce::Point<float> c, float r) { return { c.x - r, c.y - r, r * 2.0f, r * 2.0f }; }

    using hw::isAscii;

    juce::Font labelFont (const juce::String& s, float pt, bool bold = true)
    {
        return isAscii (s) ? hw::legendFont (pt * 1.04f, bold) : hw::font (pt, bold ? Weight::bold : Weight::medium);
    }

    void drawDisplayButton (juce::Graphics& g, juce::TextButton& b, bool highlighted)
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        hw::drawRecess (g, r, 4.0f, hw::glass);

        auto inner = r.reduced (9.0f, 0.0f);
        const bool enabled = b.isEnabled();

        // 左边一个小方框写链位号（像老设备显示屏上的通道号）
        if (const int badge = (int) b.getProperties().getWithDefault ("prBadge", 0); badge > 0)
        {
            const auto box = inner.removeFromLeft (r.getHeight() - 12.0f).withSizeKeepingCentre (r.getHeight() - 14.0f, r.getHeight() - 14.0f);
            g.setColour (hw::vfdAmber.withAlpha (0.18f));
            g.fillRoundedRectangle (box, 2.0f);
            g.setColour (hw::vfdAmber.withAlpha (0.8f));
            g.drawRoundedRectangle (box, 2.0f, 1.0f);
            hw::drawGlowText (g, juce::String (badge), box, hw::vfdAmber, hw::monoFont (13.0f), juce::Justification::centred, false, 0.5f);
            inner.removeFromLeft (9.0f);
        }

        theme::drawIcon (g, Icon::chevronDown, inner.removeFromRight (12.0f).withSizeKeepingCentre (12.0f, 12.0f),
                         highlighted ? hw::vfd : hw::vfdDim, 1.6f);
        inner.removeFromRight (6.0f);

        const auto txt = b.getButtonText();
        hw::drawGlowText (g, txt, inner, enabled ? hw::vfd : hw::vfdDim,
                          isAscii (txt) ? hw::monoFont (14.5f) : hw::font (13.5f, Weight::medium),
                          juce::Justification::centredLeft, true, 0.45f);

        hw::drawGlassSheen (g, r.reduced (1.0f), 3.0f);
    }
}

//==============================================================================
HardwareLookAndFeel::HardwareLookAndFeel()
{
    using namespace hw;

    setDefaultSansSerifTypefaceName (theme::family());

    setColourScheme ({ panel,           // windowBackground
                       panel,           // widgetBackground
                       bay,             // menuBackground
                       seam,            // outline
                       legend,          // defaultText
                       rubber,          // defaultFill
                       juce::Colours::white, // highlightedText
                       panelHi,         // highlightedFill
                       legend });       // menuText

    setColour (juce::ResizableWindow::backgroundColourId, panel);
    setColour (juce::DocumentWindow::backgroundColourId,  panel);

    setColour (juce::TextButton::buttonColourId,   rubber);
    setColour (juce::TextButton::buttonOnColourId, rubber);
    setColour (juce::TextButton::textColourOffId,  legend);
    setColour (juce::TextButton::textColourOnId,   legend);

    setColour (juce::ToggleButton::textColourId,         legend);
    setColour (juce::ToggleButton::tickColourId,         ledAmber);
    setColour (juce::ToggleButton::tickDisabledColourId, legendFaint);

    setColour (juce::ComboBox::backgroundColourId,     glass);
    setColour (juce::ComboBox::textColourId,           vfd);
    setColour (juce::ComboBox::outlineColourId,        seam);
    setColour (juce::ComboBox::arrowColourId,          vfdDim);
    setColour (juce::ComboBox::focusedOutlineColourId, vfd);

    // 菜单底色带一点点透明：JUCE 只在底色不透明时把菜单窗口设成不透明，半透明才画得出圆角
    setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff141f2b).withAlpha (0.995f));
    setColour (juce::PopupMenu::textColourId,                  legend);
    setColour (juce::PopupMenu::headerTextColourId,            legendFaint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ledAmber.withAlpha (0.2f));
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    setColour (juce::TextEditor::backgroundColourId,        glass);
    setColour (juce::TextEditor::textColourId,              vfd);
    setColour (juce::TextEditor::highlightColourId,         vfd.withAlpha (0.28f));
    setColour (juce::TextEditor::highlightedTextColourId,   juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId,           juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId,    vfd.withAlpha (0.6f));
    setColour (juce::TextEditor::shadowColourId,            juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId,         vfd);

    setColour (juce::Label::textColourId,               legend);
    setColour (juce::Label::textWhenEditingColourId,    vfd);
    setColour (juce::Label::outlineWhenEditingColourId, vfd);

    setColour (juce::ScrollBar::thumbColourId, vfdDim);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::Slider::rotarySliderFillColourId,    ledAmber);
    setColour (juce::Slider::rotarySliderOutlineColourId, bay);
    setColour (juce::Slider::trackColourId,               ledAmber);
    setColour (juce::Slider::backgroundColourId,          bay);
    setColour (juce::Slider::thumbColourId,               capGrey);
    setColour (juce::Slider::textBoxTextColourId,         legend);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, plate);
    setColour (juce::TooltipWindow::textColourId,       plateInk);
    setColour (juce::TooltipWindow::outlineColourId,    juce::Colour (0xffb8b19c));

    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId,       legend);
    setColour (juce::AlertWindow::outlineColourId,    seam);

    setColour (juce::ListBox::backgroundColourId, glass);
    setColour (juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::ListBox::textColourId,       vfd);

    setColour (juce::TableHeaderComponent::textColourId,       legendDim);
    setColour (juce::TableHeaderComponent::backgroundColourId, bay);
    setColour (juce::TableHeaderComponent::outlineColourId,    seam);
    setColour (juce::TableHeaderComponent::highlightColourId,  panelHi);
}

//==============================================================================
juce::Font HardwareLookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
{
    const auto txt = b.getButtonText();

    if (ui::variantOf (b) == ui::Variant::primary)
        return labelFont (txt, buttonHeight >= 40 ? 15.0f : 13.0f);

    return labelFont (txt, buttonHeight < 26 ? 11.0f : 12.0f);
}

void HardwareLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                bool highlighted, bool down)
{
    if (hwui::isDisplay (b))
        return;     // 屏幕样式整个在 drawButtonText 里画

    const auto cap     = capFor (b, down);
    const bool primary = ui::variantOf (b) == ui::Variant::primary;
    const bool enabled = b.isEnabled();
    const auto base    = capColour (b);

    // 键槽：面板上一个比键帽大一圈的深色凹口
    g.setColour (hw::seam.withAlpha (0.95f));
    g.fillRoundedRectangle (cap.well, cap.radius + 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (cap.well.translated (0.0f, 0.6f), cap.radius + 1.5f, 0.8f);

    // 键帽侧面（下沿露出来的那一截厚度）
    g.setColour (base.darker (0.62f));
    g.fillRoundedRectangle (cap.top.withHeight (cap.top.getHeight() + cap.depth), cap.radius);

    // 顶面：上亮下暗（光在正上方），悬停时整体亮一点点
    const float lift = highlighted && enabled ? 0.1f : 0.0f;
    g.setGradientFill (juce::ColourGradient (base.brighter (0.16f + lift), 0.0f, cap.top.getY(),
                                             base.darker (0.14f - lift * 0.5f), 0.0f, cap.top.getBottom(), false));
    g.fillRoundedRectangle (cap.top, cap.radius);

    if (primary && enabled)
    {
        // 背光键：中间透出灯光
        const auto c = cap.top.getCentre();
        g.setGradientFill (juce::ColourGradient (base.brighter (0.9f).withAlpha (down ? 0.55f : 0.42f), c.x, c.y,
                                                 base.withAlpha (0.0f), c.x + cap.top.getWidth() * 0.55f, c.y, true));
        g.fillRoundedRectangle (cap.top, cap.radius);
    }

    // 上沿一道细高光
    g.setColour (juce::Colours::white.withAlpha (down ? 0.06f : 0.17f));
    g.fillRect (cap.top.reduced (cap.radius, 0.0f).withHeight (1.0f).translated (0.0f, 0.5f));
}

void HardwareLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool down)
{
    if (hwui::isDisplay (b))
    {
        drawDisplayButton (g, b, highlighted);
        return;
    }

    const auto cap     = capFor (b, down);
    const bool primary = ui::variantOf (b) == ui::Variant::primary;
    const bool enabled = b.isEnabled();
    const bool on      = b.getToggleState();

    auto r = cap.top.reduced (primary ? 12.0f : 8.0f, 0.0f);

    // 自锁键的 LED（左侧）
    if (hwui::hasLed (b))
    {
        const auto ledArea = r.removeFromLeft (10.0f);
        hw::drawLed (g, ledArea.getCentre(), 3.0f, hwui::ledOf (b), on && enabled ? 1.0f : 0.0f);
        r.removeFromLeft (5.0f);
    }

    const juce::Colour tc = primary ? juce::Colours::white.withAlpha (enabled ? 0.95f : 0.35f)
                                    : (enabled ? (highlighted || on ? hw::legend.brighter (0.3f) : hw::legend)
                                               : hw::legendFaint.withAlpha (0.7f));

    const auto txt  = b.getButtonText();
    const auto f    = getTextButtonFont (b, b.getHeight());

    // 带灯的键不画图标：灯本身就是这颗键的标识，再塞一个图标，字就被挤成省略号了
    const auto ic   = hwui::hasLed (b) && txt.isNotEmpty() ? Icon::none : ui::iconOf (b);
    const float is  = juce::jlimit (10.0f, 16.0f, cap.top.getHeight() - 12.0f);

    // 字印在键帽上：一道往下 1px 的暗影，像丝印的厚度
    auto drawTextShadowed = [&] (juce::Rectangle<float> area, juce::Justification j)
    {
        g.setFont (f);
        g.setColour (juce::Colours::black.withAlpha (enabled ? 0.45f : 0.2f));
        g.drawText (txt, area.translated (0.0f, 1.0f), j, true);
        g.setColour (tc);
        g.drawText (txt, area, j, true);
    };

    if (ic != Icon::none)
    {
        if (txt.isEmpty())
        {
            theme::drawIcon (g, ic, r.withSizeKeepingCentre (is, is), tc, 1.7f);
            return;
        }

        const float tw    = juce::GlyphArrangement::getStringWidth (f, txt);
        const float total = is + 6.0f + tw;
        const float x0    = juce::jmax (r.getX(), r.getCentreX() - total * 0.5f);

        theme::drawIcon (g, ic, { x0, r.getCentreY() - is * 0.5f, is, is }, tc, 1.7f);
        drawTextShadowed ({ x0 + is + 6.0f, r.getY(), juce::jmax (0.0f, r.getRight() - (x0 + is + 6.0f)), r.getHeight() },
                          juce::Justification::centredLeft);
        return;
    }

    drawTextShadowed (r, juce::Justification::centred);
}

void HardwareLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    // 普通 ToggleButton（标签窗口的「只看未分类」这种）：一颗带 LED 的小方键 + 右边的丝印字。
    // 运动模式那个拨杆开关是 HwBatToggle，自己画。
    auto r = b.getLocalBounds().toFloat();
    const float d = juce::jmin (18.0f, r.getHeight() - 4.0f);

    drawTickBox (g, b, r.getX() + 1.0f, r.getCentreY() - d * 0.5f, d, d,
                 b.getToggleState(), b.isEnabled(), highlighted, down);

    if (b.getButtonText().isNotEmpty())
    {
        g.setFont (labelFont (b.getButtonText(), 12.0f, false));
        g.setColour (b.isEnabled() ? hw::legend : hw::legendFaint);
        g.drawText (b.getButtonText(), r.withLeft (r.getX() + d + 9.0f), juce::Justification::centredLeft, true);
    }
}

void HardwareLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h,
                                       bool ticked, bool isEnabled, bool highlighted, bool down)
{
    const auto box = juce::Rectangle<float> (x, y, w, h);

    g.setColour (hw::seam);
    g.fillRoundedRectangle (box, 3.0f);
    const auto top = box.reduced (2.0f).translated (0.0f, down ? 0.8f : 0.0f);
    g.setGradientFill (juce::ColourGradient (hw::rubber.brighter (highlighted ? 0.25f : 0.15f), 0.0f, top.getY(),
                                             hw::rubber.darker (0.2f), 0.0f, top.getBottom(), false));
    g.fillRoundedRectangle (top, 2.5f);

    hw::drawLed (g, top.getCentre(), juce::jmax (2.0f, w * 0.17f), hw::ledAmber, ticked && isEnabled ? 1.0f : 0.0f);
}

//==============================================================================
void HardwareLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                        int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    hw::drawRecess (g, r, 4.0f, hw::glass);

    if (box.hasKeyboardFocus (true))
    {
        g.setColour (hw::vfd.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (1.5f), 3.0f, 1.0f);
    }

    theme::drawIcon (g, Icon::chevronDown,
                     juce::Rectangle<float> ((float) width - 24.0f, (float) height * 0.5f - 6.0f, 12.0f, 12.0f),
                     box.isEnabled() ? (box.isMouseOver (true) ? hw::vfd : hw::vfdDim) : hw::vfdFaint, 1.6f);

    hw::drawGlassSheen (g, r.reduced (1.0f), 3.0f);
}

juce::Font HardwareLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return hw::font (12.5f, Weight::medium);
}

void HardwareLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (11, 1, juce::jmax (0, box.getWidth() - 11 - 28), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, box.isEnabled() ? hw::vfd : hw::vfdDim);
}

void HardwareLookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
{
    g.setColour (hw::vfdFaint.brighter (0.4f));
    g.setFont (getComboBoxFont (box));
    g.drawFittedText (box.getTextWhenNothingSelected(), label.getBounds(), juce::Justification::centredLeft, 1, 0.9f);
}

//==============================================================================
juce::Font HardwareLookAndFeel::getPopupMenuFont()
{
    return hw::font (12.5f);
}

void HardwareLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                              const juce::PopupMenu::Options&)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a2836), 0.0f, 0.0f,
                                             juce::Colour (0xff111b26), 0.0f, (float) height, false));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (hw::legendFaint.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (r.reduced (6.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
}

void HardwareLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                        bool isHighlighted, const juce::PopupMenu::Item& item,
                                                        const juce::PopupMenu::Options&)
{
    if (item.isSeparator)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (area.reduced (12, 0).withHeight (1).withY (area.getCentreY()));
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (area.reduced (12, 0).withHeight (1).withY (area.getCentreY() + 1));
        return;
    }

    auto r = area.reduced (5, 1);
    const bool enabled = item.isEnabled;

    if (isHighlighted && enabled)
    {
        g.setColour (hw::ledAmber.withAlpha (0.18f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (hw::ledAmber.withAlpha (0.35f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    juce::Colour tc = item.colour.isTransparent() ? (enabled ? hw::legend : hw::legendFaint) : item.colour;
    if (isHighlighted && enabled && item.colour.isTransparent())
        tc = juce::Colours::white;

    // 打勾的项用一颗亮着的 LED 表示——硬件上"当前选中"就是灯亮
    auto tickArea = r.removeFromLeft (24);
    if (item.isTicked)
        hw::drawLed (g, tickArea.toFloat().getCentre(), 3.2f, hw::ledAmber, 1.0f);

    r.removeFromRight (10);

    if (item.subMenu != nullptr)
        theme::drawIcon (g, Icon::chevronRight, r.removeFromRight (12).toFloat().withSizeKeepingCentre (10.0f, 10.0f),
                         hw::legendDim, 1.5f);

    g.setFont (getPopupMenuFont());

    if (item.shortcutKeyDescription.isNotEmpty())
    {
        g.setColour (hw::legendFaint);
        g.drawText (item.shortcutKeyDescription, r, juce::Justification::centredRight, true);
    }

    g.setColour (tc);
    g.drawFittedText (item.text, r, juce::Justification::centredLeft, 1, 0.9f);
}

void HardwareLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                                 const juce::String& sectionName,
                                                                 const juce::PopupMenu::Options&)
{
    g.setFont (labelFont (sectionName, 11.0f));
    g.setColour (hw::legendFaint.brighter (0.2f));
    g.drawFittedText (sectionName, area.reduced (14, 0).withTrimmedTop (4), juce::Justification::centredLeft, 1, 0.9f);
}

void HardwareLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                                                int standardMenuItemHeight, int& idealWidth,
                                                                int& idealHeight, const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth  = 50;
        idealHeight = 9;
        return;
    }

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 28;
    idealWidth  = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 24 + 10 + 34;
}

int HardwareLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return 6;
}

//==============================================================================
void HardwareLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    hw::drawRecess (g, r, 4.0f, ed.findColour (juce::TextEditor::backgroundColourId));

    if ((bool) ed.getProperties()["prSearch"])
        theme::drawIcon (g, Icon::search, juce::Rectangle<float> (11.0f, (float) height * 0.5f - 7.0f, 14.0f, 14.0f),
                         ed.hasKeyboardFocus (true) ? hw::vfd : hw::vfdDim, 1.6f);
}

void HardwareLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);

    if (ed.isEnabled() && ! ed.isReadOnly() && ed.hasKeyboardFocus (true))
    {
        g.setColour (hw::vfd.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (1.5f), 3.0f, 1.0f);
    }

    hw::drawGlassSheen (g, r.reduced (1.0f), 3.0f, 0.8f);
}

//==============================================================================
void HardwareLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float pos, float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool enabled = s.isEnabled();
    const auto label   = s.getName();
    const auto labels  = hwui::scaleOf (s);

    juce::Rectangle<float> labelRow, valueRow;
    if (label.isNotEmpty())
        labelRow = r.removeFromTop (17.0f);
    valueRow = r.removeFromBottom (16.0f);

    // 数字刻度要额外留一圈地方
    const bool  numbers = ! labels.isEmpty();
    const float ring    = numbers ? 17.0f : 8.0f;
    const float R       = juce::jmax (8.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - ring);
    const auto  c       = r.getCentre();
    const auto  ink     = hw::legend.withMultipliedAlpha (enabled ? 0.9f : 0.4f);

    hw::drawScale (g, c, R + 5.5f, a0, a1, numbers ? (labels.size() - 1) * 2 + 1 : 11, numbers ? 2 : 5,
                   labels, ink, R + 13.5f);

    hw::drawKnob (g, juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), a0 + pos * (a1 - a0),
                  hwui::knobOf (s), enabled, s.isMouseOverOrDragging());

    if (! labelRow.isEmpty())
    {
        g.setFont (labelFont (label, 11.5f));
        g.setColour (enabled ? hw::legend : hw::legendFaint);
        g.drawText (label, labelRow, juce::Justification::centred, true);
    }

    // 读数：拖动时变成琥珀色——手上在动的那个数一眼能找到
    const bool active = s.isMouseButtonDown();
    const auto txt = s.getTextFromValue (s.getValue());
    g.setFont (isAscii (txt) ? hw::monoFont (12.0f) : hw::font (11.5f, Weight::medium));
    g.setColour (! enabled ? hw::legendFaint : active ? hw::vfdAmber : hw::legendDim);
    g.drawText (txt, valueRow, juce::Justification::centred, true);
}

void HardwareLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float minPos, float maxPos,
                                            juce::Slider::SliderStyle style, juce::Slider& s)
{
    // 推子：一道凹槽 + 一个有厚度的推子帽。硬件皮肤自己不用横推子，这里是给
    // JUCE 自带界面（比如插件扫描窗口里偶尔出现的）一个不突兀的样子。
    const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool horiz = style == juce::Slider::LinearHorizontal || style == juce::Slider::TwoValueHorizontal;

    const auto groove = horiz ? juce::Rectangle<float> (r.getX(), r.getCentreY() - 2.5f, r.getWidth(), 5.0f)
                              : juce::Rectangle<float> (r.getCentreX() - 2.5f, r.getY(), 5.0f, r.getHeight());
    hw::drawRecess (g, groove, 2.5f, juce::Colours::black);

    auto cap = [&] (float p)
    {
        const auto t = horiz ? juce::Rectangle<float> (12.0f, 20.0f).withCentre ({ p, r.getCentreY() })
                             : juce::Rectangle<float> (20.0f, 12.0f).withCentre ({ r.getCentreX(), p });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (t.translated (0.0f, 2.0f), 2.5f);
        g.setGradientFill (juce::ColourGradient (hw::capGrey.brighter (0.3f), 0.0f, t.getY(),
                                                 hw::capGrey.darker (0.4f), 0.0f, t.getBottom(), false));
        g.fillRoundedRectangle (t, 2.5f);
        g.setColour (s.isEnabled() ? hw::legend : hw::legendFaint);
        if (horiz) g.fillRect (t.withSizeKeepingCentre (1.5f, t.getHeight() - 6.0f));
        else       g.fillRect (t.withSizeKeepingCentre (t.getWidth() - 6.0f, 1.5f));
    };

    if (style == juce::Slider::TwoValueHorizontal || style == juce::Slider::TwoValueVertical)
    {
        cap (minPos);
        cap (maxPos);
    }
    else
    {
        cap (sliderPos);
    }
}

//==============================================================================
int HardwareLookAndFeel::getDefaultScrollbarWidth()
{
    return 10;
}

void HardwareLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                         bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    if (thumbSize <= 0)
        return;

    const float t = (over || down) ? 5.0f : 3.5f;

    const auto thumb = vertical
        ? juce::Rectangle<float> ((float) x + ((float) width - t) * 0.5f, (float) thumbStart + 2.0f, t, (float) thumbSize - 4.0f)
        : juce::Rectangle<float> ((float) thumbStart + 2.0f, (float) y + ((float) height - t) * 0.5f, (float) thumbSize - 4.0f, t);

    g.setColour (down ? hw::vfd.withAlpha (0.8f) : (over ? hw::vfdDim.brighter (0.4f) : hw::vfdDim));
    g.fillRoundedRectangle (thumb, t * 0.5f);
}

//==============================================================================
namespace
{
    juce::TextLayout layoutTooltip (const juce::String& text)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, hw::font (12.5f), hw::plateInk);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 340.0f);
        return tl;
    }
}

void HardwareLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    // 一张米白的纸条——录音棚里贴在设备上的那种手写备注
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);

    g.setGradientFill (juce::ColourGradient (hw::plate.brighter (0.04f), 0.0f, 0.0f,
                                             hw::plate.darker (0.06f), 0.0f, (float) height, false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    layoutTooltip (text).draw (g, r.reduced (11.0f, 7.0f));
}

juce::Rectangle<int> HardwareLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos,
                                                            juce::Rectangle<int> parentArea)
{
    const auto tl = layoutTooltip (tipText);
    const int w = (int) std::ceil (tl.getWidth())  + 24;
    const int h = (int) std::ceil (tl.getHeight()) + 16;

    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 16,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 8)  : pos.y + 8,
                                 w, h)
               .constrainedWithin (parentArea);
}

//==============================================================================
juce::AlertWindow* HardwareLookAndFeel::createAlertWindow (const juce::String& title, const juce::String& message,
                                                           const juce::String& button1, const juce::String& button2,
                                                           const juce::String& button3, juce::MessageBoxIconType iconType,
                                                           int numButtons, juce::Component* associatedComponent)
{
    auto* w = LookAndFeel_V4::createAlertWindow (title, message, button1, button2, button3,
                                                 iconType, numButtons, associatedComponent);

    // 对话框是顶层窗口，不会自动继承关联组件的 LookAndFeel
    if (w != nullptr)
    {
        w->setLookAndFeel (this);

        if (w->getNumButtons() > 0)
        {
            ui::variant (*w->getButton (0), ui::Variant::primary);
            ui::tint (*w->getButton (0), hw::capRed);
        }
    }

    return w;
}

void HardwareLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                        const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    const auto b = alert.getLocalBounds();
    hw::drawFaceplate (g, b.toFloat());
    g.setColour (hw::seam);
    g.drawRect (b, 1);

    for (auto p : { juce::Point<float> { 11.0f, 11.0f }, { (float) b.getRight() - 11.0f, 11.0f },
                    { 11.0f, (float) b.getBottom() - 11.0f }, { (float) b.getRight() - 11.0f, (float) b.getBottom() - 11.0f } })
        hw::drawScrew (g, p, 4.0f, 0.4f + p.x * 0.01f);

    int iconSpace = 0;

    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon)
    {
        const bool warning = alert.getAlertType() == juce::MessageBoxIconType::WarningIcon;
        hw::drawLed (g, { 40.0f, 44.0f }, 6.0f, warning ? hw::ledAmber : hw::ledBlue, 1.0f);
        iconSpace = 74;
    }

    g.setColour (hw::legend);
    textLayout.draw (g, juce::Rectangle<int> (b.getX() + iconSpace, 30, b.getWidth(),
                                              b.getHeight() - getAlertWindowButtonHeight() - 20)
                            .getIntersection (b.withTrimmedLeft (iconSpace).withTrimmedRight (18))
                            .toFloat());
    juce::ignoreUnused (textArea);
}

int HardwareLookAndFeel::getAlertWindowButtonHeight()       { return 34; }
juce::Font HardwareLookAndFeel::getAlertWindowTitleFont()   { return hw::font (15.0f, Weight::bold); }
juce::Font HardwareLookAndFeel::getAlertWindowMessageFont() { return hw::font (13.0f); }
juce::Font HardwareLookAndFeel::getAlertWindowFont()        { return hw::font (13.0f); }

//==============================================================================
void HardwareLookAndFeel::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll (hw::bay);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (0, header.getHeight() - 1, header.getWidth(), 1);
}

void HardwareLookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&,
                                                 const juce::String& name, int, int width, int height,
                                                 bool over, bool down, int flags)
{
    if (over || down)
    {
        g.setColour (hw::panelHi.withAlpha (0.4f));
        g.fillRect (0, 0, width, height);
    }

    g.setFont (labelFont (name, 11.5f));
    g.setColour (hw::legendDim);
    g.drawText (name, juce::Rectangle<int> (10, 0, width - 26, height), juce::Justification::centredLeft, true);

    if ((flags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0)
    {
        const bool fwd = (flags & juce::TableHeaderComponent::sortedForwards) != 0;
        auto ar = juce::Rectangle<float> ((float) width - 20.0f, (float) height * 0.5f - 5.0f, 10.0f, 10.0f);
        g.saveState();
        if (! fwd)
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi, ar.getCentreX(), ar.getCentreY()));
        theme::drawIcon (g, Icon::chevronDown, ar, hw::legendDim, 1.5f);
        g.restoreState();
    }

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (width - 1, 6, 1, juce::jmax (0, height - 12));
}

void HardwareLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    // 右下角正好落在原木边框上：刻三道斜槽，像木头上的防滑纹
    const auto c = (over || dragging) ? hw::woodDark.darker (0.5f) : hw::woodDark.darker (0.25f);

    for (int i = 0; i < 3; ++i)
    {
        const float o = 4.0f + (float) i * 4.0f;
        g.setColour (c);
        g.drawLine ((float) w - o, (float) h - 2.0f, (float) w - 2.0f, (float) h - o, 1.4f);
        g.setColour (hw::woodLight.withAlpha (0.6f));
        g.drawLine ((float) w - o + 1.0f, (float) h - 2.0f, (float) w - 2.0f, (float) h - o + 1.0f, 0.8f);
    }
}

} // namespace pr
