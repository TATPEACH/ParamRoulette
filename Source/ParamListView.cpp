#include "ParamListView.h"

namespace pr
{

using theme::Weight;
using theme::Icon;

//==============================================================================
ParamListView::ParamListView()
{
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

ParamListView::~ParamListView()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
}

void ParamListView::setSource (juce::Array<ParamInfo>& m, juce::Array<bool>& c)
{
    if (model == &m && checked == &c)
        return;

    // 换了数据源，拖动 / 悬停的中间状态都作废——它们记的是旧列表里的行号。
    stopTimer();
    painting = false;
    hasDragged = false;
    lastPaintedRow = -1;
    hoverRow = -1;

    model = &m;
    checked = &c;
}

void ParamListView::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (kHeaderH));
    updateContentSize();
}

//==============================================================================
ParamListView::Cols ParamListView::layoutCols (juce::Rectangle<int> row)
{
    // **按宽度取舍列**：窗口缩到最小（1100 宽）时中间这一栏只剩 500 来像素，
    // 全部列硬塞进去的话名字列会被挤成零宽——上一版截图里真出现过"一列名字都没有"。
    //
    // 必留：勾选 / 序号 / 名字（至少 110）/ 当前值 / 幅度。
    // 可选，按重要程度依次加：类型 → 备注 → 旧值。旧值最先丢：当前值那一栏的
    // 值条上已经有旧值的刻度了，丢了它信息损失最小。
    auto r = row.reduced (6, 0);

    Cols c;
    c.check = r.removeFromLeft (28);
    c.index = r.removeFromLeft (36);

    constexpr int nameMin = 110, valueW = 100, spanW = 56, kindW = 58, noteW = 84, wasW = 76;

    int budget = r.getWidth() - (nameMin + valueW + spanW);
    const bool showKind = budget >= kindW;  if (showKind) budget -= kindW;
    const bool showNote = budget >= noteW;  if (showNote) budget -= noteW;
    const bool showWas  = budget >= wasW;

    if (showNote) c.note = r.removeFromRight (noteW);
    c.span  = r.removeFromRight (spanW);
    c.value = r.removeFromRight (valueW);
    if (showWas)  c.was  = r.removeFromRight (wasW);
    if (showKind) c.kind = r.removeFromRight (kindW);
    c.name  = r.withTrimmedLeft (6);
    return c;
}

juce::Rectangle<int> ParamListView::headerCheckArea() const
{
    return layoutCols ({ 0, 0, viewport.getMaximumVisibleWidth(), kHeaderH }).check;
}

void ParamListView::paint (juce::Graphics& g)
{
    using namespace theme;

    paintHeader (g, getLocalBounds().removeFromTop (kHeaderH));

    if (! visible.isEmpty())
        return;

    // 空状态：一个大一点的图标 + 一句告诉用户下一步该干嘛的话
    const auto area = getLocalBounds().withTrimmedTop (kHeaderH).toFloat();
    const bool noPlugin = model == nullptr || model->isEmpty();

    const auto icon = juce::Rectangle<float> (26.0f, 26.0f).withCentre ({ area.getCentreX(), area.getCentreY() - 18.0f });
    drawIcon (g, noPlugin ? Icon::plus : Icon::search, icon, textFaint, 1.8f);

    g.setColour (textDim);
    g.setFont (font (13.0f, Weight::medium));
    g.drawText (noPlugin ? "没有加载插件"_u8 : "没有符合筛选的参数"_u8,
                area.withTop (icon.getBottom() + 8.0f).withHeight (20.0f), juce::Justification::centred, false);

    g.setColour (textFaint);
    g.setFont (font (11.5f));
    g.drawText (noPlugin ? "点上面信号链里的空位，装一个插件进来"_u8 : "换个关键词，或者把显示范围切回「全部」"_u8,
                area.withTop (icon.getBottom() + 30.0f).withHeight (18.0f), juce::Justification::centred, false);
}

