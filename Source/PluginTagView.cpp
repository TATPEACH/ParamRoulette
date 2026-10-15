#include "PluginTagView.h"
#include "GridLookAndFeel.h"
#include "HwTheme.h"

namespace pr
{

PluginTagView::PluginTagView (PluginScanner& s, Look l)
    : scanner (s), look (l)
{
    table.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    table.setRowHeight (30);
    table.setHeaderHeight (28);

    auto& h = table.getHeader();
    h.addColumn ("插件"_u8,   colName,   300, 120, -1, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("格式"_u8,   colFormat,  80,  60, -1, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("功能标签"_u8, colTag,   190, 120, -1, juce::TableHeaderComponent::defaultFlags);

    addAndMakeVisible (table);

    teFilter.getProperties().set ("prSearch", true);
    teFilter.setIndents (32, 0);
    teFilter.setJustification (juce::Justification::centredLeft);
    teFilter.setFont (theme::font (12.5f));
    teFilter.setTextToShowWhenEmpty ("按名字 / 厂牌筛选（空格分隔）"_u8, theme::textFaint);
    teFilter.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible (teFilter);

    // 默认只看未分类：已经标好的没必要再翻一遍。七百个插件里
    // 真正需要人工处理的就是这一批。
    tbUntaggedOnly.setToggleState (true, juce::dontSendNotification);
    tbUntaggedOnly.onClick = [this] { rebuildRows(); };
    addAndMakeVisible (tbUntaggedOnly);

    ui::variant (bGuess, ui::Variant::primary);
    ui::icon (bGuess, theme::Icon::tag);
    bGuess.setTooltip ("按插件名里的关键词猜一轮。只能盖住约三成，"_u8
                       "而且只填未分类的，不会覆盖你手动标过的。"_u8);
    bGuess.onClick = [this] { guessAll(); };
    addAndMakeVisible (bGuess);

    lCoverage.setFont (theme::font (11.5f));
    lCoverage.setColour (juce::Label::textColourId, theme::textDim);
    lCoverage.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (lCoverage);

    setLook (look);
    rebuildRows();
}

PluginTagView::~PluginTagView() = default;

void PluginTagView::setLook (Look l)
{
    look = l;

    const bool hwLook = look == Look::hardware;
    teFilter.setTextToShowWhenEmpty ("按名字 / 厂牌筛选（空格分隔）"_u8, hwLook ? hw::vfdDim : theme::textFaint);
    lCoverage.setColour (juce::Label::textColourId, hwLook ? hw::legendDim : theme::textDim);
    repaint();
    table.repaint();
}

void PluginTagView::paint (juce::Graphics& g)
{
    if (look == Look::hardware)
    {
        // 藏青面板上压一块荧光屏，表格就显示在屏幕里
        hw::drawFaceplate (g, getLocalBounds().toFloat());
        const auto screen = table.getBounds().expanded (6).toFloat();
        hw::drawRecess (g, screen, 5.0f, hw::glass);
        return;
    }

    g.fillAll (theme::canvas);
    theme::fillDotGrid (g, getLocalBounds());

    // 表格放在一张卡片里，和主界面一个语言。**不画投影**：表格是透明的，悬停一行
    // 就会连带重画这里，DropShadow 每次都要把整张卡片模糊一遍，太贵。
    theme::drawCard (g, table.getBounds().expanded (6).toFloat(), theme::rCard, theme::module, false);
}

void PluginTagView::resized()
{
    auto r = getLocalBounds().reduced (theme::gap);

    {
        auto row = r.removeFromTop (32);
        bGuess.setBounds (row.removeFromRight (134));
        row.removeFromRight (10);
        tbUntaggedOnly.setBounds (row.removeFromRight (112));
        row.removeFromRight (10);
        teFilter.setBounds (row);
    }

    r.removeFromTop (14);
    lCoverage.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (12);

    table.setBounds (r.reduced (6));
}

//==============================================================================
void PluginTagView::rebuildRows()
{
    rows.clear();

    auto terms = juce::StringArray::fromTokens (teFilter.getText().toLowerCase(), " ", {});
    terms.removeEmptyStrings();

    const bool untaggedOnly = tbUntaggedOnly.getToggleState();

    for (const auto& d : scanner.getKnownPlugins().getTypes())
    {
        if (untaggedOnly && scanner.loadTag (d.createIdentifierString()) != FnTag::Unknown)
            continue;

        if (! terms.isEmpty())
        {
            const auto lower = (d.name + " " + d.manufacturerName).toLowerCase();
            bool hit = false;

            for (const auto& t : terms)
                if (lower.contains (t))
                {
                    hit = true;
                    break;
                }

            if (! hit)
                continue;
        }

        rows.add (d);
    }

    table.updateContent();
    table.repaint();
    updateCoverage();
}

void PluginTagView::updateCoverage()
{
    int tagged = 0, total = 0;
    scanner.getTagCoverage (tagged, total);

    lCoverage.setText ("已标 "_u8 + juce::String (tagged) + " / "_u8 + juce::String (total)
                         + "   ·   当前列出 "_u8 + juce::String (rows.size()) + " 个"_u8,
                       juce::dontSendNotification);
}

//==============================================================================
int PluginTagView::getNumRows()
{
    return rows.size();
}

void PluginTagView::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    const auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (2.0f, 1.0f);

    if (look == Look::hardware)
    {
        if (selected)       { g.setColour (hw::vfd.withAlpha (0.12f)); g.fillRect (r); }
        else if (row % 2)   { g.setColour (hw::vfd.withAlpha (0.03f)); g.fillRect (r); }
        return;
    }

    if (selected)
    {
        g.setColour (theme::controlHi.withAlpha (0.55f));
        g.fillRoundedRectangle (r, theme::rRow);
    }
    else if (row % 2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.02f));
        g.fillRoundedRectangle (r, theme::rRow);
    }
}

