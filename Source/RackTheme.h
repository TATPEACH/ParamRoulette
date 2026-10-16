#pragma once

#include "Theme.h"

namespace pr
{

/** Rack（VCV Rack 风格）皮肤的设计系统。

    **数值大多直接取自 VCV Rack 2 的源码和元件 SVG**，不是凭感觉挑的：
      - 指示灯 / 旋钮配色：`componentlibrary.hpp` 里的 `SCHEME_*`（红 #ED2C24、绿 #90C73E……）
      - 线缆默认五色：`settings.cpp` 的 `resetCables()`（#F3374B 红、#FFB437 黄、#00B56E 绿、#3695EF 蓝、#8B4ADE 紫）
      - 线缆画法：`CableWidget.cpp`——二次贝塞尔，中点往下坠 `(1 − 张力) × (150 + 距离)`，
        端点从插孔中心往坠点方向让 14px，描边 = 颜色 ×0.8、芯 = 颜色 ×0.95，
        整体透明度 = 不透明度^1.5，影子往下多坠 30px、10% 黑
      - 导轨：`Rail.svg` 那七层灰条和 #303030 的机架底色
      - LED 屏：`LedDisplay.cpp`——#222→#121212 的渐变底、#FFD714 的黄字
      - UI 主题（菜单 / 提示 / 浏览器）：`ui/common.cpp` 的暗色主题，底 #202020、字 #F0F0F0
      - 旋钮转角 ±0.83π（微调电位器 ±0.75π），尺寸单位 1 HP = 15 px、一排 3U = 380 px

    和 VCV 一样有一组「视图」设置（菜单栏 → 视图）：线缆不透明度 / 张力、房间亮度、
    光晕、深色面板。**房间亮度**是 Rack 2 的招牌：调暗以后面板、旋钮、线缆都变暗，
    只有指示灯和屏幕还亮着（它们是发光体）。所以这里所有**不发光**的颜色都要过 `shade()`，
    发光的（灯、LED 屏）不过。 */
namespace rk
{
    //== VCV 的配色常量 ===========================================================
    inline const juce::Colour red       { 0xffed2c24 };
    inline const juce::Colour orange    { 0xfff2b120 };
    inline const juce::Colour yellow    { 0xffffd714 };
    inline const juce::Colour green     { 0xff90c73e };
    inline const juce::Colour cyan      { 0xff22e6ef };
    inline const juce::Colour blue      { 0xff29b2ef };
    inline const juce::Colour purple    { 0xffd52bed };

    /** 默认线缆五色（按顺序轮流用，和 Rack 一样）。 */
    juce::Colour cableColour (int index);

    //== 界面（菜单栏 / 菜单 / 提示 / 浏览器）=======================================
    inline const juce::Colour uiBg { 0xff202020 };
    inline const juce::Colour uiFg { 0xfff0f0f0 };

    /** Rack 的主题色都是 bg 到 fg 之间按比例插出来的（`color::lerp (bg, fg, t)`）。 */
    inline juce::Colour ui (float t) { return uiBg.interpolatedWith (uiFg, t); }

    inline const juce::Colour rackBg { 0xff303030 };   ///< 导轨之间露出来的机架底色（Rail.svg）

    //== 面板主题（浅色 / 深色，跟着「视图 → 深色面板」）=============================
    struct PanelTheme
    {
        juce::Colour bg, edge, text, textDim, title, outBox, outText, line;
    };

    const PanelTheme& panel();

    //== 视图设置 ==================================================================
    struct View
    {
        float cableOpacity   = 0.65f;
        float cableTension   = 0.80f;   ///< Rack 官网截图里线缆的垂度大约就是这个张力
        float roomBrightness = 1.00f;
        float halo           = 0.25f;   ///< 指示灯光晕（Rack 的 "Light bloom"），默认 25%
        bool  darkPanels     = false;
    };

    /** 全进程一份（同一个 REAPER 里几个实例看到的是同一个"房间"）。 */
    View& view();

    void loadView (const juce::File&);
    void saveView (const juce::File&);

    /** 房间亮度：不发光的颜色都乘上它（最暗也留一点，不至于全黑看不见旋钮在哪）。 */
    juce::Colour shade (juce::Colour);

    //== 字体 ======================================================================
    /** 模块标题 / 面板上的英文字：几何无衬线（Century Gothic Bold，VCV 面板字的味道）。 */
    juce::Font titleFont (float px);

    /** 面板上的标签。纯 ASCII 走几何字体，有中文走 Noto Sans SC——几何字体没有中文字形。 */
    juce::Font labelFont (const juce::String&, float px);

    /** LED 屏上的字（Rack 用 Share Tech Mono；这里用 Cascadia Mono / Consolas，中文走 Noto）。 */
    juce::Font displayFont (const juce::String&, float px);

    /** 菜单栏 / 浏览器 / 提示的字（和 Grid 同一个中文字族）。 */
    inline juce::Font uiFont (float pt, bool bold = false)
    {
        return theme::font (pt, bold ? theme::Weight::bold : theme::Weight::regular);
    }

    bool isAscii (const juce::String&);

    //== 机架 ======================================================================
    /** 一排导轨：上下两条金属条（带安装孔）+ 中间的机架底色。`row` 是一整排 3U 的范围。 */
    void paintRails (juce::Graphics&, juce::Rectangle<float> row, float z);

