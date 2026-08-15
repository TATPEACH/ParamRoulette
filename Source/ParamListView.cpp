#include "ParamListView.h"

namespace pr
{

namespace
{
    // 列宽。名字列吃掉剩下的所有空间。
    constexpr int kColCheck  = 26;
    constexpr int kColIndex  = 42;   // 参数序号——它本身就是个筛选手段，不能丢
    constexpr int kColKind   = 54;
    constexpr int kColWas    = 92;   // 上次操作前的值
    constexpr int kColValue  = 92;   // 当前值
    constexpr int kColSpan   = 56;   // 音色层抖动幅度（点开菜单改）
    constexpr int kColNote   = 120;
    constexpr int kNameMin   = 110;

    juce::Colour rowBg (int i, bool isChecked)
    {
        auto base = (i % 2 == 0) ? juce::Colour (0xff2b2b2b) : juce::Colour (0xff303030);
        return isChecked ? base.brighter (0.10f) : base;
    }
}

//==============================================================================
ParamListView::ParamListView (juce::Array<ParamInfo>& modelRef, juce::Array<bool>& checkedRef)
    : model (modelRef), checked (checkedRef)
{
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

ParamListView::~ParamListView()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
}

void ParamListView::resized()
{
    viewport.setBounds (getLocalBounds());
    updateContentSize();
}

void ParamListView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262626));

    if (visible.isEmpty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (14.0f);
        g.drawText (model.isEmpty() ? "没有加载插件"_u8 : "没有匹配的参数"_u8,
                    getLocalBounds(), juce::Justification::centred);
    }
}

//==============================================================================
void ParamListView::rebuild()
{
    recomputeVisible();
    viewport.setViewPosition (0, 0);
    updateContentSize();
    repaint();
}

void ParamListView::refresh()
{
    rows.repaint();
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

    // 勾了"仅匹配"但关键词是空的时候不该把整张表藏光——
    // 用户只是先点了开关还没打字，藏光会让他以为插件挂了。
    const bool filtering = onlyMatching && ! terms.isEmpty();

    for (int i = 0; i < model.size(); ++i)
    {
        const auto& p = model.getReference (i);

        if (filtering && ! matches (p))
            continue;

        if (viewMode == ViewMode::CheckedOnly
            && ! (juce::isPositiveAndBelow (i, checked.size()) && checked.getUnchecked (i)))
            continue;

        if (viewMode == ViewMode::ChangedOnly && ! p.changed)
            continue;

        visible.add (i);
    }
}

void ParamListView::updateContentSize()
{
    const int w = juce::jmax (viewport.getMaximumVisibleWidth(),
                              kColCheck + kColIndex + kNameMin + kColKind
                              + kColWas + kColValue + kColSpan + kColNote);
    rows.setSize (w, juce::jmax (1, visible.size() * kRowH));
}

juce::Range<int> ParamListView::spanColumnX() const noexcept
{
    // 列是从右往左切的（见 paintRow），所以这里也得从右边算回来。
    // 两处共用这一个函数，改列宽时不会错位。
    const int right = rows.getWidth() - kColNote;
    return { right - kColSpan, right };
}

int ParamListView::getNumChecked() const noexcept
{
    int n = 0;
    for (int i : visible)
        if (juce::isPositiveAndBelow (i, checked.size()) && checked.getUnchecked (i))
            ++n;

    return n;
}

//==============================================================================
void ParamListView::checkAll (bool value)
{
    for (int i : visible)
        if (juce::isPositiveAndBelow (i, checked.size()))
            checked.set (i, value);

    rows.repaint();
    if (onChecksChanged != nullptr) onChecksChanged();
}

void ParamListView::invertAll()
{
    for (int i : visible)
        if (juce::isPositiveAndBelow (i, checked.size()))
            checked.set (i, ! checked.getUnchecked (i));

    rows.repaint();
    if (onChecksChanged != nullptr) onChecksChanged();
}

void ParamListView::applyToMatching (bool value)
{
    // 这一个**不**限制在可见行里：关键词就是选择器本身，
    // 「取消匹配」的意思是"凡是叫这个的都别随机"，哪怕它现在被 onlyMatching 藏着。
    if (terms.isEmpty())
        return;

    for (int i = 0; i < model.size(); ++i)
        if (matches (model.getReference (i)) && juce::isPositiveAndBelow (i, checked.size()))
            checked.set (i, value);

    rows.repaint();
    if (onChecksChanged != nullptr) onChecksChanged();
}