void ParamListView::paintHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    using namespace theme;

    const auto cols = layoutCols (area.withWidth (viewport.getMaximumVisibleWidth()));

    // 全选圆圈：三态（全勾 / 部分 / 全没勾），只看**可见**的行——和「全选」按钮同一个范围
    {
        const int nChecked = getNumChecked();
        const int nVisible = visible.size();
        const auto box = cols.check.toFloat().withSizeKeepingCentre (15.0f, 15.0f);

        if (nVisible > 0 && nChecked == nVisible)
        {
            g.setColour (accent);
            g.fillEllipse (box);
            drawIcon (g, Icon::check, box.reduced (2.6f), ink, 1.8f);
        }
        else if (nChecked > 0)
        {
            g.setColour (tint (accent, 0.45f));
            g.fillEllipse (box);
            g.setColour (accent);
            g.drawEllipse (box.reduced (0.5f), 1.3f);
            g.fillRoundedRectangle (box.withSizeKeepingCentre (7.0f, 2.0f), 1.0f);
        }
        else
        {
            g.setColour (strokeHi.brighter (0.15f));
            g.drawEllipse (box.reduced (0.5f), 1.4f);
        }
    }

    g.setFont (font (11.0f, Weight::medium));
    g.setColour (textFaint.brighter (0.15f));

    g.drawText ("#",            cols.index, juce::Justification::centredRight, false);
    g.drawText ("参数"_u8,      cols.name,  juce::Justification::centredLeft,  false);
    g.drawText ("类型"_u8,      cols.kind,  juce::Justification::centredLeft,  false);
    g.drawText ("旧值"_u8,      cols.was,   juce::Justification::centredLeft,  false);
    g.drawText ("当前值"_u8,    cols.value, juce::Justification::centredLeft,  false);
    g.drawText ("幅度"_u8,      cols.span,  juce::Justification::centred,      false);
    g.drawText ("备注"_u8,      cols.note.withTrimmedLeft (8), juce::Justification::centredLeft, false);

    g.setColour (stroke);
    g.fillRect (area.getX() + 6, area.getBottom() - 1, area.getWidth() - 12, 1);
}

void ParamListView::mouseDown (const juce::MouseEvent& e)
{
    // 只有表头那一行会落到这里（下面的行都在 Rows 里）。
    if (e.y >= kHeaderH || ! headerCheckArea().expanded (4, 0).contains (e.getPosition()))
        return;

    // 已经全勾 → 全不勾；否则 → 全勾。范围是**可见行**，和「全选」按钮一致。
    checkAll (! (visible.size() > 0 && getNumChecked() == visible.size()));
    repaint();
}

//==============================================================================
void ParamListView::rebuild()
{
    recomputeVisible();
    viewport.setViewPosition (0, 0);
    hoverRow = -1;
    updateContentSize();
    repaint();
}

void ParamListView::refresh()
{
    rows.repaint();
    repaint (getLocalBounds().removeFromTop (kHeaderH));    // 表头那个三态圆圈也跟着变
}

void ParamListView::setFilter (const juce::String& text, bool onlyMatchingIn)
{
    terms.clear();
    for (const auto& t : juce::StringArray::fromTokens (text.toLowerCase(), " ", {}))
        if (t.isNotEmpty())
            terms.add (t);

    onlyMatching = onlyMatchingIn;

    recomputeVisible();
    updateContentSize();
    repaint();
}

bool ParamListView::matches (const ParamInfo& p) const
{
    for (const auto& t : terms)
        if (p.lowerName.contains (t))
            return true;

    return false;
}

void ParamListView::setViewMode (ViewMode m)
{
    if (viewMode == m)
        return;

    viewMode = m;
    recomputeVisible();
    viewport.setViewPosition (0, 0);
    updateContentSize();
    repaint();
}

void ParamListView::recomputeVisible()
{
    visible.clearQuick();

    if (model == nullptr)
        return;

    // 勾了"仅匹配"但关键词是空的时候不该把整张表藏光——
    // 用户只是先点了开关还没打字，藏光会让他以为插件挂了。
    const bool filtering = onlyMatching && ! terms.isEmpty();

    for (int i = 0; i < model->size(); ++i)
    {
        const auto& p = model->getReference (i);

        if (filtering && ! matches (p))
            continue;

        if (viewMode == ViewMode::CheckedOnly && ! isChecked (i))
            continue;

        if (viewMode == ViewMode::ChangedOnly && ! p.changed)
            continue;

        visible.add (i);
    }
}

