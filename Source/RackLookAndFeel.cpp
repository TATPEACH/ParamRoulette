#include "RackLookAndFeel.h"

namespace pr
{

using theme::Icon;

namespace
{
    const juce::Colour fg     = rk::uiFg;
    const juce::Colour bg     = rk::uiBg;
    const juce::Colour inner  = rk::ui (0.10f);    ///< 按钮 / 下拉的底（Rack: innerColor = lerp 0.1）
    const juce::Colour innerS = rk::ui (0.20f);    ///< 悬停 / 选中（innerSelectedColor = lerp 0.2）
    const juce::Colour dimFg  = rk::ui (0.60f);    ///< 菜单里的小标题（menuTheme.textColor = lerp 0.6）

    juce::TextLayout layoutTooltip (const juce::String& text)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, rk::uiFont (12.5f), fg);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 340.0f);
        return tl;
    }
}

//==============================================================================
RackLookAndFeel::RackLookAndFeel()
{
    setDefaultSansSerifTypefaceName (theme::family());

    setColourScheme ({ bg, bg, bg, rk::ui (0.1f), fg, inner, juce::Colours::white, innerS, fg });

    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::backgroundColourId,  bg);

    setColour (juce::TextButton::buttonColourId,   inner);
    setColour (juce::TextButton::buttonOnColourId, innerS);
    setColour (juce::TextButton::textColourOffId,  fg);
    setColour (juce::TextButton::textColourOnId,   fg);

    setColour (juce::ToggleButton::textColourId,         fg);
    setColour (juce::ToggleButton::tickColourId,         fg);
    setColour (juce::ToggleButton::tickDisabledColourId, dimFg);

    setColour (juce::ComboBox::backgroundColourId,     inner);
    setColour (juce::ComboBox::textColourId,           fg);
    setColour (juce::ComboBox::outlineColourId,        rk::ui (0.1f));
    setColour (juce::ComboBox::arrowColourId,          dimFg);
    setColour (juce::ComboBox::focusedOutlineColourId, rk::ui (0.4f));

    // 菜单底色带一点点透明：JUCE 只在底色不透明时把菜单窗口设成不透明，半透明才画得出圆角
    setColour (juce::PopupMenu::backgroundColourId,            bg.withAlpha (0.995f));
    setColour (juce::PopupMenu::textColourId,                  fg);
    setColour (juce::PopupMenu::headerTextColourId,            dimFg);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, innerS);
    setColour (juce::PopupMenu::highlightedTextColourId,       fg);

    // Rack 暗色主题的文本框是**浅灰底、深色字**（textFieldTheme.innerColor = lerp 0.7）
    setColour (juce::TextEditor::backgroundColourId,      rk::ui (0.70f));
    setColour (juce::TextEditor::textColourId,            juce::Colour (0xff0a0a0a));
    setColour (juce::TextEditor::highlightColourId,       rk::ui (0.45f));
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId,         juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId,  rk::ui (0.85f));
    setColour (juce::TextEditor::shadowColourId,          juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId,       juce::Colour (0xff0a0a0a));

    setColour (juce::Label::textColourId,               fg);
    setColour (juce::Label::textWhenEditingColourId,    fg);
    setColour (juce::Label::outlineWhenEditingColourId, rk::ui (0.4f));

    setColour (juce::ScrollBar::thumbColourId, rk::ui (0.4f));
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::Slider::trackColourId,      rk::ui (0.4f));
    setColour (juce::Slider::backgroundColourId, bg);
    setColour (juce::Slider::thumbColourId,      fg);

    setColour (juce::TooltipWindow::backgroundColourId, bg);
    setColour (juce::TooltipWindow::textColourId,       fg);
    setColour (juce::TooltipWindow::outlineColourId,    rk::ui (0.15f));

    setColour (juce::AlertWindow::backgroundColourId, bg);
    setColour (juce::AlertWindow::textColourId,       fg);
    setColour (juce::AlertWindow::outlineColourId,    rk::ui (0.15f));

    setColour (juce::ListBox::backgroundColourId, bg);
    setColour (juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::ListBox::textColourId,       fg);

    setColour (juce::TableHeaderComponent::textColourId,       dimFg);
    setColour (juce::TableHeaderComponent::backgroundColourId, rk::ui (0.05f));
    setColour (juce::TableHeaderComponent::outlineColourId,    rk::ui (0.1f));
    setColour (juce::TableHeaderComponent::highlightColourId,  innerS);
}