void ParamListView::uncheckInert()
{
    for (int i = 0; i < model.size(); ++i)
        if (model.getReference (i).health == Health::Inert
            && juce::isPositiveAndBelow (i, checked.size()))
            checked.set (i, false);

    rows.repaint();
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
    const int mi = visible.getUnchecked (displayRow);
    if (! juce::isPositiveAndBelow (mi, model.size()))
        return;

    const auto& p  = model.getReference (mi);
    const bool  on = juce::isPositiveAndBelow (mi, checked.size()) && checked.getUnchecked (mi);

    g.setColour (rowBg (displayRow, on));
    g.fillRect (area);

    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());

    auto r = area.reduced (4, 0);

    // —— 勾选框
    {
        auto box = r.removeFromLeft (kColCheck).withSizeKeepingCentre (13, 13);
        g.setColour (on ? juce::Colour (0xff5aa9e6) : juce::Colour (0xff6a6a6a));
        g.drawRect (box, 1);

        if (on)
        {
            g.setColour (juce::Colour (0xff5aa9e6));
            g.fillRect (box.reduced (3));
        }
    }

    // 右侧五列先切走，剩下全给名字
    auto note  = r.removeFromRight (kColNote);
    auto spanCol = r.removeFromRight (kColSpan);
    auto value = r.removeFromRight (kColValue);
    auto was   = r.removeFromRight (kColWas);
    auto kind  = r.removeFromRight (kColKind);
    auto idxCol = r.removeFromLeft (kColIndex);

    g.setFont (13.0f);

    // —— 序号。这是筛选的重要线索（fauna 的 eg4 那组就是 40-49），不能只显示名字。
    g.setColour (juce::Colour (0xff7a7a7a));
    g.drawText (juce::String (p.index), idxCol, juce::Justification::centredRight, false);

    // 名字。inert 的行整行标红；没勾的行压暗，但仍然可读——
    // "看不清"和"被排除"是两回事。
    const bool inert = (p.health == Health::Inert);
    g.setColour (inert ? juce::Colour (0xffe06c6c)
                       : (on ? juce::Colours::white : juce::Colour (0xff9a9a9a)));
    g.drawText (p.name, r.reduced (6, 0), juce::Justification::centredLeft, true);

    g.setColour (juce::Colour (0xff8a8a8a));
    g.drawText (kindLabel (p.kind) + (p.kind == Kind::Stepped ? " " + juce::String (p.numSteps) : juce::String()),
                kind, juce::Justification::centredLeft, true);

    // —— 旧值。只有本次真的改动过才显示，否则这一列是噪声。
    if (p.changed)
    {
        g.setColour (juce::Colour (0xff6f6f6f));
        g.drawText (p.prevDisplay, was, juce::Justification::centredLeft, true);
    }

    // —— 当前值。改动过的用亮青色标出来，一眼能扫到本次动了哪些。
    g.setColour (p.blob    ? juce::Colour (0xffd9a441)
               : p.changed ? juce::Colour (0xff6fd3e6)
                           : juce::Colour (0xffb0b0b0));
    g.drawText ((p.changed ? juce::String::fromUTF8 ("→ ") : juce::String()) + p.display,
                value, juce::Justification::centredLeft, true);

    // —— 音色层抖动幅度。设过的用亮色 + 下划虚线暗示"这格能点"，
    // 没设过的显示一个暗淡的 · 表示"用默认档"。
    if (p.hasCustomSpan())
    {
        g.setColour (juce::Colour (0xffc9a0dc));
        g.drawText (juce::String (p.span, 3), spanCol.reduced (4, 0),
                    juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (juce::Colour (0xff5a5a5a));
        g.drawText ("·"_u8, spanCol.reduced (4, 0), juce::Justification::centredRight, false);
    }

    // 备注列：体检结论优先于默认排除理由——
    // "写进去没用"比"我默认没勾它"是更要紧的信息。
    juce::String noteText;
    juce::Colour noteColour { 0xff7a7a7a };

    if (p.health == Health::Inert)             { noteText = healthLabel (p.health); noteColour = juce::Colour (0xffe06c6c); }
    else if (p.health == Health::EndpointOnly) { noteText = healthLabel (p.health); noteColour = juce::Colour (0xffd9a441); }
    else if (p.defaultSkip())                  { noteText = skipReasonLabel (p.skipReason); }
    else if (p.health == Health::Ok)           { noteText = healthLabel (p.health); noteColour = juce::Colour (0xff6aa84f); }

    g.setColour (noteColour);
    g.drawText (noteText, note, juce::Justification::centredLeft, true);
}

//==============================================================================
void ParamListView::rowsMouseDown (const juce::MouseEvent& e)
{
    const int r = rowAt (e.y);
    if (r < 0)
        return;

    const int mi = visible.getUnchecked (r);
    if (! juce::isPositiveAndBelow (mi, checked.size()))
        return;

    // —— 点在「幅度」列上：开菜单改幅度，**不要**顺手把这一行的勾选也翻掉。
    // 想改幅度的时候被连带改了随机范围，是那种"我明明没点勾选框"的恼人 bug。
    if (spanColumnX().contains (e.x))
    {
        showSpanMenu (mi);
        return;
    }

    // 目标值在**按下的那一刻**锁定，整趟拖动都用它。
    // 每行各自取反的话，来回划过同一行会左右横跳，涂不出一条稳定的区间。
    paintValue = ! checked.getUnchecked (mi);
    checked.set (mi, paintValue);

    painting = true;
    dirtyChecks = true;
    lastPaintedRow = r;
    hasDragged = false;
    dragYInViewport = e.getEventRelativeTo (&viewport).y;

    rows.repaint (0, r * kRowH, rows.getWidth(), kRowH);

    // **定时器不在这里开。** 边缘自动滚动只能在真拖动时生效——
    // 在 mouseDown 就开的话，一次静止的单击也会触发滚动 + 整段涂抹。
}

void ParamListView::showSpanMenu (int modelIndex)
{
    if (! juce::isPositiveAndBelow (modelIndex, model.size()))
        return;

    const auto& p = model.getReference (modelIndex);

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
    if (! painting)
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
        if (juce::isPositiveAndBelow (mi, checked.size()))
            checked.set (mi, paintValue);
    }

    lastPaintedRow = r;
    rows.repaint (0, from * kRowH, rows.getWidth(), (to - from + 1) * kRowH);
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
    if (! painting || ! hasDragged)
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
        if (juce::isPositiveAndBelow (mi, checked.size()))
            checked.set (mi, paintValue);
    }

    lastPaintedRow = r;
    rows.repaint();
}

} // namespace pr
