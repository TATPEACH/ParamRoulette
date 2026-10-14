#include "GridLookAndFeel.h"

namespace pr
{

using theme::Icon;
using theme::Weight;

//==============================================================================
namespace ui
{
    static const juce::Identifier kVariant { "prVariant" };
    static const juce::Identifier kIcon    { "prIcon" };
    static const juce::Identifier kTint    { "prTint" };
    static const juce::Identifier kSwitch  { "prSwitch" };
    static const juce::Identifier kLeft    { "prLeft" };
    static const juce::Identifier kBadge   { "prBadge" };

    void variant (juce::Component& c, Variant v)      { c.getProperties().set (kVariant, (int) v); c.repaint(); }
    void icon    (juce::Component& c, theme::Icon i)  { c.getProperties().set (kIcon, (int) i); c.repaint(); }
    void tint    (juce::Component& c, juce::Colour t) { c.getProperties().set (kTint, (juce::int64) t.getARGB()); c.repaint(); }
    void asSwitch (juce::ToggleButton& b)             { b.getProperties().set (kSwitch, true); b.repaint(); }
    void alignLeft (juce::Component& c)               { c.getProperties().set (kLeft, true); c.repaint(); }
    void badge (juce::Component& c, int n)            { c.getProperties().set (kBadge, n); c.repaint(); }

    Variant variantOf (const juce::Component& c)
    {
        return (Variant) (int) c.getProperties().getWithDefault (kVariant, (int) Variant::normal);
    }

    theme::Icon iconOf (const juce::Component& c)
    {
        return (theme::Icon) (int) c.getProperties().getWithDefault (kIcon, (int) theme::Icon::none);
    }

    juce::Colour tintOf (const juce::Component& c, juce::Colour fallback)
    {
        const auto v = c.getProperties()[kTint];
        return v.isVoid() ? fallback : juce::Colour ((juce::uint32) (juce::int64) v);
    }

    static bool isSwitch (const juce::Component& c) { return (bool) c.getProperties()[kSwitch]; }
}

//==============================================================================
GridLookAndFeel::GridLookAndFeel()
{
    using namespace theme;

    // 只换我们这一棵组件树的字体，**不动全局默认**——宿主里别的插件界面不该被波及。
    setDefaultSansSerifTypefaceName (family());

    // 先给一套整体配色，覆盖到我们没单独处理的控件上。
    setColourScheme ({ canvas,          // windowBackground
                       module,          // widgetBackground
                       module,          // menuBackground
                       stroke,          // outline
                       text,            // defaultText
                       control,         // defaultFill
                       juce::Colours::white, // highlightedText
                       controlHi,       // highlightedFill
                       text });         // menuText

    setColour (juce::ResizableWindow::backgroundColourId, canvas);
    setColour (juce::DocumentWindow::backgroundColourId,  canvas);

    setColour (juce::TextButton::buttonColourId,   control);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId,  text);
    setColour (juce::TextButton::textColourOnId,   ink);

    setColour (juce::ToggleButton::textColourId,         text);
    setColour (juce::ToggleButton::tickColourId,         accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textFaint);

    setColour (juce::ComboBox::backgroundColourId,     well);
    setColour (juce::ComboBox::textColourId,           text);
    setColour (juce::ComboBox::outlineColourId,        stroke);
    setColour (juce::ComboBox::arrowColourId,          textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);

    // 菜单底色**故意带一点点透明**：JUCE 只在底色不透明时把菜单窗口设成不透明，
    // 半透明的窗口才能画出圆角（不然四个角会露出方形的黑边）。
    setColour (juce::PopupMenu::backgroundColourId,            module.withAlpha (0.995f));
    setColour (juce::PopupMenu::textColourId,                  text);
    setColour (juce::PopupMenu::headerTextColourId,            textFaint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, controlHi);
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    setColour (juce::TextEditor::backgroundColourId,        well);
    setColour (juce::TextEditor::textColourId,              text);
    setColour (juce::TextEditor::highlightColourId,         accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId,   juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId,           stroke);
    setColour (juce::TextEditor::focusedOutlineColourId,    accent);
    setColour (juce::TextEditor::shadowColourId,            juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId,         accent);