void ParamListView::updateContentSize()
{
    // 不做横向滚动：名字列吃剩下的宽度，窄了就省略号。横向滚动的表格在插件窗口里
    // 很难用（表头还得跟着滚），而且这几列本来就排得下。
    rows.setSize (juce::jmax (1, viewport.getMaximumVisibleWidth()),
                  juce::jmax (1, visible.size() * kRowH));
}

int ParamListView::getNumChecked() const noexcept
{
    int n = 0;
    for (int i : visible)
        if (isChecked (i))
            ++n;

    return n;
}

//==============================================================================
void ParamListView::checkAll (bool value)
{
    if (checked == nullptr)
        return;

    for (int i : visible)
        if (juce::isPositiveAndBelow (i, checked->size()))
            checked->set (i, value);

    refresh();
    if (onChecksChanged != nullptr) onChecksChanged();
}

void ParamListView::invertAll()
{
    if (checked == nullptr)
        return;

    for (int i : visible)
        if (juce::isPositiveAndBelow (i, checked->size()))
            checked->set (i, ! checked->getUnchecked (i));

    refresh();
    if (onChecksChanged != nullptr) onChecksChanged();
}

void ParamListView::applyToMatching (bool value)
{
    // 这一个**不**限制在可见行里：关键词就是选择器本身，
    // 「取消匹配」的意思是"凡是叫这个的都别随机"，哪怕它现在被 onlyMatching 藏着。
    if (terms.isEmpty() || model == nullptr || checked == nullptr)
        return;

    for (int i = 0; i < model->size(); ++i)
        if (matches (model->getReference (i)) && juce::isPositiveAndBelow (i, checked->size()))
            checked->set (i, value);

    refresh();
    if (onChecksChanged != nullptr) onChecksChanged();
}

void ParamListView::uncheckInert()
{
    if (model == nullptr || checked == nullptr)
        return;

    for (int i = 0; i < model->size(); ++i)
        if (model->getReference (i).health == Health::Inert
            && juce::isPositiveAndBelow (i, checked->size()))
            checked->set (i, false);

    refresh();
    if (onChecksChanged != nullptr) onChecksChanged();
}

//==============================================================================
int ParamListView::rowAt (int yInRows) const noexcept
{
    if (yInRows < 0)
        return -1;

    const int r = yInRows / kRowH;
    return juce::isPositiveAndBelow (r, visible.size()) ? r : -1;
}

void ParamListView::paintRows (juce::Graphics& g)
{
    const auto clip = g.getClipBounds();
    const int first = juce::jmax (0, clip.getY() / kRowH);
    const int last  = juce::jmin (visible.size() - 1, clip.getBottom() / kRowH);

    for (int r = first; r <= last; ++r)
        paintRow (g, r, { 0, r * kRowH, rows.getWidth(), kRowH });
}

