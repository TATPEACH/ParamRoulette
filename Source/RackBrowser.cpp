#include "RackBrowser.h"
#include "GridLookAndFeel.h"

namespace pr
{

namespace
{
    constexpr float kMargin = 12.0f;

    juce::String allLabel() { return "全部"_u8; }
}

//==============================================================================
/** 预览网格。只画看得见的那几行——七百多个插件全画一遍，滚一下就卡。 */
class RackBrowser::Grid final : public juce::Component,
                                public juce::TooltipClient
{
public:
    explicit Grid (RackBrowser& o) : owner (o) { setRepaintsOnMouseActivity (false); }

    float cellW() const { return juce::jmax (11.0f * 15.0f * owner.zoom + 18.0f, 118.0f); }
    float cellH() const { return 380.0f * owner.zoom + 46.0f; }

    int columns() const { return juce::jmax (1, (int) (((float) getWidth() - kMargin) / cellW())); }

    void updateSize (int width)
    {
        const int cols = juce::jmax (1, (int) (((float) width - kMargin) / cellW()));
        const int rows = ((int) owner.shown.size() + cols - 1) / cols;
        setSize (width, juce::jmax (1, (int) (kMargin + (float) rows * cellH() + kMargin)));
    }

    juce::Rectangle<float> cell (int i) const
    {
        const int cols = columns();
        const float x0 = ((float) getWidth() - (float) cols * cellW()) * 0.5f;
        return { x0 + (float) (i % cols) * cellW(), kMargin + (float) (i / cols) * cellH(), cellW(), cellH() };
    }

    juce::Rectangle<float> panelIn (juce::Rectangle<float> c) const
    {
        const float pw = 11.0f * 15.0f * owner.zoom, ph = 380.0f * owner.zoom;
        return { c.getCentreX() - pw * 0.5f, c.getY() + 4.0f, pw, ph };
    }

    int indexAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) owner.shown.size(); ++i)
        {
            const auto c = cell (i);
            if (c.getY() > p.y)
                break;
            if (c.contains (p))
                return i;
        }
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto clip = g.getClipBounds().toFloat();
        const float z = owner.zoom;

        for (int i = 0; i < (int) owner.shown.size(); ++i)
        {
            const auto c = cell (i);
            if (c.getBottom() < clip.getY())
                continue;
            if (c.getY() > clip.getBottom())
                break;

            const auto& e = owner.shown[(size_t) i];
            const auto  p = panelIn (c);
            const bool  hov = i == hover;
            const bool  cur = e.id == owner.currentId;

            // 预览下面的影子（Rack 的 ModelBox 用 box gradient 画的那种）
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), p.getCentreX(), p.getCentreY(),
                                                     juce::Colours::transparentBlack, p.getRight() + 10.0f, p.getCentreY(), true));
            g.fillRect (p.expanded (8.0f));

            HostLook L;
            L.name      = e.desc.name;
            L.brand     = e.brand;
            L.tag       = e.tag == FnTag::Unknown ? juce::String ("未分类"_u8) : fnTagLabel (e.tag);
            L.tagColour = theme::tagColour (e.tag);
            L.info      = e.desc.pluginFormatName;
            paintHostModule (g, p, L, z);

            if (hov || cur)
            {
                g.setColour (cur ? rk::yellow : juce::Colours::white.withAlpha (0.8f));
                g.drawRect (p.expanded (2.0f), 2.0f);
            }

            auto text = juce::Rectangle<float> (c.getX() + 2.0f, p.getBottom() + 6.0f, c.getWidth() - 4.0f, 16.0f);
            g.setColour (rk::uiFg);
            g.setFont (rk::uiFont (11.5f, hov));
            g.drawText (e.desc.name, text, juce::Justification::centred, true);

            g.setColour (cur ? rk::yellow : rk::ui (0.5f));
            g.setFont (rk::uiFont (10.0f));
            g.drawText (cur ? "当前装着的"_u8 : (e.brand.isNotEmpty() ? e.brand : e.desc.pluginFormatName),
                        text.translated (0.0f, 15.0f), juce::Justification::centred, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = indexAt (e.position);
        if (h != hover)
        {
            if (hover >= 0) repaint (cell (hover).expanded (4.0f).getSmallestIntegerContainer());
            hover = h;
            if (hover >= 0) repaint (cell (hover).expanded (4.0f).getSmallestIntegerContainer());
        }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hover >= 0)
            repaint (cell (hover).expanded (4.0f).getSmallestIntegerContainer());
        hover = -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.position);
        if (i < 0)
            return;

        const auto entry = owner.shown[(size_t) i];     // 拷一份：回调里可能重建 shown

        if (e.mods.isPopupMenu())
            owner.showEntryMenu (entry);
        else
            owner.choose (entry);
    }

    juce::String getTooltip() override
    {
        if (! juce::isPositiveAndBelow (hover, (int) owner.shown.size()))
            return {};

        const auto& e = owner.shown[(size_t) hover];
        return e.desc.name + "\n"
             + (e.brand.isNotEmpty() ? e.brand + "  ·  "_u8 : juce::String()) + e.desc.pluginFormatName
             + "  ·  "_u8 + (e.tag == FnTag::Unknown ? juce::String ("未分类"_u8) : fnTagLabel (e.tag))
             + "\n\n点一下装到链位 "_u8 + juce::String (owner.targetSlot + 1) + "，右键改它的功能标签"_u8;
    }

    int hover = -1;