    setColour (juce::Label::textColourId,               textDim);
    setColour (juce::Label::textWhenEditingColourId,    text);
    setColour (juce::Label::outlineWhenEditingColourId, accent);

    setColour (juce::ScrollBar::thumbColourId, strokeHi);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::Slider::rotarySliderFillColourId,    accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, well);
    setColour (juce::Slider::trackColourId,               accent);
    setColour (juce::Slider::backgroundColourId,          well);
    setColour (juce::Slider::thumbColourId,               knobFace);
    setColour (juce::Slider::textBoxTextColourId,         text);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff343434));
    setColour (juce::TooltipWindow::textColourId,       text);
    setColour (juce::TooltipWindow::outlineColourId,    strokeHi);

    setColour (juce::AlertWindow::backgroundColourId, module);
    setColour (juce::AlertWindow::textColourId,       text);
    setColour (juce::AlertWindow::outlineColourId,    strokeHi);

    setColour (juce::ListBox::backgroundColourId, module);
    setColour (juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::ListBox::textColourId,       text);

    setColour (juce::TableHeaderComponent::textColourId,       textDim);
    setColour (juce::TableHeaderComponent::backgroundColourId, well);
    setColour (juce::TableHeaderComponent::outlineColourId,    stroke);
    setColour (juce::TableHeaderComponent::highlightColourId,  controlHi);
}

//==============================================================================
juce::Font GridLookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
{
    if (ui::variantOf (b) == ui::Variant::primary)
        return theme::font (buttonHeight >= 34 ? 14.5f : 13.0f, Weight::bold);

    return theme::font (buttonHeight < 24 ? 11.5f : 12.5f, Weight::medium);
}

void GridLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool highlighted, bool down)
{
    using namespace theme;

    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = b.getProperties().contains ("radius") ? (float) b.getProperties()["radius"]
                                                               : r.getHeight() * 0.5f;   // 胶囊形
    const auto v   = ui::variantOf (b);
    const auto c   = ui::tintOf (b);
    const bool on  = b.getToggleState();

    juce::Colour fill, line = juce::Colours::transparentBlack;

    switch (v)
    {
        case ui::Variant::primary:
            fill = c;
            if (highlighted) fill = fill.brighter (0.12f);
            if (down)        fill = fill.darker (0.18f);
            break;

        case ui::Variant::ghost:
            fill = on   ? tint (c, 0.30f)
                 : down ? control
                 : highlighted ? control.withAlpha (0.75f)
                 : juce::Colours::transparentBlack;
            break;

        case ui::Variant::chip:
            fill = on ? tint (c, 0.30f) : (highlighted ? controlHi.withAlpha (0.55f) : well);
            line = on ? c.withAlpha (0.65f) : stroke;
            break;

        case ui::Variant::normal:
        default:
            fill = on   ? tint (c, 0.35f)
                 : down ? control.darker (0.12f)
                 : highlighted ? controlHi
                 : control;
            break;
    }

    if (! b.isEnabled())
    {
        fill = fill.withMultipliedAlpha (0.45f);
        line = line.withMultipliedAlpha (0.45f);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);

    if (! line.isTransparent())
    {
        g.setColour (line);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    // 主按钮顶上一道很淡的高光，像 Bitwig 的 PLAY 键那样有点"实体感"
    if (v == ui::Variant::primary && b.isEnabled())
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.0f : 0.18f));
        g.drawRoundedRectangle (r.reduced (1.0f), radius - 1.0f, 1.0f);
    }
}

void GridLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    using namespace theme;

    const auto v  = ui::variantOf (b);
    const auto c  = ui::tintOf (b);
    const bool on = b.getToggleState();

    juce::Colour tc;
    switch (v)
    {
        case ui::Variant::primary: tc = ink; break;
        case ui::Variant::ghost:   tc = on ? c.brighter (0.15f) : (highlighted ? text : textDim); break;
        case ui::Variant::chip:    tc = on ? c.brighter (0.25f) : (highlighted ? text : textDim); break;
        case ui::Variant::normal:
        default:                   tc = on ? c.brighter (0.25f) : text; break;
    }

    if (! b.isEnabled())
        tc = tc.withMultipliedAlpha (0.4f);

    const auto textFont = getTextButtonFont (b, b.getHeight());
    const auto ic   = ui::iconOf (b);
    const auto txt  = b.getButtonText();
    const float h   = (float) b.getHeight();
    const float pad = h < 30.0f ? 9.0f : 12.0f;
    auto bounds = b.getLocalBounds().toFloat();
    auto r = bounds.reduced (pad, 0.0f);

    // —— 左对齐样式（顶栏的插件选择器）：编号徽章 + 名字靠左，图标挪到最右边当"下拉箭头"
    if ((bool) b.getProperties()[ui::kLeft])
    {
        if (const int badge = (int) b.getProperties().getWithDefault (ui::kBadge, 0); badge > 0)
        {
            const auto br = r.removeFromLeft (h - 10.0f).withSizeKeepingCentre (h - 12.0f, h - 12.0f);
            g.setColour (accent.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
            g.fillEllipse (br);
            g.setColour (ink);
            g.setFont (font (11.0f, Weight::bold));
            g.drawText (juce::String (badge), br, juce::Justification::centred, false);
            r.removeFromLeft (8.0f);
        }

        if (ic != Icon::none)
        {
            drawIcon (g, ic, r.removeFromRight (12.0f).withSizeKeepingCentre (12.0f, 12.0f), textDim, 1.6f);
            r.removeFromRight (6.0f);
        }

        g.setFont (textFont);
        g.setColour (tc);
        g.drawText (txt, r, juce::Justification::centredLeft, true);
        return;
    }

    if (ic != Icon::none)
    {
        const float is = juce::jlimit (10.0f, 16.0f, h - 12.0f);

        if (txt.isEmpty())
        {
            drawIcon (g, ic, bounds.withSizeKeepingCentre (is, is), tc, 1.6f);
            return;
        }

        // 图标 + 文字作为一组居中。放不下时先吃掉内边距，再贴左——
        // 上一版这里内边距算得太大，「体检」「勾选匹配」都被截成了省略号。
        const float tw    = juce::GlyphArrangement::getStringWidth (textFont, txt);
        const float total = is + 6.0f + tw;
        const auto  area  = total <= r.getWidth() ? r : bounds.reduced (4.0f, 0.0f);
        const float x0    = juce::jmax (area.getX(), area.getCentreX() - total * 0.5f);

        drawIcon (g, ic, { x0, bounds.getCentreY() - is * 0.5f, is, is }, tc, 1.6f);

        g.setFont (textFont);
        g.setColour (tc);
        g.drawText (txt, juce::Rectangle<float> (x0 + is + 6.0f, 0.0f, bounds.getRight() - 4.0f - (x0 + is + 6.0f), h),
                    juce::Justification::centredLeft, true);
        return;
    }

    g.setFont (textFont);
    g.setColour (tc);
    g.drawFittedText (txt, r.toNearestInt(), juce::Justification::centred, 1, 0.85f);
}

void GridLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    using namespace theme;

    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const auto c  = ui::tintOf (b);
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;

    float textX = 0.0f;

    if (ui::isSwitch (b))
    {
        // 胶囊开关：Bitwig 没有这种控件，但"一个模块整体开/关"用它最直观
        const float sw = 32.0f, sh = 18.0f;
        const auto  track = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - sh * 0.5f, sw, sh);

        g.setColour ((on ? c : well).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track, sh * 0.5f);

        if (! on)
        {
            g.setColour ((highlighted ? strokeHi.brighter (0.2f) : strokeHi).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (track.reduced (0.5f), sh * 0.5f, 1.0f);
        }

        const float kd = sh - 4.0f;
        const auto  knob = juce::Rectangle<float> (kd, kd)
                               .withCentre ({ on ? track.getRight() - sh * 0.5f : track.getX() + sh * 0.5f,
                                              track.getCentreY() });

        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        g.fillEllipse (knob.translated (0.0f, 1.0f));
        g.setColour ((on ? knobFace : textDim).withMultipliedAlpha (alpha));
        g.fillEllipse (knob);

        textX = track.getRight() + 8.0f;
    }
    else
    {
        const float d = juce::jmin (16.0f, r.getHeight() - 4.0f);
        drawTickBox (g, b, r.getX() + 1.0f, r.getCentreY() - d * 0.5f, d, d,
                     on, b.isEnabled(), highlighted, down);
        textX = r.getX() + d + 8.0f;
    }

    if (b.getButtonText().isNotEmpty())
    {
        g.setFont (font (12.5f, Weight::medium));
        g.setColour ((on || highlighted ? text : textDim).withMultipliedAlpha (alpha));
        g.drawText (b.getButtonText(), r.withLeft (textX), juce::Justification::centredLeft, true);
    }
}

void GridLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& comp, float x, float y, float w, float h,
                                   bool ticked, bool isEnabled, bool highlighted, bool)
{
    using namespace theme;

    // Bitwig 的勾选是**圆的**：橙色实心圆里一个深色勾（设备面板里 "Active" 那个）。
    const auto box   = juce::Rectangle<float> (x, y, w, h).reduced (0.5f);
    const auto c     = ui::tintOf (comp);
    const float a    = isEnabled ? 1.0f : 0.4f;

    if (ticked)
    {
        g.setColour (c.withMultipliedAlpha (a));
        g.fillEllipse (box);
        drawIcon (g, Icon::check, box.reduced (box.getWidth() * 0.16f), ink.withMultipliedAlpha (a),
                  juce::jmax (1.5f, box.getWidth() * 0.12f));
    }
    else
    {
        g.setColour (well.withMultipliedAlpha (a));
        g.fillEllipse (box);
        g.setColour ((highlighted ? textDim : strokeHi.brighter (0.15f)).withMultipliedAlpha (a));
        g.drawEllipse (box.reduced (0.5f), 1.4f);
    }
}

//==============================================================================
void GridLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    using namespace theme;

    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const float a = box.isEnabled() ? 1.0f : 0.45f;

    g.setColour ((box.isMouseOver (true) ? controlHi.withAlpha (0.55f) : well).withMultipliedAlpha (a));
    g.fillRoundedRectangle (r, radius);

    g.setColour ((box.hasKeyboardFocus (true) ? accent : stroke).withMultipliedAlpha (a));
    g.drawRoundedRectangle (r, radius, 1.0f);

    drawIcon (g, Icon::chevronDown,
              juce::Rectangle<float> ((float) width - 26.0f, (float) height * 0.5f - 6.0f, 12.0f, 12.0f),
              textDim.withMultipliedAlpha (a), 1.6f);
}

juce::Font GridLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return theme::font (12.5f, Weight::medium);
}

void GridLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (12, 1, juce::jmax (0, box.getWidth() - 12 - 28), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void GridLookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
{
    g.setColour (theme::textFaint);
    g.setFont (getComboBoxFont (box));
    g.drawFittedText (box.getTextWhenNothingSelected(), label.getBounds(),
                      juce::Justification::centredLeft, 1, 0.9f);
}

//==============================================================================
juce::Font GridLookAndFeel::getPopupMenuFont()
{
    return theme::font (12.5f);
}

void GridLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                          const juce::PopupMenu::Options&)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour (theme::module);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (theme::strokeHi);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
}

void GridLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                    bool isHighlighted, const juce::PopupMenu::Item& item,
                                                    const juce::PopupMenu::Options&)
{
    using namespace theme;

    if (item.isSeparator)
    {
        g.setColour (stroke);
        g.fillRect (area.reduced (12, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (5, 1);
    const bool enabled = item.isEnabled;

    if (isHighlighted && enabled)
    {
        g.setColour (controlHi);
        g.fillRoundedRectangle (r.toFloat(), 7.0f);
    }

    juce::Colour tc = item.colour.isTransparent() ? (enabled ? text : textFaint) : item.colour;
    if (isHighlighted && enabled && item.colour.isTransparent())
        tc = juce::Colours::white;

    auto tickArea = r.removeFromLeft (24);
    if (item.isTicked)
        drawIcon (g, Icon::check, tickArea.toFloat().withSizeKeepingCentre (13.0f, 13.0f), accent, 1.9f);

    r.removeFromRight (10);

    if (item.subMenu != nullptr)
        drawIcon (g, Icon::chevronRight, r.removeFromRight (12).toFloat().withSizeKeepingCentre (10.0f, 10.0f),
                  textDim, 1.5f);

    g.setFont (getPopupMenuFont());

    if (item.shortcutKeyDescription.isNotEmpty())
    {
        g.setColour (textFaint);
        g.drawText (item.shortcutKeyDescription, r, juce::Justification::centredRight, true);
    }

    g.setColour (tc);
    g.drawFittedText (item.text, r, juce::Justification::centredLeft, 1, 0.9f);
}

void GridLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                             const juce::String& sectionName,
                                                             const juce::PopupMenu::Options&)
{
    g.setFont (theme::font (11.0f, Weight::bold));
    g.setColour (theme::textFaint);
    g.drawFittedText (sectionName, area.reduced (14, 0).withTrimmedTop (4),
                      juce::Justification::centredLeft, 1, 0.9f);
}

void GridLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
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

int GridLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return 6;
}

//==============================================================================
void GridLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    using namespace theme;

    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    const float radius = juce::jmin (r.getHeight() * 0.5f, rWell);

    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (r, radius);

    if ((bool) ed.getProperties()["prSearch"])
        drawIcon (g, Icon::search, juce::Rectangle<float> (11.0f, (float) height * 0.5f - 7.0f, 14.0f, 14.0f),
                  ed.hasKeyboardFocus (true) ? text : textFaint, 1.6f);
}

void GridLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    if (! ed.isEnabled() || ed.isReadOnly())
        return;

    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    const float radius = juce::jmin (r.getHeight() * 0.5f, theme::rWell);
    const bool focused = ed.hasKeyboardFocus (true);

    g.setColour (focused ? theme::accent : theme::stroke);
    g.drawRoundedRectangle (r, radius, focused ? 1.3f : 1.0f);
}

//==============================================================================
void GridLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float pos, float a0, float a1, juce::Slider& s)
{
    using namespace theme;

    // 布局：上面一行标签、中间旋钮、下面一行数值。Grid 的旋钮就是这个结构，
    // 而且数值用和弧一样的颜色——一眼能把数字和旋钮对上。
    auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool enabled = s.isEnabled();
    const auto colour  = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto label   = s.getName();

    juce::Rectangle<float> labelRow, valueRow;
    if (label.isNotEmpty())
        labelRow = r.removeFromTop (16.0f);
    valueRow = r.removeFromBottom (16.0f);

    const float d = juce::jmax (12.0f, juce::jmin (r.getWidth(), r.getHeight()) - 2.0f);
    const auto  knob   = r.withSizeKeepingCentre (d, d);
    const auto  c      = knob.getCentre();
    const float ringW  = juce::jmax (3.0f, d * 0.08f);
    const float ringR  = d * 0.5f - ringW * 0.5f - 1.0f;
    const float a      = a0 + pos * (a1 - a0);

    const juce::PathStrokeType ringStroke (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
    g.setColour (well);
    g.strokePath (track, ringStroke);

    if (pos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a, true);
        g.setColour (enabled ? colour : textFaint);
        g.strokePath (value, ringStroke);
    }

    // 白色旋钮面 + 投影 + 一点点竖向渐变：Grid 大旋钮的样子
    const float faceR = ringR - ringW * 0.5f - 3.0f;
    const auto  face  = juce::Rectangle<float> (faceR * 2.0f, faceR * 2.0f).withCentre (c);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (face.translated (0.0f, 1.6f).expanded (0.6f));

    g.setColour (enabled ? knobFace : juce::Colour (0xff5c5c5c));
    g.fillEllipse (face);

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), c.x, face.getY(),
                                             juce::Colours::black.withAlpha (0.12f), c.x, face.getBottom(), false));
    g.fillEllipse (face);

    if (s.isMouseOverOrDragging() && enabled)
    {
        g.setColour (colour.withAlpha (0.18f));
        g.drawEllipse (face.expanded (2.0f), 2.0f);
    }

    juce::Path pointer;
    pointer.startNewSubPath (c.getPointOnCircumference (faceR * 0.2f, a));
    pointer.lineTo (c.getPointOnCircumference (faceR * 0.8f, a));
    g.setColour (ink);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (2.0f, faceR * 0.17f),
                                                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (! labelRow.isEmpty())
    {
        g.setFont (font (11.5f, Weight::medium));
        g.setColour (enabled ? textDim : textFaint);
        g.drawText (label, labelRow, juce::Justification::centred, true);
    }

    g.setFont (font (12.0f, Weight::medium));
    g.setColour (enabled ? colour.brighter (0.1f) : textFaint);
    g.drawText (s.getTextFromValue (s.getValue()), valueRow, juce::Justification::centred, true);
}

void GridLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minPos, float maxPos,
                                        juce::Slider::SliderStyle style, juce::Slider& s)
{
    using namespace theme;

    const bool enabled = s.isEnabled();
    const auto colour  = s.findColour (juce::Slider::trackColourId);
    auto r = juce::Rectangle<int> (x, y, width, height).toFloat();

    const bool twoValue = (style == juce::Slider::TwoValueHorizontal);

    // 标签 / 数值一行。范围滑块要同时看到上下限，所以数值写成 "0.00 – 1.00"。
    if (s.getName().isNotEmpty())
    {
        auto labelRow = r.removeFromTop (16.0f);
        g.setFont (font (11.5f, Weight::medium));
        g.setColour (enabled ? textDim : textFaint);
        g.drawText (s.getName(), labelRow, juce::Justification::centredLeft, true);

        const auto vt = twoValue ? juce::String (s.getMinValue(), 2) + juce::String::fromUTF8 (" \xe2\x80\x93 ")
                                       + juce::String (s.getMaxValue(), 2)
                                 : s.getTextFromValue (s.getValue());
        g.setColour (enabled ? colour.brighter (0.1f) : textFaint);
        g.drawText (vt, labelRow, juce::Justification::centredRight, true);
    }

    const float th = 6.0f;
    const float cy = r.getCentreY();
    const auto  track = juce::Rectangle<float> (r.getX() + 2.0f, cy - th * 0.5f, r.getWidth() - 4.0f, th);

    g.setColour (well);
    g.fillRoundedRectangle (track, th * 0.5f);

    const float from = twoValue ? minPos : track.getX();
    const float to   = twoValue ? maxPos : sliderPos;

    g.setColour (enabled ? colour : textFaint);
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, to), track.getY(),
                                                                       juce::jmax (from, to), track.getBottom()),
                            th * 0.5f);

    auto thumb = [&] (float px)
    {
        const auto t = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ px, cy });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (t.translated (0.0f, 1.2f));
        g.setColour (enabled ? knobFace : juce::Colour (0xff5c5c5c));
        g.fillEllipse (t);
    };

    if (twoValue)
    {
        thumb (minPos);
        thumb (maxPos);
    }
    else
    {
        thumb (sliderPos);
    }
}

int GridLookAndFeel::getSliderThumbRadius (juce::Slider&)
{
    return 8;
}

//==============================================================================
int GridLookAndFeel::getDefaultScrollbarWidth()
{
    return 10;
}

void GridLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    using namespace theme;

    if (thumbSize <= 0)
        return;

    // 细、圆、悬停时变粗——macOS / Bitwig 都是这个路数，比 Win95 那种带箭头的灰槽安静得多。
    const float t = (over || down) ? 6.0f : 4.0f;

    const auto thumb = vertical
        ? juce::Rectangle<float> ((float) x + ((float) width - t) * 0.5f, (float) thumbStart + 2.0f,
                                  t, (float) thumbSize - 4.0f)
        : juce::Rectangle<float> ((float) thumbStart + 2.0f, (float) y + ((float) height - t) * 0.5f,
                                  (float) thumbSize - 4.0f, t);

    g.setColour (down ? textDim : (over ? strokeHi.brighter (0.25f) : strokeHi));
    g.fillRoundedRectangle (thumb, t * 0.5f);
}

//==============================================================================
namespace
{
    juce::TextLayout layoutTooltip (const juce::String& text)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (text, theme::font (12.5f), theme::text);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 340.0f);
        return tl;
    }
}

void GridLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);

    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    layoutTooltip (text).draw (g, r.reduced (11.0f, 7.0f));
}

