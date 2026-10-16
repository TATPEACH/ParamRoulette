#pragma once

#include "PluginScanner.h"

namespace pr
{

/** 给插件打功能标签的界面。

    **这是"随机效果器链"的数据录入口。** 配方（`modern rumble = 全通 +
    disperser + 修饰性效果器`）要能自动填满，前提是知道每个插件干什么用的，
    而这件事在本机实测下来只有约三成能自动推断出来（见 `FnTag` 的覆盖率表）。
    剩下七成必须人工点，所以这个界面的设计目标就一个：**点得快**。

    为此：
      - 「只看未分类」是默认打开的 —— 已经标好的没必要再看一遍
      - 「按名字猜一遍」先自动盖掉三成，剩下的才需要人工
      - 标签列**点一下就出菜单**，不用先选中行再去别处操作
      - 关键词框可以按厂牌批量收窄（Acustica 那种密码名插件通常一整批同类）

    刻意**不改 JUCE 自带的 `PluginListComponent`**：那是它的组件，加一列要 fork。
*/
class PluginTagView final : public juce::Component,
                            private juce::TableListBoxModel
{
public:
    /** 两套皮肤各一种画法。行为一样，只是 Grid 版是深色点阵画布上的卡片，
        硬件版是藏青面板上的一块荧光屏。 */
    enum class Look { grid, hardware, rack };

    explicit PluginTagView (PluginScanner&, Look = Look::grid);
    ~PluginTagView() override;

    /** 换皮肤时窗口不关，原地换画法。 */
    void setLook (Look);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    //==============================================================================
    // TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int w, int h, bool selected) override;
    void cellClicked (int row, int columnId, const juce::MouseEvent&) override;

    //==============================================================================
    void paintCellHardware (juce::Graphics&, const juce::PluginDescription&, int columnId, juce::Rectangle<int> area);
    void paintCellRack (juce::Graphics&, const juce::PluginDescription&, int columnId, juce::Rectangle<int> area);
    void rebuildRows();
    void updateCoverage();
    void showTagMenu (int row);
    void guessAll();

    enum ColumnId { colName = 1, colFormat, colTag };

    PluginScanner& scanner;
    Look look;

    juce::TableListBox table { "plugins", this };

    juce::TextEditor   teFilter;
    juce::ToggleButton tbUntaggedOnly { "只看未分类"_u8 };
    juce::TextButton   bGuess { "按名字猜一遍"_u8 };
    juce::Label        lCoverage;

    /** 当前显示的行 → PluginDescription。每次筛选变化重建。 */
    juce::Array<juce::PluginDescription> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginTagView)
};

} // namespace pr
