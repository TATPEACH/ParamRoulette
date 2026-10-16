#pragma once

#include "RackPanels.h"
#include "PluginScanner.h"

namespace pr
{

/** Rack 的模块浏览器（Enter 或者点空位打开）：盖在整个界面上的一块深色面板，
    顶上一排筛选（搜索 / 厂牌 / 功能标签 / 排序 / 缩放），下面是每个插件的**预览**——
    预览就是它装进机架以后的样子（Rack 的浏览器也是直接画模块面板）。

    比原来的插件菜单多出来的：
      - 搜索按名字 / 厂牌 / 格式，空格分隔任一命中
      - **按功能标签筛**：标签窗口里攒下来的数据第一次真正派上用场——
        想找一个"全通"或者"失真"，点一下就只剩那一类
      - 右键预览直接改它的功能标签（不用再去开标签窗口）
      - 排序里有「随机」：这个插件本来就叫 ParamRoulette

    键盘：打开时光标就在搜索框里，回车装第一个结果，Esc 关掉。 */
class RackBrowser final : public juce::Component
{
public:
    explicit RackBrowser (PluginScanner&);
    ~RackBrowser() override;

    /** 打开，目标是链位 `chainSlot`。`currentId` = 那一位现在装着的插件（预览上会标出来）。 */
    void open (int chainSlot, const juce::String& currentId);

    std::function<void (int chainSlot, const juce::PluginDescription&)> onChoose;
    std::function<void()> onClose;
    std::function<void()> onScan;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    enum class Sort { name, brand, tag, random };

private:
    struct Entry
    {
        juce::PluginDescription desc;
        juce::String id, brand, haystack;   ///< haystack：搜索用的小写拼接串
        FnTag tag = FnTag::Unknown;
    };

    class Grid;

    void reload();          ///< 从插件表重新读（标签可能改了）
    void rebuild();         ///< 按当前筛选 / 排序重算
    void choose (const Entry&);
    void close();
    juce::Rectangle<int> panelArea() const;
    void showBrandMenu();
    void showTagMenu();
    void showSortMenu();
    void showZoomMenu();
    void showEntryMenu (const Entry&);
    void updateButtons();

    PluginScanner& scanner;
    int targetSlot = 0;
    juce::String currentId;

    juce::TextEditor search;
    juce::TextButton bBrand, bTag, bReset, bSort, bZoom, bScan, bClose;
    juce::Viewport viewport;
    std::unique_ptr<Grid> grid;

    std::vector<Entry> all, shown;
    juce::String brandFilter;           ///< 空 = 全部
    int  tagFilter = -1;                ///< -1 = 全部，其余 = (int) FnTag
    Sort sort = Sort::name;
    float zoom = 0.5f;
    juce::int64 randomSeed = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackBrowser)
};

} // namespace pr
