#pragma once

#include "RackTheme.h"
#include "ChainStrip.h"
#include "ParamListView.h"
#include "MorphPad.h"

namespace pr
{

//==============================================================================
/** 一个插件模块（HOST）要画的全部内容。机架上的链位和模块浏览器里的预览都用它，
    所以浏览器里看到的预览就是装上之后在机架里的样子（Rack 的浏览器也是这么做的）。 */
struct HostLook
{
    int          number = 0;          ///< 链位号 1..6；0 = 浏览器预览，不画编号
    juce::String name, brand, tag, program, info;
    juce::Colour tagColour;
    bool active      = true;          ///< false = 旁通
    bool editorOpen  = false;
    bool hasPrograms = false;
    std::vector<float>       bars;    ///< 参数值（已经按桶压缩过），0..1
    std::vector<juce::uint8> flags;   ///< bit0 = 这次改动过，bit1 = 没勾选
    float level = 0.0f;               ///< 这一位处理完之后的峰值（线性）
};

/** 画一个插件模块。r 是整块面板（11 HP × 3U，乘过缩放）。 */
void paintHostModule (juce::Graphics&, juce::Rectangle<float> r, const HostLook&, float z);

//==============================================================================
/** 面板模块：标题、厂牌、螺丝，加上一批"装饰"（印在面板上的字、指示灯、输出框、插孔、分隔线）。
    旋钮 / 按键 / 屏幕是子组件，由 editor 摆在上面。

    装饰都是**数据**——editor 在 resized() 里按缩放重新填一遍，指示灯的亮度随时改，
    只重画那一颗灯的范围。 */
class RackModule : public juce::Component
{
public:
    RackModule (juce::String title, juce::String brand = "ROULETTE");

    void setZoom (float z)        { zoom = z; }
    float getZoom() const noexcept { return zoom; }

    void clearDecor();
    int  addLabel (juce::Rectangle<float>, const juce::String&, float px = 10.0f,
                   juce::Justification = juce::Justification::centred, bool onBox = false, bool dim = false);
    void setLabel (int index, const juce::String&, bool dim = false);
    int  addLed (juce::Point<float>, float radius, juce::Colour);
    void setLed (int index, float brightness);
    void setLedColour (int index, juce::Colour);
    void addBox (juce::Rectangle<float> r)                     { boxes.push_back (r); }
    void addJack (juce::Point<float> p)                        { jacks.push_back (p); }
    void addRule (juce::Point<float> a, juce::Point<float> b)  { rules.push_back ({ a, b }); }

    void paint (juce::Graphics&) override;

protected:
    juce::String title, brand;
    float zoom = 1.0f;

    struct Label { juce::Rectangle<float> r; juce::String text; float px; juce::Justification just; bool onBox, dim; };
    struct Led   { juce::Point<float> p; float radius; juce::Colour colour; float b = 0.0f; };

    std::vector<Label> labels;
    std::vector<Led>   leds;
    std::vector<juce::Rectangle<float>> boxes;
    std::vector<juce::Point<float>>     jacks;
    std::vector<std::pair<juce::Point<float>, juce::Point<float>>> rules;
};

//==============================================================================
/** 第一排：IN → 6 个插件模块 → LIMIT → OUT。

    行为（单击聚焦、点空位装插件、双击开插件界面、右键菜单、旁通、开关安全网、提示）
    全部继承 `ChainStrip`；这里换掉布局和画法，再加上 Rack 特有的三样：

      - **拖模块调顺序**：按住模块拖到别的位置，其余的依次让位（Rack 里拖模块就是这个手感）。
        拖的时候线缆实时跟着走，松手才真的挪（`onMove`）
      - 每个模块上的「界面」键和 program 屏（点一下弹 program 菜单）
      - 每个模块的信号灯、IN 的电平和 MIDI 灯、OUT 的电平 —— 看信号死在了哪一位

    线缆不在这里画：Rack 的线压在所有模块上面（包括下面一排），所以是单独一层（RackCableLayer）。 */
class RackChainRow final : public ChainStrip
{
public:
    RackChainRow() = default;

    void setZoom (float z)  { zoom = z; resized(); repaint(); }

    struct Extra
    {
        juce::String brand, program, info;
        bool hasPrograms = false;
        bool editorOpen  = false;
        std::vector<float>       bars;
        std::vector<juce::uint8> flags;

        bool sameAs (const Extra& o) const
        {
            return brand == o.brand && program == o.program && info == o.info && hasPrograms == o.hasPrograms
                && editorOpen == o.editorOpen && bars == o.bars && flags == o.flags;
        }
    };

    void setExtras (const std::array<Extra, kMaxChain>&);