private:
    RackBrowser& owner;
};

//==============================================================================
RackBrowser::RackBrowser (PluginScanner& s)
    : scanner (s)
{
    setWantsKeyboardFocus (true);

    search.getProperties().set ("prSearch", true);
    search.setIndents (28, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.setFont (rk::uiFont (13.0f));
    search.setTextToShowWhenEmpty ("搜索插件 / 厂牌（空格分隔）"_u8, juce::Colour (0xff4a4a4a));
    search.onTextChange = [this] { rebuild(); };
    search.onReturnKey  = [this]
    {
        if (! shown.empty())
            choose (shown.front());
    };
    search.onEscapeKey  = [this] { close(); };
    addAndMakeVisible (search);

    for (auto* b : { &bBrand, &bTag, &bReset, &bSort, &bZoom, &bScan, &bClose })
        addAndMakeVisible (*b);

    bBrand.onClick = [this] { showBrandMenu(); };
    bTag  .onClick = [this] { showTagMenu(); };
    bSort .onClick = [this] { showSortMenu(); };
    bZoom .onClick = [this] { showZoomMenu(); };
    bReset.setButtonText ("重置筛选"_u8);
    bReset.onClick = [this]
    {
        brandFilter.clear();
        tagFilter = -1;
        search.clear();
        rebuild();
    };
    bScan.setButtonText ("扫描插件…"_u8);
    bScan.onClick = [this] { if (onScan != nullptr) onScan(); };
    ui::icon (bClose, theme::Icon::close);
    bClose.setTooltip ("关掉（Esc）"_u8);
    bClose.onClick = [this] { close(); };

    grid = std::make_unique<Grid> (*this);
    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

RackBrowser::~RackBrowser()
{
    viewport.setViewedComponent (nullptr, false);
}

void RackBrowser::open (int chainSlot, const juce::String& id)
{
    targetSlot = chainSlot;
    currentId  = id;
    reload();
    setVisible (true);
    toFront (true);
    viewport.setViewPosition (0, 0);
    search.grabKeyboardFocus();
    search.selectAll();
}

void RackBrowser::close()
{
    setVisible (false);
    if (onClose != nullptr)
        onClose();
}

void RackBrowser::choose (const Entry& e)
{
    const auto desc = e.desc;
    const int slot  = targetSlot;
    setVisible (false);

    if (onChoose != nullptr)
        onChoose (slot, desc);
}

//==============================================================================
void RackBrowser::reload()
{
    all.clear();

    for (const auto& d : scanner.getKnownPlugins().getTypes())
    {
        Entry e;
        e.desc     = d;
        e.id       = d.createIdentifierString();
        e.brand    = d.manufacturerName.trim();
        e.tag      = scanner.loadTag (e.id);
        e.haystack = (d.name + " " + e.brand + " " + d.pluginFormatName).toLowerCase();
        all.push_back (std::move (e));
    }

    rebuild();
}

void RackBrowser::rebuild()
{
    auto terms = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", {});
    terms.removeEmptyStrings();

    shown.clear();

    for (const auto& e : all)
    {
        if (brandFilter.isNotEmpty() && e.brand != brandFilter)
            continue;
        if (tagFilter >= 0 && (int) e.tag != tagFilter)
            continue;

        bool hit = terms.isEmpty();
        for (const auto& t : terms)
            if (e.haystack.contains (t)) { hit = true; break; }

        if (hit)
            shown.push_back (e);
    }

    auto byName = [] (const Entry& a, const Entry& b) { return a.desc.name.compareIgnoreCase (b.desc.name) < 0; };

    switch (sort)
    {
        case Sort::name:  std::stable_sort (shown.begin(), shown.end(), byName); break;
        case Sort::brand: std::stable_sort (shown.begin(), shown.end(), [&] (const Entry& a, const Entry& b)
                          { const int c = a.brand.compareIgnoreCase (b.brand); return c != 0 ? c < 0 : byName (a, b); }); break;
        case Sort::tag:   std::stable_sort (shown.begin(), shown.end(), [&] (const Entry& a, const Entry& b)
                          { return a.tag != b.tag ? (int) a.tag < (int) b.tag : byName (a, b); }); break;
        case Sort::random:
        {
            juce::Random r (randomSeed);
            for (int i = (int) shown.size() - 1; i > 0; --i)
                std::swap (shown[(size_t) i], shown[(size_t) r.nextInt (i + 1)]);
            break;
        }
    }

    grid->hover = -1;
    updateButtons();
    resized();
    repaint();
}

void RackBrowser::updateButtons()
{
    bBrand.setButtonText ("厂牌："_u8 + (brandFilter.isEmpty() ? allLabel() : brandFilter));
    bTag  .setButtonText ("标签："_u8 + (tagFilter < 0 ? allLabel() : fnTagLabel ((FnTag) tagFilter)));

    const juce::String sortNames[] = { "名称"_u8, "厂牌"_u8, "功能标签"_u8, "随机"_u8 };
    bSort.setButtonText ("排序："_u8 + sortNames[(int) sort]);
    bZoom.setButtonText ("缩放："_u8 + juce::String (juce::roundToInt (zoom * 100.0f)) + "%");

    bReset.setEnabled (brandFilter.isNotEmpty() || tagFilter >= 0 || search.getText().isNotEmpty());
    bScan.setVisible (all.empty());
}

//==============================================================================
void RackBrowser::showBrandMenu()
{
    std::map<juce::String, int> counts;
    for (const auto& e : all)
        ++counts[e.brand.isEmpty() ? juce::String() : e.brand];

    juce::PopupMenu m;
    m.addItem ("全部厂牌"_u8 + "  (" + juce::String ((int) all.size()) + ")", true, brandFilter.isEmpty(),
               [this] { brandFilter.clear(); rebuild(); });
    m.addSeparator();

    for (const auto& [brand, n] : counts)
    {
        if (brand.isEmpty())
            continue;
        const auto b = brand;
        m.addItem (b + "  (" + juce::String (n) + ")", true, b == brandFilter, [this, b] { brandFilter = b; rebuild(); });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bBrand));
}

void RackBrowser::showTagMenu()
{
    std::array<int, kNumFnTags> counts {};
    for (const auto& e : all)
        ++counts[(size_t) e.tag];

    juce::PopupMenu m;
    m.addItem ("全部标签"_u8, true, tagFilter < 0, [this] { tagFilter = -1; rebuild(); });
    m.addSeparator();

    for (int i = 0; i < kNumFnTags; ++i)
    {
        const auto t = (FnTag) i;
        const auto label = (t == FnTag::Unknown ? juce::String ("未分类"_u8) : fnTagLabel (t))
                         + "  (" + juce::String (counts[(size_t) i]) + ")";

        juce::PopupMenu::Item item (label);
        item.setTicked (tagFilter == i);
        item.setEnabled (counts[(size_t) i] > 0);
        item.setColour (counts[(size_t) i] > 0 ? theme::tagColour (t).brighter (0.2f) : juce::Colours::transparentBlack);
        item.setAction ([this, i] { tagFilter = i; rebuild(); });
        m.addItem (std::move (item));
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bTag));
}