juce::Rectangle<int> GridLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos,
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
juce::AlertWindow* GridLookAndFeel::createAlertWindow (const juce::String& title, const juce::String& message,
                                                       const juce::String& button1, const juce::String& button2,
                                                       const juce::String& button3, juce::MessageBoxIconType iconType,
                                                       int numButtons, juce::Component* associatedComponent)
{
    auto* w = LookAndFeel_V4::createAlertWindow (title, message, button1, button2, button3,
                                                 iconType, numButtons, associatedComponent);

    // JUCE 用关联组件的 LookAndFeel **创建**对话框，但对话框是顶层窗口，
    // 不会自动继承它——不设的话画出来又是默认的蓝灰色。
    if (w != nullptr)
    {
        w->setLookAndFeel (this);

        if (w->getNumButtons() > 0)
            ui::variant (*w->getButton (0), ui::Variant::primary);
    }

    return w;
}

void GridLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                    const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    using namespace theme;

    // 对话框是不透明的顶层窗口（TopLevelWindow 构造时就设了），画不出真正的圆角；
    // 整块铺底色，再画一圈描边和左上角一个分类色端口。
    const auto b = alert.getLocalBounds();
    g.fillAll (module);
    g.setColour (strokeHi);
    g.drawRect (b, 1);

    int iconSpace = 0;

    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon)
    {
        const auto ic = alert.getAlertType() == juce::MessageBoxIconType::WarningIcon ? warn : blue;
        drawIcon (g, alert.getAlertType() == juce::MessageBoxIconType::WarningIcon ? Icon::warning : Icon::more,
                  juce::Rectangle<float> (26.0f, 30.0f, 26.0f, 26.0f), ic, 2.0f);
        iconSpace = 80;
    }

    g.setColour (text);
    textLayout.draw (g, juce::Rectangle<int> (b.getX() + iconSpace, 30, b.getWidth(),
                                              b.getHeight() - getAlertWindowButtonHeight() - 20)
                            .getIntersection (b.withTrimmedLeft (iconSpace).withTrimmedRight (16))
                            .toFloat());
    juce::ignoreUnused (textArea);
}

int GridLookAndFeel::getAlertWindowButtonHeight()       { return 32; }
juce::Font GridLookAndFeel::getAlertWindowTitleFont()   { return theme::font (15.0f, Weight::bold); }
juce::Font GridLookAndFeel::getAlertWindowMessageFont() { return theme::font (13.0f); }
juce::Font GridLookAndFeel::getAlertWindowFont()        { return theme::font (13.0f); }

//==============================================================================
void GridLookAndFeel::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll (theme::well);
    g.setColour (theme::stroke);
    g.fillRect (0, header.getHeight() - 1, header.getWidth(), 1);
}

void GridLookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&,
                                             const juce::String& name, int, int width, int height,
                                             bool over, bool down, int flags)
{
    using namespace theme;

    if (over || down)
    {
        g.setColour (controlHi.withAlpha (0.35f));
        g.fillRect (0, 0, width, height);
    }

    g.setFont (font (11.5f, Weight::medium));
    g.setColour (textDim);
    g.drawText (name, juce::Rectangle<int> (10, 0, width - 26, height), juce::Justification::centredLeft, true);

    if ((flags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0)
    {
        const bool fwd = (flags & juce::TableHeaderComponent::sortedForwards) != 0;
        auto ar = juce::Rectangle<float> ((float) width - 20.0f, (float) height * 0.5f - 5.0f, 10.0f, 10.0f);
        g.saveState();
        if (! fwd)
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi,
                                                             ar.getCentreX(), ar.getCentreY()));
        drawIcon (g, Icon::chevronDown, ar, textDim, 1.5f);
        g.restoreState();
    }

    g.setColour (stroke);
    g.fillRect (width - 1, 6, 1, juce::jmax (0, height - 12));
}

void GridLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    // 三颗斜排的小点，比默认的三道斜线安静
    g.setColour ((over || dragging) ? theme::textDim : theme::textFaint);

    for (int i = 0; i < 3; ++i)
        for (int j = 0; j <= i; ++j)
            g.fillEllipse ((float) w - 4.0f - (float) j * 4.5f, (float) h - 4.0f - (float) (i - j) * 4.5f, 2.0f, 2.0f);
}

} // namespace pr