void PluginTagView::paintCell (juce::Graphics& g, int row, int columnId,
                               int w, int h, bool /*selected*/)
{
    using namespace theme;

    if (! juce::isPositiveAndBelow (row, rows.size()))
        return;

    const auto& d = rows.getReference (row);
    const auto area = juce::Rectangle<int> (0, 0, w, h).reduced (10, 0);

    if (look == Look::hardware)
    {
        paintCellHardware (g, d, columnId, area);
        return;
    }

    switch (columnId)
    {
        case colName:
            g.setFont (font (12.5f, Weight::medium));
            g.setColour (text);
            g.drawText (d.name, area, juce::Justification::centredLeft, true);
            break;

        case colFormat:
        {
            g.setFont (font (10.5f, Weight::medium));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), d.pluginFormatName) + 14.0f;
            const auto pill = area.toFloat().withSizeKeepingCentre (juce::jmin ((float) area.getWidth(), tw), 18.0f)
                                            .withX ((float) area.getX());
            g.setColour (well);
            g.fillRoundedRectangle (pill, 9.0f);
            g.setColour (textDim);
            g.drawText (d.pluginFormatName, pill, juce::Justification::centred, false);
            break;
        }

        case colTag:
        {
            const auto tag = scanner.loadTag (d.createIdentifierString());
            const auto c   = tagColour (tag);
            const auto label = fnTagLabel (tag);

            // 标签做成分类色的胶囊，和信号链上模块的颜色一一对应；
            // 未分类的是虚线框——"这里还没填"，一眼就能扫出剩多少活。
            g.setFont (font (11.5f, Weight::medium));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 34.0f;
            const auto pill = area.toFloat().withSizeKeepingCentre (juce::jmin ((float) area.getWidth(), tw), 22.0f)
                                            .withX ((float) area.getX());

            if (tag == FnTag::Unknown)
            {
                juce::Path outline, dashed;
                outline.addRoundedRectangle (pill.reduced (0.5f), 11.0f);
                const float dashes[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (strokeHi);
                g.fillPath (dashed);
                g.setColour (textFaint);
            }
            else
            {
                g.setColour (tint (c, 0.30f));
                g.fillRoundedRectangle (pill, 11.0f);
                g.setColour (c);
                g.fillEllipse (pill.withWidth (22.0f).reduced (7.5f));
                g.setColour (c.brighter (0.3f));
            }

            g.drawText (label, pill.withTrimmedLeft (18.0f).withTrimmedRight (6.0f), juce::Justification::centred, true);
            drawIcon (g, Icon::chevronDown, juce::Rectangle<float> (pill.getRight() + 4.0f, pill.getCentreY() - 5.0f, 10.0f, 10.0f),
                      textFaint, 1.4f);
            break;
        }

        default:
            break;
    }
}

