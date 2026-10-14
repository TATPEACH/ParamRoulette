#pragma once

#include "Theme.h"
#include <array>

namespace pr
{

/** 分段选择器：胶囊形凹槽里一排选项，选中的那段凸起来。
    取代界面上那几个只有两三个选项的下拉框（1D/2D、全部/勾选/改动）——
    下拉框要点两下、而且看不见别的选项是什么；分段控件一眼全看到、点一下就选。

    每段可以带一个小图形（`glyph`）代替文字，运动语义那六个曲线图标就是这么画的。 */
class SegmentedControl : public juce::Component,
                         public juce::TooltipClient
{
public:
    struct Item
    {
        juce::String text;
        juce::String tooltip;
        std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> glyph;
    };

    SegmentedControl();

    void setItems (std::vector<Item>);
    void setSelected (int index, juce::NotificationType = juce::dontSendNotification);
    int  getSelected() const noexcept { return selected; }
    void setAccent (juce::Colour c) { accent = c; repaint(); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    juce::Rectangle<float> segment (int i) const;
    int indexAt (juce::Point<float>) const;

    std::vector<Item> items;
    int selected = 0, hover = -1;
    juce::Colour accent = theme::accent;
};

//==============================================================================
/** 整数步进器：`[ − ]  不限  [ + ]`。滚轮也能调，双击数字归零。

    取代 JUCE 的 IncDecButtons 样式——那个控件把文本框塞在左边、两个按钮挤在右边，
    在上一版界面里就是 "0 [-][...]" 那一坨。 */
class Stepper : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    Stepper();

    void setRange (int lo, int hi)            { minV = lo; maxV = hi; setValue (value); }
    void setValue (int v, juce::NotificationType = juce::dontSendNotification);
    int  getValue() const noexcept            { return value; }

    std::function<juce::String (int)> textFor;
    std::function<void (int)>         onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> minusArea() const;
    juce::Rectangle<float> plusArea() const;
    void step (int delta);

    int minV = 0, maxV = 64, value = 0;
    int hover = 0;          ///< -1 = 减号，+1 = 加号，0 = 都不是
    float wheelAcc = 0.0f;  ///< 触控板会给很多很小的滚动量，攒够一格再走
};

//==============================================================================
/** 四个快照方块（2×2）。

    交互从上一版的"每个快照三个按钮（存 / 召回 / ×）"收成一个方块：
      - **空的方块**：点一下 = 存进来
      - **有内容的方块**：点一下 = 召回；悬停时右上角浮出「覆盖」「清空」两个小按钮
      - **右键**：完整菜单（存入 / 召回 / 清空）
    十二个按钮变成四个方块，而且"这个快照有没有存"一眼就能看出来。

    键盘：1–4 召回，Shift+1–4 存入（在 editor 里处理）。 */
class SnapshotTiles : public juce::Component,
                      public juce::TooltipClient
{
public:
    struct Tile
    {
        bool         used = false;
        juce::String name;
        int          numSlots = 0;    ///< 这份快照里有几个链位的状态
    };

    SnapshotTiles();

    void setTiles (const std::array<Tile, 4>&, int winner, bool canStore);

    std::function<void (int)> onRecall, onStore, onClear;

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum class Hit { none, tile, overwrite, clear };

    juce::Rectangle<float> tileRect (int i) const;
    juce::Rectangle<float> overwriteRect (int i) const;
    juce::Rectangle<float> clearRect (int i) const;
    std::pair<int, Hit> hitTest (juce::Point<float>) const;
    void showMenu (int i);

    std::array<Tile, 4> tiles;
    int  winner   = -1;
    bool canStore = false;
    int  hoverTile = -1;
    Hit  hoverHit  = Hit::none;
};

} // namespace pr
