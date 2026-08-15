#include "PluginTagView.h"

namespace pr
{

PluginTagView::PluginTagView (PluginScanner& s)
    : scanner (s)
{
    table.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff262626));
    table.setRowHeight (22);
    table.setHeaderHeight (24);

    auto& h = table.getHeader();
    h.addColumn ("插件"_u8,   colName,   300, 120, -1, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("格式"_u8,   colFormat,  70,  50, -1, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("功能标签"_u8, colTag,   160, 100, -1, juce::TableHeaderComponent::defaultFlags);

    addAndMakeVisible (table);

    teFilter.setTextToShowWhenEmpty ("按名字筛选（空格分隔）"_u8, juce::Colour (0xff707070));
    teFilter.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible (teFilter);

    // 默认只看未分类：已经标好的没必要再翻一遍。七百个插件里
    // 真正需要人工处理的就是这一批。
    tbUntaggedOnly.setToggleState (true, juce::dontSendNotification);
    tbUntaggedOnly.onClick = [this] { rebuildRows(); };
    addAndMakeVisible (tbUntaggedOnly);

    bGuess.setTooltip ("按插件名里的关键词猜一轮。只能盖住约三成，"_u8
                       "而且**只填未分类的**，不会覆盖你手动标过的。"_u8);
    bGuess.onClick = [this] { guessAll(); };
    addAndMakeVisible (bGuess);

    lCoverage.setFont (juce::FontOptions (12.0f));
    lCoverage.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    lCoverage.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (lCoverage);

    rebuildRows();
}

PluginTagView::~PluginTagView() = default;

void PluginTagView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}

void PluginTagView::resized()
{
    auto r = getLocalBounds().reduced (8);

    {
        auto row = r.removeFromTop (26);
        bGuess.setBounds (row.removeFromRight (110));
        row.removeFromRight (8);
        tbUntaggedOnly.setBounds (row.removeFromRight (100));
        row.removeFromRight (8);
        teFilter.setBounds (row);
    }

    r.removeFromTop (4);
    lCoverage.setBounds (r.removeFromBottom (18));
    r.removeFromBottom (4);

    table.setBounds (r);
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
    juce::ignoreUnused (w, h);

    if (selected)
        g.fillAll (juce::Colour (0xff3d6b7d));
    else if (row % 2)
        g.fillAll (juce::Colour (0xff2b2b2b));
}

void PluginTagView::paintCell (juce::Graphics& g, int row, int columnId,
                               int w, int h, bool /*selected*/)
{
    if (! juce::isPositiveAndBelow (row, rows.size()))
        return;

    const auto& d = rows.getReference (row);
    const auto area = juce::Rectangle<int> (0, 0, w, h).reduced (6, 0);

    g.setFont (juce::FontOptions (13.0f));

    switch (columnId)
    {
        case colName:
            g.setColour (juce::Colours::white);
            g.drawText (d.name, area, juce::Justification::centredLeft, true);
            break;

        case colFormat:
            g.setColour (juce::Colour (0xff8a8a8a));
            g.drawText (d.pluginFormatName, area, juce::Justification::centredLeft, true);
            break;

        case colTag:
        {
            const auto tag = scanner.loadTag (d.createIdentifierString());

            // 未分类的画暗，标过的画亮 —— 扫一眼就知道还剩多少活
            g.setColour (tag == FnTag::Unknown ? juce::Colour (0xff6a6a6a)
                                               : juce::Colour (0xffc9a0dc));
            g.drawText (fnTagLabel (tag), area, juce::Justification::centredLeft, true);
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