//==============================================================================
juce::Font RackLookAndFeel::getTextButtonFont (juce::TextButton& b, int)
{
    return rk::uiFont (12.5f, ui::variantOf (b) == ui::Variant::primary);
}

void RackLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool highlighted, bool down)
{
    // Rack 的 ui::Button：深灰圆角块，悬停 / 按下浅一级；主按钮用浅色字底
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = ui::variantOf (b) == ui::Variant::primary;

    auto c = primary ? rk::ui (0.30f) : (b.getToggleState() ? innerS : inner);
    if (highlighted) c = c.brighter (0.12f);
    if (down)        c = c.darker (0.1f);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
}

void RackLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (8.0f, 0.0f);
    const auto tc = fg.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f);
    const auto ic = ui::iconOf (b);

    if (ic != Icon::none)
    {
        const float is = juce::jlimit (10.0f, 14.0f, (float) b.getHeight() - 12.0f);
        const auto txt = b.getButtonText();
        const auto f = getTextButtonFont (b, b.getHeight());

        if (txt.isEmpty())
        {
            theme::drawIcon (g, ic, r.withSizeKeepingCentre (is, is), tc, 1.6f);
            return;
        }

        const float tw = juce::GlyphArrangement::getStringWidth (f, txt);
        const float x0 = juce::jmax (r.getX(), r.getCentreX() - (is + 6.0f + tw) * 0.5f);
        theme::drawIcon (g, ic, { x0, r.getCentreY() - is * 0.5f, is, is }, tc, 1.6f);
        g.setFont (f);
        g.setColour (tc);
        g.drawText (txt, juce::Rectangle<float> (x0 + is + 6.0f, r.getY(), r.getRight() - x0 - is - 6.0f, r.getHeight()),
                    juce::Justification::centredLeft, true);
        return;
    }

    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (tc);
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1, 0.85f);
}

void RackLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    const float d = juce::jmin (16.0f, r.getHeight() - 4.0f);

    drawTickBox (g, b, r.getX() + 1.0f, r.getCentreY() - d * 0.5f, d, d,
                 b.getToggleState(), b.isEnabled(), highlighted, down);

    g.setFont (rk::uiFont (12.5f));
    g.setColour (fg.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawText (b.getButtonText(), r.withLeft (r.getX() + d + 8.0f), juce::Justification::centredLeft, true);
}

void RackLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h,
                                   bool ticked, bool isEnabled, bool highlighted, bool)
{
    // Rack 的 "Favorites" 勾选框：深灰小方块，勾上是浅色实心
    const auto r = juce::Rectangle<float> (x, y, w, h);
    g.setColour ((ticked ? rk::ui (0.75f) : (highlighted ? innerS : rk::ui (0.15f))).withMultipliedAlpha (isEnabled ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 3.0f);

    if (ticked)
        theme::drawIcon (g, Icon::check, r.reduced (w * 0.18f), bg, 1.8f);
}

//==============================================================================
void RackLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                    juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour ((box.isMouseOver (true) ? innerS : inner).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 4.0f);

    theme::drawIcon (g, Icon::chevronDown, { (float) width - 22.0f, (float) height * 0.5f - 6.0f, 12.0f, 12.0f },
                     dimFg, 1.6f);
}

juce::Font RackLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return rk::uiFont (12.5f);
}

void RackLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, juce::jmax (0, box.getWidth() - 10 - 26), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
juce::Font RackLookAndFeel::getPopupMenuFont()
{
    return rk::uiFont (12.5f);
}

void RackLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                          const juce::PopupMenu::Options&)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour (bg);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (rk::ui (0.12f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void RackLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                    bool isHighlighted, const juce::PopupMenu::Item& item,
                                                    const juce::PopupMenu::Options&)
{
    if (item.isSeparator)
    {
        g.setColour (rk::ui (0.15f));
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 0);
    const bool enabled = item.isEnabled;

    if (isHighlighted && enabled)
    {
        g.setColour (innerS);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
    }

    r.removeFromLeft (10);
    const auto tc = item.colour.isTransparent() ? (enabled ? fg : rk::ui (0.45f)) : item.colour;

    // Rack 的勾（✔）和子菜单箭头（▸）都在**右边**
    auto right = r.removeFromRight (22);
    if (item.isTicked)
        theme::drawIcon (g, Icon::check, right.toFloat().withSizeKeepingCentre (12.0f, 12.0f), fg, 1.8f);
    else if (item.subMenu != nullptr)
        theme::drawIcon (g, Icon::chevronRight, right.toFloat().withSizeKeepingCentre (10.0f, 10.0f), dimFg, 1.5f);

    g.setFont (getPopupMenuFont());

    if (item.shortcutKeyDescription.isNotEmpty())
    {
        g.setColour (dimFg);
        g.drawText (item.shortcutKeyDescription, r.withTrimmedRight (4), juce::Justification::centredRight, true);
    }

    g.setColour (tc);
    g.drawFittedText (item.text, r, juce::Justification::centredLeft, 1, 0.9f);
}

void RackLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                             const juce::String& sectionName,
                                                             const juce::PopupMenu::Options&)
{
    g.setFont (rk::uiFont (11.5f));
    g.setColour (dimFg);
    g.drawFittedText (sectionName, area.reduced (14, 0).withTrimmedTop (3), juce::Justification::centredLeft, 1, 0.9f);
}

void RackLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                                            int standardMenuItemHeight, int& idealWidth,
                                                            int& idealHeight, const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth  = 50;
        idealHeight = 9;
        return;
    }

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 26;
    idealWidth  = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 14 + 22 + 30;
}

int RackLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return 5;
}

//==============================================================================
void RackLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);

    if ((bool) ed.getProperties()["rkLcd"])
    {
        rk::paintDisplay (g, r, 1.0f);

        if ((bool) ed.getProperties()["prSearch"])
            theme::drawIcon (g, Icon::search, { 9.0f, (float) height * 0.5f - 6.5f, 13.0f, 13.0f },
                             rk::yellow.withAlpha (ed.hasKeyboardFocus (true) ? 0.95f : 0.55f), 1.5f);
        return;
    }

    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (r.reduced (0.5f), 4.0f);

    if ((bool) ed.getProperties()["prSearch"])
        theme::drawIcon (g, Icon::search, { 9.0f, (float) height * 0.5f - 6.5f, 13.0f, 13.0f },
                         juce::Colour (0xff3a3a3a), 1.6f);
}

void RackLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    if (! ed.isEnabled() || ed.isReadOnly() || ! ed.hasKeyboardFocus (true))
        return;

    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);

    if ((bool) ed.getProperties()["rkLcd"])
    {
        g.setColour (rk::yellow.withAlpha (0.45f));
        g.drawRect (r, 1.0f);
        return;
    }

    g.setColour (rk::ui (0.95f));
    g.drawRoundedRectangle (r, 4.0f, 1.2f);
}

//==============================================================================
void RackLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    // Rack 的 ui::Slider：深色条、填充部分浅一级、文字压在条上
    const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    g.setColour (rk::ui (0.06f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (rk::ui (0.30f));
    g.fillRoundedRectangle (r.withRight (juce::jmax (r.getX() + 4.0f, sliderPos)), 4.0f);

    g.setFont (rk::uiFont (12.0f));
    g.setColour (fg.withMultipliedAlpha (s.isEnabled() ? 1.0f : 0.4f));
    g.drawText (s.getName() + "  " + s.getTextFromValue (s.getValue()), r, juce::Justification::centred, true);
}

//==============================================================================
int RackLookAndFeel::getDefaultScrollbarWidth()
{
    return 10;
}

void RackLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    if (thumbSize <= 0)
        return;

    const float t = (over || down) ? 6.0f : 4.0f;
    const auto thumb = vertical
        ? juce::Rectangle<float> ((float) x + ((float) width - t) * 0.5f, (float) thumbStart + 2.0f, t, (float) thumbSize - 4.0f)
        : juce::Rectangle<float> ((float) thumbStart + 2.0f, (float) y + ((float) height - t) * 0.5f, (float) thumbSize - 4.0f, t);

    g.setColour (down ? rk::ui (0.6f) : rk::ui (over ? 0.5f : 0.38f));
    g.fillRoundedRectangle (thumb, t * 0.5f);
}

