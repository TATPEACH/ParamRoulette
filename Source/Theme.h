#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Utf8.h"

namespace pr
{
enum class FnTag;
enum class MotionSemantic;

/** 界面的设计系统：配色、字体、圆角、以及几样到处都要画的东西（端口、连线、图标）。

    **视觉语言照着 Bitwig 的 The Grid 来**——深色画布上的圆角模块、模块标题用
    分类色、白色旋钮面配彩色数值弧、空心圆环端口、平滑的贝塞尔连线、橙色当强调色。
    下面那些色值不是凭感觉挑的，是从 Bitwig 官网和用户手册的 Grid 截图上逐像素取样的
    （画布 #151515、模块 #2A2A2A、强调橙 #FF7F17，以及模块调色板那九个色）。

    和 Bitwig 原版的一处刻意偏离：**圆角更大**。Bitwig 自己的模块圆角只有 3~4px，
    按需求要更圆润，所以卡片 14px、模块 12px、按钮一律做成胶囊形。

    所有界面代码只从这里取颜色和字体。散落在各处的 `juce::Colour (0xff...)`
    是上一版界面"像 Win95"的根源之一——同一个灰色写了七八种，没法统一调。
*/
namespace theme
{
    //== 底色层级 =================================================================
    inline const juce::Colour canvas     { 0xff151515 };   ///< 画布（Grid 截图取样）
    inline const juce::Colour canvasDot  { 0xff242424 };   ///< 画布上的点阵
    inline const juce::Colour bar        { 0xff1e1e1e };   ///< 顶栏 / 状态栏
    inline const juce::Colour module     { 0xff2a2a2a };   ///< 卡片 / 模块本体
    inline const juce::Colour moduleHi   { 0xff323232 };   ///< 悬停 / 聚焦的模块本体
    inline const juce::Colour well       { 0xff1c1c1c };   ///< 凹槽：输入框、滑轨、pad 底
    inline const juce::Colour control    { 0xff3a3a3a };   ///< 按钮本体
    inline const juce::Colour controlHi  { 0xff474747 };   ///< 按钮悬停
    inline const juce::Colour stroke     { 0xff363636 };   ///< 分隔线 / 描边
    inline const juce::Colour strokeHi   { 0xff4a4a4a };

    inline const juce::Colour text       { 0xffe8e8e8 };
    inline const juce::Colour textDim    { 0xff9c9c9c };
    inline const juce::Colour textFaint  { 0xff626262 };
    inline const juce::Colour knobFace   { 0xffe5e5e5 };   ///< Grid 大旋钮的白色面（取样 #E5E5E5）
    inline const juce::Colour ink        { 0xff181818 };   ///< 亮底上的深色字 / 指针

    //== Bitwig 模块调色板（Grid inspector 里那排色块，逐个取样）==================
    inline const juce::Colour orange     { 0xffff7f17 };
    inline const juce::Colour red        { 0xffff1c40 };
    inline const juce::Colour yellow     { 0xffffde42 };
    inline const juce::Colour lime       { 0xffbbdd21 };
    inline const juce::Colour green      { 0xff4abc00 };
    inline const juce::Colour cyan       { 0xff1be3e5 };
    inline const juce::Colour blue       { 0xff5ca8ee };
    inline const juce::Colour purple     { 0xffc36eff };
    inline const juce::Colour pink       { 0xffff5fa2 };
    inline const juce::Colour teal       { 0xff3bcb9a };
    inline const juce::Colour lavender   { 0xff9f86ff };

    //== 语义色 ===================================================================
    inline const juce::Colour accent     = orange;   ///< 主强调：随机化、聚焦、勾选
    inline const juce::Colour motion     = cyan;     ///< 运动语义那一块
    inline const juce::Colour morph      = blue;     ///< 快照 / morph 那一块
    inline const juce::Colour warn       { 0xffffb02e };
    inline const juce::Colour danger     { 0xffff4d5e };
    inline const juce::Colour ok         { 0xff6fcf5a };
    inline const juce::Colour audio      { 0xffe8475f };   ///< Grid 惯例：音频线是红的