void PluginTagView::paintCellHardware (juce::Graphics& g, const juce::PluginDescription& d, int columnId,
                                       juce::Rectangle<int> area)
{
    using namespace hw;

    switch (columnId)
    {
        case colName:
            g.setFont (font (12.5f, theme::Weight::medium));
            g.setColour (vfd);
            g.drawText (d.name, area, juce::Justification::centredLeft, true);
            break;

        case colFormat:
            g.setFont (monoFont (11.5f));
            g.setColour (vfdDim.brighter (0.3f));
            g.drawText (d.pluginFormatName, area, juce::Justification::centredLeft, true);
            break;

        case colTag:
        {
            // 标签：功能色的细框 + 字，和机架上模块那条彩色胶带一一对应；未分类是一道虚线
            const auto tag   = scanner.loadTag (d.createIdentifierString());
            const auto c     = theme::tagColour (tag);
            const auto label = fnTagLabel (tag);

            g.setFont (font (11.5f, theme::Weight::medium));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 30.0f;
            const auto box = area.toFloat().withSizeKeepingCentre (juce::jmin ((float) area.getWidth(), tw), 20.0f)
                                            .withX ((float) area.getX());

            if (tag == FnTag::Unknown)
            {
                juce::Path outline, dashed;
                outline.addRectangle (box.reduced (0.5f));
                const float dashes[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (vfdDim);
                g.fillPath (dashed);
                g.setColour (vfdDim.brighter (0.3f));
            }
            else
            {
                g.setColour (c.withAlpha (0.16f));
                g.fillRect (box);
                g.setColour (c.withAlpha (0.8f));
                g.drawRect (box, 1.0f);
                g.fillRect (box.withWidth (5.0f).reduced (0.0f, 4.0f).translated (6.0f, 0.0f));
                g.setColour (c.brighter (0.35f));
            }

            g.drawText (label, box.withTrimmedLeft (16.0f).withTrimmedRight (6.0f), juce::Justification::centred, true);
            theme::drawIcon (g, theme::Icon::chevronDown, juce::Rectangle<float> (box.getRight() + 4.0f, box.getCentreY() - 5.0f, 10.0f, 10.0f),
                             vfdDim, 1.4f);
            break;
        }

        default:
            break;
    }
}

void PluginTagView::cellClicked (int row, int columnId, const juce::MouseEvent&)
{
    // 只有标签列可点。点名字列不该弹菜单——那是"我在看这一行"，不是"我要改它"。
    if (columnId == colTag)
        showTagMenu (row);
}

void PluginTagView::showTagMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, rows.size()))
        return;

    const auto& d = rows.getReference (row);
    const auto id = d.createIdentifierString();
    const auto cur = scanner.loadTag (id);

    juce::PopupMenu m;
    m.addSectionHeader (d.name);

    for (int i = 0; i < kNumFnTags; ++i)
    {
        const auto t = (FnTag) i;
        m.addItem (i + 1, fnTagLabel (t), true, t == cur);
    }

    juce::Component::SafePointer<PluginTagView> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withParentComponent (this),
                     [safe, id] (int choice)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || choice <= 0)
            return;

        self->scanner.saveTag (id, (FnTag) (choice - 1));

        // 「只看未分类」开着的时候，标完这一行就该从列表里消失——
        // 那正是"还剩多少活"的直观反馈，所以整表重建而不是只重画一行。
        self->rebuildRows();
    });
}

void PluginTagView::guessAll()
{
    int filled = 0;

    for (const auto& d : scanner.getKnownPlugins().getTypes())
    {
        const auto id = d.createIdentifierString();

        // **只填未分类的。** 覆盖用户手标过的等于把人工劳动抹掉，
        // 而名字关键词的可信度远不如人眼。
        if (scanner.loadTag (id) != FnTag::Unknown)
            continue;

        if (const auto guess = PluginScanner::guessTag (d.name); guess != FnTag::Unknown)
        {
            scanner.saveTag (id, guess);
            ++filled;
        }
    }

    rebuildRows();

    lCoverage.setText ("按名字猜出 "_u8 + juce::String (filled) + " 个，剩下的要手动标"_u8,
                       juce::dontSendNotification);
}

} // namespace pr
