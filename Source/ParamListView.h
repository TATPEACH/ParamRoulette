#pragma once

#include "ParamModel.h"

namespace pr
{

/** 参数列表：拖动涂抹勾选 + 文本 filter + 批量按钮。

    从 ReaScript 的 immediate-mode 搬到 retained-mode 反而更简单——
    ImGui 版每帧都要重算一遍可见行，这里只在 filter 变化时算一次。

    **不持有数据**：model 和 checked 都是 processor 的引用。这样随机化/morph
    改了值以后，界面只要 repaint 就是最新的，不需要来回拷。代价是这个组件
    必须比 processor 先死——editor 本来就是这个生命周期，没问题。
*/
class ParamListView final : public juce::Component,
                            private juce::Timer
{
public:
    ParamListView (juce::Array<ParamInfo>& modelRef, juce::Array<bool>& checkedRef);
    ~ParamListView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** 结构变了（换插件 / 重建模型）：重算可见行，滚回顶部。 */
    void rebuild();

    /** 只是值变了：重画。 */
    void refresh();

    /** 关键词用空格切分，全部转小写；任一 term 是参数名的子串就算匹配。 */
    void setFilter (const juce::String& text, bool onlyMatchingIn);

    /** 显示范围。和关键词 filter 是**两条独立的筛选**，同时生效。 */
    enum class ViewMode
    {
        All,            ///< 全部
        CheckedOnly,    ///< 只看勾选的——回答"我到底勾了哪些"
        ChangedOnly     ///< 只看上次操作真的改动了的
    };

    void setViewMode (ViewMode);
    ViewMode getViewMode() const noexcept { return viewMode; }

    //==============================================================================
    // 批量操作。**作用范围有意不一致**，不是写漏了：
    //   checkAll / invertAll —— 只动**当前可见**的行。筛了关键词时，"全选"的意思
    //                           就是"把我现在看到的这些全选上"，不该殃及看不见的三百个。
    //   applyToMatching      —— 动**全部**匹配关键词的行。关键词本身就是选择器，
    //                           "取消匹配"的意思是"凡是叫这个的都别随机"，
    //                           哪怕它此刻正被「仅匹配」藏着。
    //   uncheckInert         —— 动**全部**体检判定为 Inert 的行，同理。
    void checkAll (bool value);
    void invertAll();
    void applyToMatching (bool value);
    void uncheckInert();

    int getNumVisible() const noexcept  { return visible.size(); }
    int getNumChecked() const noexcept;

    /** 勾选状态被用户改动时触发（拖动结束 / 批量按钮），用来落盘。 */
    std::function<void()> onChecksChanged;

    /** 用户在「幅度」列上点了并选了一个值。参数是**模型下标**和新幅度
        （负数 = 回到角色默认档）。

        为什么是点出菜单而不是在格子里打字：列表是自绘的，塞一个 TextEditor
        进去要自己管焦点、滚动跟随、Esc 取消一大堆事；而实际要填的值就是
        codex 那张表里的几个数（0.01~0.10），一个菜单点起来还更快。 */
    std::function<void (int modelIndex, float span)> onSpanChanged;

private:
    //==============================================================================
    /** 内容组件。只负责把事件转回 owner——所有状态都在 owner 上，
        免得两个类各存一半。 */
    struct Rows final : public juce::Component
    {
        explicit Rows (ParamListView& o) : owner (o) {}
        void paint (juce::Graphics& g) override             { owner.paintRows (g); }
        void mouseDown (const juce::MouseEvent& e) override  { owner.rowsMouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override  { owner.rowsMouseDrag (e); }
        void mouseUp   (const juce::MouseEvent& e) override  { owner.rowsMouseUp (e); }
        ParamListView& owner;
    };

    void paintRows (juce::Graphics&);
    void rowsMouseDown (const juce::MouseEvent&);
    void rowsMouseDrag (const juce::MouseEvent&);
    void rowsMouseUp   (const juce::MouseEvent&);

    void timerCallback() override;      ///< 拖到边缘时自动滚动

    void recomputeVisible();
    void updateContentSize();
    bool matches (const ParamInfo&) const;
    int  rowAt (int yInRows) const noexcept;
    void paintRow (juce::Graphics&, int displayRow, juce::Rectangle<int> area);

    /** 「幅度」列的横向范围（在 rows 坐标系里）。点击命中判定和绘制共用它，
        免得两处各算一遍、改列宽时错位。 */
    juce::Range<int> spanColumnX() const noexcept;
    void showSpanMenu (int modelIndex);

    //==============================================================================
    juce::Array<ParamInfo>& model;
    juce::Array<bool>&      checked;

    juce::Viewport viewport;
    Rows           rows { *this };

    juce::Array<int> visible;           ///< 显示行 → model 下标
    juce::StringArray terms;            ///< 已切分、已小写
    bool onlyMatching = false;
    ViewMode viewMode = ViewMode::All;

    bool painting = false;
    bool paintValue = false;
    int  lastPaintedRow = -1;
    bool dirtyChecks = false;           ///< 这趟拖动确实改过东西吗
    int  dragYInViewport = 0;

    /** 鼠标按下后**真的移动过**了吗。

        边缘自动滚动只能在真拖动时生效。以前它在 mouseDown 就开了定时器，
        而 `dragYInViewport` 还是上一次的残值（初始 0）——0 < kEdgePx 会被判成
        "贴着顶边"，于是往上滚一行、再把顶行到当前行整段涂成同一个值。
        结果是：列表滚下去之后**单击一行会连带刷掉上面一整片**。 */
    bool hasDragged = false;

    static constexpr int kRowH   = 22;
    static constexpr int kEdgePx = 14;  ///< 距上下边缘多近开始自动滚

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamListView)
};

} // namespace pr
