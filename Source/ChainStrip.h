#pragma once

#include "Theme.h"
#include "ChainDefs.h"
#include <array>

namespace pr
{

/** 信号链：IN → 6 个链位模块 → 输出保护 → OUT，模块之间用 Grid 风格的连线串起来。

    这是界面上最"Grid"的一块，也是交互最多的一块：

      - **点模块** = 聚焦这个链位（所有操作都切到它身上）
      - **点空位** = 聚焦并直接弹出插件菜单装一个进来
      - **点模块右上角的电源键** = 旁通
      - **双击模块** = 打开这个插件自己的界面
      - **右键模块** = 菜单（打开界面 / 更换插件 / 旁通 / 卸载）
      - **点输出保护的电源键** = 开关链尾 limiter

    连线的颜色跟着**信号**走：从 IN 出来是红色（Grid 的惯例：音频线是红的），
    每经过一个装了插件、没旁通的模块就染成那个模块的分类色。所以给插件打了功能标签之后，
    一眼就能看出"信号现在被谁处理过"。空位和旁通的模块里画一道淡淡的直通线。

    组件只画、只报事件，不碰 processor——数据由 editor 每次刷新时塞进来。 */
class ChainStrip : public juce::Component,
                   public juce::TooltipClient
{
public:
    struct Slot
    {
        bool         loaded = false;
        bool         bypassed = false;
        juce::String name;
        juce::String tag;             ///< 第一行：功能标签（「相位 / disperser」），用分类色写
        juce::String detail;          ///< 第三行：格式 · 参数数
        juce::Colour colour;          ///< 分类色
        bool operator== (const Slot& o) const
        {
            return loaded == o.loaded && bypassed == o.bypassed && name == o.name
                && tag == o.tag && detail == o.detail && colour == o.colour;
        }
    };

    struct Guard
    {
        bool  on = true;
        float grDb = 0.0f;
        float ceilingDb = -1.0f;
        bool  nanWarn = false;
        bool operator== (const Guard& o) const
        {
            return on == o.on && std::abs (grDb - o.grDb) < 0.05f
                && std::abs (ceilingDb - o.ceilingDb) < 0.01f && nanWarn == o.nanWarn;
        }
    };

    ChainStrip();

    void setState (const std::array<Slot, kMaxChain>&, int focused, const Guard&);

    std::function<void (int)>       onFocus;
    std::function<void (int)>       onAdd;          ///< 点了空位
    std::function<void (int, bool)> onBypass;
    std::function<void (int)>       onOpenEditor;   ///< 双击模块
    std::function<void (int)>       onContextMenu;  ///< 右键模块
    std::function<void (bool)>      onGuardToggle;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

protected:
    // 硬件版（HwChainRack）是子类：它换掉 resized() / paint() 和开关键的位置，
    // 点击判定、悬停、提示文字都用这里的。
    enum class Part { none, body, power, guardBody, guardPower };

    virtual juce::Rectangle<float> powerRect (juce::Rectangle<float> module) const;
    std::pair<int, Part>   hitTest (juce::Point<float>) const;

    void paintModule (juce::Graphics&, int i);
    void paintGuard  (juce::Graphics&);

    std::array<Slot, kMaxChain>                   slots;
    std::array<juce::Colour, kMaxChain>           inColour;   ///< 进入每个模块时的信号颜色
    std::array<juce::Rectangle<float>, kMaxChain> rects;
    juce::Rectangle<float> guardRect;
    juce::Point<float>     inNode, outNode;
    int   focused = 0;
    Guard guard;

    int  hoverIndex = -1;
    Part hoverPart  = Part::none;
};

} // namespace pr