void RackBrowser::showSortMenu()
{
    juce::PopupMenu m;
    const std::pair<Sort, juce::String> items[] = { { Sort::name, "名称"_u8 }, { Sort::brand, "厂牌"_u8 },
                                                    { Sort::tag, "功能标签"_u8 }, { Sort::random, "随机（每次都不一样）"_u8 } };
    for (const auto& [s, t] : items)
    {
        const auto so = s;
        m.addItem (t, true, sort == so, [this, so]
        {
            sort = so;
            if (so == Sort::random)
                randomSeed = juce::Time::currentTimeMillis();
            rebuild();
        });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bSort));
}

void RackBrowser::showZoomMenu()
{
    juce::PopupMenu m;
    for (const float z : { 0.35f, 0.5f, 0.7f })
        m.addItem (juce::String (juce::roundToInt (z * 100.0f)) + "%", true, std::abs (zoom - z) < 0.01f,
                   [this, z] { zoom = z; rebuild(); });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bZoom));
}

void RackBrowser::showEntryMenu (const Entry& e)
{
    juce::PopupMenu tags;
    for (int i = 0; i < kNumFnTags; ++i)
    {
        const auto t  = (FnTag) i;
        const auto id = e.id;
        tags.addItem (t == FnTag::Unknown ? juce::String ("未分类"_u8) : fnTagLabel (t), true, t == e.tag, [this, id, t]
        {
            scanner.saveTag (id, t);
            reload();
        });
    }

    juce::PopupMenu m;
    m.addSectionHeader (e.desc.name);
    const auto entry = e;
    m.addItem ("装到链位 "_u8 + juce::String (targetSlot + 1), [this, entry] { choose (entry); });
    m.addSubMenu ("功能标签"_u8, tags);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (grid.get()).withMousePosition());
}