//==============================================================================
void RackLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour (bg.withAlpha (0.97f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (rk::ui (0.18f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    layoutTooltip (text).draw (g, r.reduced (10.0f, 6.0f));
}

juce::Rectangle<int> RackLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos,
                                                        juce::Rectangle<int> parentArea)
{
    const auto tl = layoutTooltip (tipText);
    const int w = (int) std::ceil (tl.getWidth())  + 22;
    const int h = (int) std::ceil (tl.getHeight()) + 14;

    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 16,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 8)  : pos.y + 8,
                                 w, h)
               .constrainedWithin (parentArea);
}

//==============================================================================
juce::AlertWindow* RackLookAndFeel::createAlertWindow (const juce::String& title, const juce::String& message,
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
            ui::variant (*w->getButton (0), ui::Variant::primary);
    }

    return w;
}

void RackLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                    const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    const auto b = alert.getLocalBounds();
    g.fillAll (bg);
    g.setColour (rk::ui (0.15f));
    g.drawRect (b, 1);

    int iconSpace = 0;

    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon)
    {
        const bool warning = alert.getAlertType() == juce::MessageBoxIconType::WarningIcon;
        rk::paintLight (g, { 36.0f, 44.0f }, 6.0f, warning ? rk::yellow : rk::blue, 1.0f, true);
        iconSpace = 68;
    }

    g.setColour (fg);
    textLayout.draw (g, juce::Rectangle<int> (b.getX() + iconSpace, 30, b.getWidth(),
                                              b.getHeight() - getAlertWindowButtonHeight() - 20)
                            .getIntersection (b.withTrimmedLeft (iconSpace).withTrimmedRight (16))
                            .toFloat());
    juce::ignoreUnused (textArea);
}

int RackLookAndFeel::getAlertWindowButtonHeight()       { return 30; }
juce::Font RackLookAndFeel::getAlertWindowTitleFont()   { return rk::uiFont (15.0f, true); }
juce::Font RackLookAndFeel::getAlertWindowMessageFont() { return rk::uiFont (13.0f); }
juce::Font RackLookAndFeel::getAlertWindowFont()        { return rk::uiFont (13.0f); }

//==============================================================================
void RackLookAndFeel::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll (rk::ui (0.05f));
    g.setColour (rk::ui (0.15f));
    g.fillRect (0, header.getHeight() - 1, header.getWidth(), 1);
}

void RackLookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&,
                                             const juce::String& name, int, int width, int height,
                                             bool over, bool down, int flags)
{
    if (over || down)
    {
        g.setColour (innerS.withAlpha (0.6f));
        g.fillRect (0, 0, width, height);
    }

    g.setFont (rk::uiFont (11.5f));
    g.setColour (dimFg);
    g.drawText (name, juce::Rectangle<int> (10, 0, width - 26, height), juce::Justification::centredLeft, true);

    if ((flags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0)
    {
        const bool fwd = (flags & juce::TableHeaderComponent::sortedForwards) != 0;
        auto ar = juce::Rectangle<float> ((float) width - 20.0f, (float) height * 0.5f - 5.0f, 10.0f, 10.0f);
        g.saveState();
        if (! fwd)
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi, ar.getCentreX(), ar.getCentreY()));
        theme::drawIcon (g, Icon::chevronDown, ar, dimFg, 1.5f);
        g.restoreState();
    }

    g.setColour (rk::ui (0.12f));
    g.fillRect (width - 1, 6, 1, juce::jmax (0, height - 12));
}

void RackLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour ((over || dragging) ? rk::ui (0.7f) : rk::ui (0.4f));

    for (int i = 0; i < 3; ++i)
    {
        const float o = 4.0f + (float) i * 4.0f;
        g.drawLine ((float) w - o, (float) h - 2.0f, (float) w - 2.0f, (float) h - o, 1.2f);
    }
}

} // namespace pr