void ParamListView::paintRow (juce::Graphics& g, int displayRow, juce::Rectangle<int> area)
{
    using namespace theme;

    if (model == nullptr)
        return;

    const int mi = visible.getUnchecked (displayRow);
    if (! juce::isPositiveAndBelow (mi, model->size()))
        return;

    const auto& p  = model->getReference (mi);
    const bool  on = isChecked (mi);
    const auto  cols = layoutCols (area);

    // —— 底：隔行一道极淡的条纹，悬停时一块圆角高亮
    if (displayRow % 2 == 1)
    {
        g.setColour (juce::Colours::white.withAlpha (0.018f));
        g.fillRect (area.reduced (4, 0));
    }

    if (displayRow == hoverRow)
    {
        g.setColour (controlHi.withAlpha (0.42f));
        g.fillRoundedRectangle (area.reduced (4, 2).toFloat(), rRow);
    }

    // 本次改动过的行：左边一道强调色短条，扫一眼就知道动了哪些
    if (p.changed)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) area.getX() + 4.0f, (float) area.getCentreY() - 7.0f,
                                                        3.0f, 14.0f), 1.5f);
    }

    // —— 勾选：Bitwig 式的圆形勾
    {
        const auto box = cols.check.toFloat().withSizeKeepingCentre (15.0f, 15.0f);
        if (on)
        {
            g.setColour (accent);
            g.fillEllipse (box);
            drawIcon (g, Icon::check, box.reduced (2.6f), ink, 1.8f);
        }
        else
        {
            g.setColour (strokeHi.brighter (0.15f));
            g.drawEllipse (box.reduced (0.5f), 1.4f);
        }
    }

    // —— 序号。这是筛选的重要线索（fauna 的 eg4 那组就是 40-49），不能只显示名字。
    g.setFont (font (11.5f));
    g.setColour (textFaint.brighter (0.1f));
    g.drawText (juce::String (p.index), cols.index, juce::Justification::centredRight, false);

    // —— 名字。写不进去的整行标红；没勾的压暗，但仍然可读——"看不清"和"被排除"是两回事。
    const bool inert = (p.health == Health::Inert);
    g.setFont (font (12.5f, Weight::medium));
    g.setColour (inert ? danger : (on ? text : textDim));
    g.drawText (p.name, cols.name, juce::Justification::centredLeft, true);

    // —— 类型：小胶囊。开关 / 档位 / 连续三种颜色分开，扫一眼就知道这一列是什么性质。
    // （窗口窄的时候这一列会被 layoutCols 丢掉，矩形是空的。）
    if (! cols.kind.isEmpty())
    {
        juce::String label = kindLabel (p.kind);
        if (p.kind == Kind::Stepped)
            label << " " << p.numSteps;

        const auto c = p.kind == Kind::Toggle  ? green
                     : p.kind == Kind::Stepped ? blue
                                               : textFaint;

        g.setFont (font (10.5f, Weight::medium));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 14.0f;
        const auto pill = cols.kind.toFloat().withSizeKeepingCentre (juce::jmin ((float) cols.kind.getWidth() - 4.0f, tw), 17.0f)
                                             .withX ((float) cols.kind.getX());
        g.setColour (p.kind == Kind::Continuous ? well : tint (c, 0.30f));
        g.fillRoundedRectangle (pill, 8.5f);
        g.setColour (p.kind == Kind::Continuous ? textDim : c.brighter (0.35f));
        g.drawFittedText (label, pill.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    // —— 旧值。只有本次真的改动过才显示，否则这一列是噪声。
    if (p.changed && ! cols.was.isEmpty())
    {
        g.setFont (font (11.5f));
        g.setColour (textFaint.brighter (0.2f));
        g.drawText (p.prevDisplay, cols.was.withTrimmedRight (6), juce::Justification::centredLeft, true);
    }

    // —— 当前值 + 底下一道值条。值条让"动了多少"一眼可见，不用比两串数字。
    {
        auto vr = cols.value;
        const auto valueBar = juce::Rectangle<float> ((float) vr.getX(), (float) vr.getBottom() - 6.0f,
                                                 (float) vr.getWidth() - 10.0f, 2.0f);
        const auto vc = p.blob ? warn : (p.changed ? accent : text);

        g.setFont (font (12.0f, p.changed ? Weight::bold : Weight::regular));
        g.setColour (vc);
        g.drawText (p.display, vr.withTrimmedBottom (4).withTrimmedRight (8), juce::Justification::centredLeft, true);

        if (! p.blob)
        {
            g.setColour (well);
            g.fillRoundedRectangle (valueBar, 1.0f);
            g.setColour ((p.changed ? accent : textFaint).withAlpha (p.changed ? 0.9f : 0.7f));
            g.fillRoundedRectangle (valueBar.withWidth (juce::jmax (2.0f, valueBar.getWidth() * juce::jlimit (0.0f, 1.0f, p.value))), 1.0f);

            if (p.changed)
            {
                // 旧值的位置：一个小竖刻度
                const float px = valueBar.getX() + valueBar.getWidth() * juce::jlimit (0.0f, 1.0f, p.prevValue);
                g.setColour (textDim);
                g.fillRect (juce::Rectangle<float> (px - 0.75f, valueBar.getY() - 2.0f, 1.5f, 6.0f));
            }
        }
    }

    // —— 音色层抖动幅度。设过的是一枚紫色胶囊，没设过是一个暗淡的点。
    {
        const bool hov = (displayRow == hoverRow) && hoverSpan;

        if (p.hasCustomSpan())
        {
            const auto label = juce::String::fromUTF8 ("\xc2\xb1") + juce::String (p.span, 3);
            g.setFont (font (10.5f, Weight::medium));
            const auto pill = cols.span.toFloat().withSizeKeepingCentre ((float) cols.span.getWidth() - 4.0f, 17.0f);
            g.setColour (tint (purple, hov ? 0.45f : 0.30f));
            g.fillRoundedRectangle (pill, 8.5f);
            g.setColour (purple.brighter (0.35f));
            g.drawFittedText (label, pill.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
        else
        {
            if (hov)
            {
                g.setColour (control.withAlpha (0.8f));
                g.fillRoundedRectangle (cols.span.toFloat().withSizeKeepingCentre ((float) cols.span.getWidth() - 4.0f, 17.0f), 8.5f);
            }

            g.setFont (font (12.0f, Weight::bold));
            g.setColour (hov ? textDim : textFaint);
            g.drawText (hov ? "+" : juce::String::fromUTF8 ("\xc2\xb7"), cols.span,
                        juce::Justification::centred, false);
        }
    }

    // —— 备注：体检结论优先于默认排除理由——"写进去没用"比"我默认没勾它"是更要紧的信息。
    if (! cols.note.isEmpty())
    {
        juce::String noteText;
        juce::Colour dot = juce::Colours::transparentBlack;

        if (p.health == Health::Inert)             { noteText = healthLabel (p.health); dot = danger; }
        else if (p.health == Health::EndpointOnly) { noteText = healthLabel (p.health); dot = warn; }
        else if (p.defaultSkip())                  { noteText = skipReasonLabel (p.skipReason); }
        else if (p.health == Health::Ok)           { noteText = healthLabel (p.health); dot = ok; }

        auto nr = cols.note.withTrimmedLeft (8);

        if (! dot.isTransparent())
        {
            g.setColour (dot);
            g.fillEllipse (nr.removeFromLeft (7).toFloat().withSizeKeepingCentre (6.0f, 6.0f));
            nr.removeFromLeft (5);
        }

        g.setFont (font (11.0f));
        g.setColour (dot.isTransparent() ? textFaint.brighter (0.15f) : dot.brighter (0.2f));
        g.drawText (noteText, nr, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
void ParamListView::rowsMouseMove (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    const bool overSpan = r >= 0 && layoutCols ({ 0, 0, rows.getWidth(), kRowH }).span.getHorizontalRange().contains (e.x);

    if (r != hoverRow || overSpan != hoverSpan)
    {
        if (hoverRow >= 0) rows.repaint (0, hoverRow * kRowH, rows.getWidth(), kRowH);
        hoverRow  = r;
        hoverSpan = overSpan;
        if (hoverRow >= 0) rows.repaint (0, hoverRow * kRowH, rows.getWidth(), kRowH);
    }

    rows.setMouseCursor (overSpan ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ParamListView::rowsMouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0)
        rows.repaint (0, hoverRow * kRowH, rows.getWidth(), kRowH);

    hoverRow = -1;
    hoverSpan = false;
}

void ParamListView::rowsMouseDown (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    if (r < 0 || checked == nullptr)
        return;

    const int mi = visible.getUnchecked (r);
    if (! juce::isPositiveAndBelow (mi, checked->size()))
        return;

    // —— 点在「幅度」列上：开菜单改幅度，**不要**顺手把这一行的勾选也翻掉。
    // 想改幅度的时候被连带改了随机范围，是那种"我明明没点勾选框"的恼人 bug。
    if (layoutCols ({ 0, 0, rows.getWidth(), kRowH }).span.getHorizontalRange().contains (e.x))
    {
        showSpanMenu (mi);
        return;
    }

    // 目标值在**按下的那一刻**锁定，整趟拖动都用它。
    // 每行各自取反的话，来回划过同一行会左右横跳，涂不出一条稳定的区间。
    paintValue = ! checked->getUnchecked (mi);
    checked->set (mi, paintValue);

    painting = true;
    dirtyChecks = true;
    lastPaintedRow = r;
    hasDragged = false;
    dragYInViewport = e.getEventRelativeTo (&viewport).y;

    rows.repaint (0, r * kRowH, rows.getWidth(), kRowH);
    repaint (getLocalBounds().removeFromTop (kHeaderH));

    // **定时器不在这里开。** 边缘自动滚动只能在真拖动时生效——
    // 在 mouseDown 就开的话，一次静止的单击也会触发滚动 + 整段涂抹。
}

void ParamListView::showSpanMenu (int modelIndex)
{
    if (model == nullptr || ! juce::isPositiveAndBelow (modelIndex, model->size()))
        return;

    const auto& p = model->getReference (modelIndex);

    // 菜单里这几个值就是 codex 在 bird 5 上试听得出的那张表的取值范围
    // （tens ±0.02~0.04 / lp.f ±0.015~0.03 / mem ±0.04~0.08 / n.lp ±0.05~0.10）。
    static constexpr float kPresets[] = { 0.01f, 0.015f, 0.02f, 0.03f, 0.05f, 0.08f, 0.10f, 0.20f };

    juce::PopupMenu m;
    m.addSectionHeader (p.name + "  —  音色层抖动幅度"_u8);
    m.addItem (1, "用角色默认档"_u8, true, ! p.hasCustomSpan());
    m.addSeparator();

    for (int i = 0; i < (int) std::size (kPresets); ++i)
    {
        const float v = kPresets[i];
        m.addItem (10 + i, "±"_u8 + juce::String (v, 3),
                   true, p.hasCustomSpan() && std::abs (p.span - v) < 1.0e-4f);
    }

    m.addSeparator();
    m.addItem (99, "实际幅度 = 这个数 × 音色强度"_u8, false);

    juce::Component::SafePointer<ParamListView> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rows),
                     [safe, modelIndex] (int choice)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || choice <= 0 || choice == 99)
            return;

        const float span = (choice == 1) ? -1.0f : kPresets[choice - 10];

        if (self->onSpanChanged != nullptr)
            self->onSpanChanged (modelIndex, span);

        self->repaint();
    });
}

void ParamListView::rowsMouseDrag (const juce::MouseEvent& e)
{
    if (! painting || checked == nullptr)
        return;

    dragYInViewport = e.getEventRelativeTo (&viewport).y;

    if (! hasDragged)
    {
        hasDragged = true;
        startTimerHz (30);      // 真动了才开始考虑自动滚
    }

    const int r = rowAt (e.y);
    if (r < 0 || r == lastPaintedRow)
        return;

    // 快速拖动时鼠标事件会跳行，只涂当前行会留下空洞。补齐中间的。
    const int from = juce::jmin (r, lastPaintedRow);
    const int to   = juce::jmax (r, lastPaintedRow);

    for (int i = from; i <= to; ++i)
    {
        const int mi = visible.getUnchecked (i);
        if (juce::isPositiveAndBelow (mi, checked->size()))
            checked->set (mi, paintValue);
    }

    lastPaintedRow = r;
    rows.repaint (0, from * kRowH, rows.getWidth(), (to - from + 1) * kRowH);
    repaint (getLocalBounds().removeFromTop (kHeaderH));
}

void ParamListView::rowsMouseUp (const juce::MouseEvent&)
{
    stopTimer();
    painting = false;
    hasDragged = false;
    lastPaintedRow = -1;

    if (dirtyChecks)
    {
        dirtyChecks = false;
        if (onChecksChanged != nullptr) onChecksChanged();
    }
}

void ParamListView::timerCallback()
{
    if (! painting || ! hasDragged || checked == nullptr)
    {
        stopTimer();
        return;
    }

    const int h = viewport.getViewHeight();
    int delta = 0;

    if (dragYInViewport < kEdgePx)              delta = -kRowH;
    else if (dragYInViewport > h - kEdgePx)     delta =  kRowH;

    if (delta == 0)
        return;

    const int before = viewport.getViewPositionY();
    viewport.setViewPosition (viewport.getViewPositionX(),
                              juce::jlimit (0, juce::jmax (0, rows.getHeight() - h), before + delta));

    if (viewport.getViewPositionY() == before)
        return;     // 已经到头了，别再涂

    // 滚过去以后把新露出来的行也涂上，否则用户得反复来回蹭。
    const int y = viewport.getViewPositionY() + juce::jlimit (0, h - 1, dragYInViewport);
    const int r = rowAt (y);

    if (r < 0)
        return;

    const int from = (lastPaintedRow < 0) ? r : juce::jmin (r, lastPaintedRow);
    const int to   = (lastPaintedRow < 0) ? r : juce::jmax (r, lastPaintedRow);

    for (int i = from; i <= to; ++i)
    {
        const int mi = visible.getUnchecked (i);
        if (juce::isPositiveAndBelow (mi, checked->size()))
            checked->set (mi, paintValue);
    }

    lastPaintedRow = r;
    rows.repaint();
}

} // namespace pr