    //== 面板 ======================================================================
    void paintPanel (juce::Graphics&, juce::Rectangle<float>, float z);
    void paintScrews (juce::Graphics&, juce::Rectangle<float> panel, float z);
    void paintScrew (juce::Graphics&, juce::Point<float> centre, float z);
    void paintTitle (juce::Graphics&, juce::Rectangle<float> panel, const juce::String&, float z);

    /** 面板上的一个标签。`onBox` = 印在深色的输出框里（反白）。 */
    void paintLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float z,
                     float px = 10.0f, juce::Justification = juce::Justification::centred, bool onBox = false,
                     bool dim = false);

    /** 输出框：VCV 的惯例，输出插孔放在反色的圆角框里，一眼分清进和出。 */
    void paintOutputBox (juce::Graphics&, juce::Rectangle<float>, float z);

    /** 底部的厂牌字（VCV 模块底下的那个 logo 位置）。 */
    void paintBrand (juce::Graphics&, juce::Rectangle<float> panel, const juce::String&, float z);

    /** 一条细分隔线（面板上的分区）。 */
    void paintRule (juce::Graphics&, juce::Point<float> a, juce::Point<float> b, float z);

    //== 元件 ======================================================================
    void paintJack (juce::Graphics&, juce::Point<float> centre, float z);

    enum class Knob
    {
        roundSmall,     ///< RoundSmallBlackKnob
        round,          ///< RoundBlackKnob
        roundLarge,     ///< RoundLargeBlackKnob
        roundHuge,      ///< RoundHugeBlackKnob
        daviesWhite,    ///< Davies1900h 白
        daviesRed,      ///< Davies1900h 红
        trimpot         ///< Trimpot（微调电位器，转角 ±0.75π）
    };

    float knobDiameter (Knob);              ///< 100% 缩放下的直径（px）
    float knobMaxAngle (Knob);              ///< 0.83π 或 0.75π

    /** `angle`：0 = 正上方，顺时针为正（和 VCV 一样）。光影固定，只有盖子上的指示线转。 */
    void paintKnob (juce::Graphics&, juce::Point<float> centre, float diameter, float angle, Knob,
                    bool enabled, bool hover);

    /** 指示灯。brightness 0..1；亮的时候带一圈光晕（大小照 Rack：半径 + min (4r, 15)）。

        光晕的浓淡跟着**底色**走：Rack 的灯是「滤色」叠上去的（`lightColor × (1 − dest) + dest`），
        浅色面板上几乎看不见光晕，深色面板、房间调暗、LED 屏上才亮起来。`onDark` = 灯在
        LED 屏 / 深色界面上（不在面板上）。 */
    void paintLight (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, float brightness,
                     bool onDark = false);

    /** VCVBezel：灰色金属圈里一颗能亮的灯键。 */
    void paintBezel (juce::Graphics&, juce::Point<float> centre, float diameter, juce::Colour light,
                     float brightness, bool pressed, bool hover);

    /** TL1105：黑色小圆键。 */
    void paintTL1105 (juce::Graphics&, juce::Point<float> centre, float diameter, bool pressed, bool hover,
                      bool enabled);

    /** CKSS 拨动开关（竖的）。positions = 2 或 3；position 0 = 最上面。 */
    void paintSwitch (juce::Graphics&, juce::Rectangle<float>, int positions, int position, bool enabled, bool hover);

    /** LED 屏的底（Rack LedDisplay 的渐变 + 上下沿的细线）。屏幕是发光体，不过 shade()。 */
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>, float z);

    /** 一串竖排的电平灯（下绿、上黄、顶红）。level 是线性峰值。 */
    void paintLadder (juce::Graphics&, juce::Rectangle<float> column, int numLeds, float level, float z,
                      bool horizontal = false);

    //== 线缆 ======================================================================
    /** 中点往下坠多少（Rack 的 getSlumpPos）。和 Rack 不同的一点：参与计算的距离封顶 320 px——
        这里机架只有两排，链上留了空位时线会横跨大半个窗口，照原公式会垂进第二排、
        盖住随机化 / 参数表这些天天要点的控件。封顶以后长线是"绷着的"，短线的垂度和 Rack 一样。 */
    juce::Point<float> slumpPoint (juce::Point<float> out, juce::Point<float> in, float z);

    /** 一根线（不含插头）。opacity < 0 = 用视图设置里的不透明度。 */
    void paintCable (juce::Graphics&, juce::Point<float> out, juce::Point<float> in, juce::Colour,
                     float z, float opacity, bool thick);

    /** 插头：染成线的颜色的塑料头 + 金属插孔 + 中间的信号灯。`towards` 是线出去的方向。 */
    void paintPlug (juce::Graphics&, juce::Point<float> pos, juce::Point<float> towards, juce::Colour,
                    float z, float signal);

    //== 杂项 ======================================================================
    /** 选中模块的高亮：Rack 2 的选区就是这个样子（半透明红 + 红边）。 */
    void paintSelection (juce::Graphics&, juce::Rectangle<float>, float z);

    /** 电平（线性）→ 0..1 的灯亮度：-48 dB 起亮，0 dB 满。 */
    float levelToBrightness (float linear);
}

} // namespace pr