    /** 电平（线性峰值，已经在外面做过平滑）。只重画灯的那几块。 */
    void setLevels (const std::array<float, kMaxChain>& slots, float inL, float inR, float outL, float outR, float midi);

    /** LIMIT 模块上天花板旋钮的位置（本组件坐标）。 */
    juce::Rectangle<int> ceilingKnobArea() const;

    struct Cable
    {
        juce::Point<float> out, in;
        int   colourIndex = 0;
        float signal = 0.0f;           ///< 插头上那颗信号灯的亮度
        bool  highlight = false;       ///< 连着鼠标所在的模块：画成不透明（Rack 也是这样）
    };

    /** 当前（含拖动预览）的全部线缆，本组件坐标。 */
    std::vector<Cable> cables() const;

    std::function<void()>              onCablesChanged;
    std::function<void (int, int)>     onMove;          ///< 拖完松手：from → to
    std::function<void (int)>          onProgramMenu;   ///< 点了某个模块的 program 屏
    std::function<void (int)>          onEditToggle;    ///< 点了某个模块的「界面」键

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    bool isDraggingModule() const noexcept { return dragActive; }

    /** 第 slot 个插件模块在本组件里的位置（不含拖动预览）。 */
    juce::Rectangle<float> moduleArea (int slot) const   { return rects[(size_t) juce::jlimit (0, kMaxChain - 1, slot)]; }

private:
    juce::Rectangle<float> powerRect (juce::Rectangle<float> module) const override;
    juce::Rectangle<float> editRect (juce::Rectangle<float> module) const;
    juce::Rectangle<float> programRect (juce::Rectangle<float> module) const;
    juce::Rectangle<float> sigRect (juce::Rectangle<float> module) const;

    /** 拖动时每个链位**显示**在哪（其余的已经让好位），没在拖就是 rects。 */
    std::array<juce::Rectangle<float>, kMaxChain> displayRects() const;

    juce::Point<float> hostIn  (juce::Rectangle<float> m) const { return { m.getX() + 32.0f * zoom, m.getY() + 334.0f * zoom }; }
    juce::Point<float> hostOut (juce::Rectangle<float> m) const { return { m.getRight() - 34.0f * zoom, m.getY() + 334.0f * zoom }; }

    void paintInModule (juce::Graphics&);
    void paintOutModule (juce::Graphics&);
    void paintLimiter (juce::Graphics&);
    HostLook lookFor (int slot) const;

    float zoom = 1.0f;
    juce::Rectangle<float> inModule, outModule;
    std::array<Extra, kMaxChain> extras;
    std::array<float, kMaxChain> slotLevel {};
    float inL = 0, inR = 0, outL = 0, outR = 0, midi = 0;

    int   pressSlot = -1;
    bool  dragActive = false;
    float dragDx = 0.0f;
    int   dropIndex = -1;
    juce::Point<float> pressPos;
};

//==============================================================================
/** 线缆层：盖在两排模块上面、不接鼠标，只画线和插头（Rack 的线就是压在一切上面的）。 */
class RackCableLayer final : public juce::Component
{
public:
    explicit RackCableLayer (RackChainRow& r) : row (r) { setInterceptsMouseClicks (false, false); }

    void setZoom (float z) { zoom = z; }
    void paint (juce::Graphics&) override;

    /** 只重画插头那几块（信号灯在变）。 */
    void repaintPlugs();

private:
    RackChainRow& row;
    float zoom = 1.0f;
};

//==============================================================================
/** 参数列表的 LED 屏版：黑底、黄色的数值、勾选是一颗绿灯。交互全是 ParamListView 的。 */
class RackParamList final : public ParamListView
{
public:
    RackParamList();

    void setZoom (float z);
    void paint (juce::Graphics&) override;

private:
    void paintRow (juce::Graphics&, int displayRow, juce::Rectangle<int> area) override;
    void paintHeader (juce::Graphics&, juce::Rectangle<int> area) override;

    float zoom = 1.0f;
};

//==============================================================================
/** morph 的示波器版（Rack 的 Scope 模块那种黑屏 + 彩色迹线）。拖动 / 双击回中 / 权重语义
    全部是 MorphPad 的。四个快照用线缆的前四种颜色（红黄绿蓝），和快照模块上的灯一致。 */
class RackScope final : public MorphPad,
                        public juce::SettableTooltipClient
{
public:
    explicit RackScope (const SnapshotBank&);

    void setZoom (float z) { zoom = z; repaint(); }
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> padArea() const override;
    void paintSpot (juce::Graphics&, juce::Point<float>, float radius, juce::Colour, float intensity);

    float zoom = 1.0f;
};

} // namespace pr