    //== 圆角 / 尺寸 ===============================================================
    inline constexpr float rCard   = 14.0f;
    inline constexpr float rModule = 12.0f;
    inline constexpr float rWell   = 10.0f;
    inline constexpr float rRow    = 7.0f;
    inline constexpr int   gap     = 12;      ///< 卡片之间、卡片内外边距

    //== 字体 =====================================================================
    enum class Weight { regular, medium, bold };

    /** UI 字体族。优先 Noto Sans SC（中英文都好看，小字号也清楚），
        机器上没装就退回 Microsoft YaHei UI。只在第一次调用时查一次。 */
    const juce::String& family();

    /** 按**点高**（em 大小）取字体，而不是 JUCE 的"字体高度"。

        区别很要紧：CJK 字体的 ascent+descent 普遍是 em 的 1.4 倍以上，
        按 JUCE 高度给 13 的话 Noto Sans SC 实际只有 9px 大，比英文字体小一圈。
        按点高给，换字体时字号观感不变。 */
    juce::Font font (float pointHeight, Weight w = Weight::regular);

    //== 分类色 ===================================================================
    /** 插件功能标签 → 模块颜色。链条上的模块、连线、标签窗口都用它。 */
    juce::Colour tagColour (FnTag);

    /** 四个快照槽的颜色。pad 角标、快照方块、权重读数共用。 */
    juce::Colour slotColour (int slot);

    //== 绘制 =====================================================================
    /** 画布点阵。只画 clip 区域内的点——界面每 50ms 刷一次，整窗重画点阵很浪费。 */
    void fillDotGrid (juce::Graphics&, juce::Rectangle<int> area, int spacing = 22);

    /** 一张卡片（模块）：圆角本体 + 柔和投影 + 顶边一道很淡的高光。 */
    void drawCard (juce::Graphics&, juce::Rectangle<float>, float radius = rCard,
                   juce::Colour fill = module, bool shadow = true);

    /** 卡片标题：一个分类色的空心端口圈 + 粗体标题。返回标题占掉之后剩下的区域。 */
    juce::Rectangle<int> drawCardTitle (juce::Graphics&, juce::Rectangle<int> titleRow,
                                        const juce::String& title, juce::Colour colour);

    /** Grid 风格的端口：深色填充的空心圆环。 */
    void drawPort (juce::Graphics&, juce::Point<float> centre, juce::Colour colour, float radius = 4.5f);

    /** 两个端口之间的连线（贝塞尔，带下垂）。底下先描一道深色，在亮色上也分得清。 */
    void drawCable (juce::Graphics&, juce::Point<float> from, juce::Point<float> to,
                    juce::Colour colour, float sag = 12.0f, float thickness = 2.4f);

    //== 图标（矢量，任意尺寸都清楚）=================================================
    enum class Icon
    {
        none, dice, prev, next, power, plus, close, window, eject, search, scan, tag,
        save, open, probe, pin, restore, more, check, chevronDown, chevronRight, warning, link
    };

    void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour, float strokeWidth = 1.6f);

    /** 运动语义的小曲线图标（保持 / 上扬 / 下降 / 先扬后抑 / 先抑后扬 / 漫游）。
        和 ContourIO 里真正生成的轮廓形状一致——图标画的就是会写进包络的那条线。 */
    void drawSemanticGlyph (juce::Graphics&, MotionSemantic, juce::Rectangle<float>,
                            juce::Colour, float strokeWidth = 1.8f);

    /** 把颜色调到"在深色模块上做半透明底色"的程度（快照方块、标签胶囊用）。 */
    inline juce::Colour tint (juce::Colour c, float amount = 0.22f)
    {
        return module.interpolatedWith (c, amount);
    }
}

} // namespace pr