//==============================================================================
juce::Rectangle<int> RackBrowser::panelArea() const
{
    return getLocalBounds().reduced (juce::jmin (28, getWidth() / 30), juce::jmin (24, getHeight() / 30));
}

void RackBrowser::resized()
{
    auto p = panelArea().reduced (14, 12);

    auto title = p.removeFromTop (26);
    bClose.setBounds (title.removeFromRight (28));

    p.removeFromTop (6);
    auto row = p.removeFromTop (30);
    search.setBounds (row.removeFromLeft (juce::jmin (260, row.getWidth() / 4)));
    row.removeFromLeft (10);
    bBrand.setBounds (row.removeFromLeft (180));
    row.removeFromLeft (8);
    bTag.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (8);
    bReset.setBounds (row.removeFromLeft (90));

    bZoom.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    bSort.setBounds (row.removeFromRight (140));

    p.removeFromTop (10);
    viewport.setBounds (p);
    grid->updateSize (p.getWidth() - viewport.getScrollBarThickness());

    bScan.setBounds (juce::Rectangle<int> (140, 32).withCentre (p.getCentre().translated (0, 40)));
}

void RackBrowser::paint (juce::Graphics& g)
{
    // Rack 打开浏览器时把整个机架压暗，浏览器本身是一块 #202020 的圆角面板
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    const auto p = panelArea().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (p, 8.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 24, { 0, 6 }).drawForPath (g, shape);
    g.setColour (rk::uiBg);
    g.fillPath (shape);
    g.setColour (rk::ui (0.12f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    auto title = panelArea().reduced (14, 12).removeFromTop (26);
    g.setColour (rk::uiFg);
    g.setFont (rk::uiFont (15.0f, true));
    g.drawText ("插件浏览器"_u8, title, juce::Justification::centredLeft, false);

    g.setColour (rk::ui (0.55f));
    g.setFont (rk::uiFont (12.0f));
    g.drawText ("→ 装到链位 "_u8 + juce::String (targetSlot + 1) + "     "_u8 + juce::String ((int) shown.size())
                    + " / "_u8 + juce::String ((int) all.size()) + " 个插件"_u8,
                title.withTrimmedLeft (110).withTrimmedRight (40), juce::Justification::centredLeft, true);

    if (shown.empty())
    {
        const auto area = viewport.getBounds().toFloat();
        g.setColour (rk::uiFg);
        g.setFont (rk::uiFont (15.0f, true));
        g.drawText (all.empty() ? "插件表是空的"_u8 : "没有符合筛选的插件"_u8,
                    area.withHeight (24.0f).withCentre (area.getCentre().translated (0.0f, -20.0f)), juce::Justification::centred, false);
        g.setColour (rk::ui (0.55f));
        g.setFont (rk::uiFont (12.5f));
        g.drawText (all.empty() ? "先扫描一次，把插件所在的文件夹加进去"_u8 : "换个关键词，或者点「重置筛选」"_u8,
                    area.withHeight (20.0f).withCentre (area.getCentre().translated (0.0f, 6.0f)), juce::Justification::centred, false);
    }
}

void RackBrowser::mouseDown (const juce::MouseEvent& e)
{
    // 点面板外面 = 关掉（Rack 也是点浏览器外面就收起）
    if (! panelArea().contains (e.getPosition()))
        close();
}

bool RackBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }

    return false;
}

} // namespace pr
