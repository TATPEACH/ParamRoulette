#pragma once

#include "Theme.h"

namespace pr
{

/** 硬件拟物皮肤的设计系统。

    **视觉语言照着实体音频设备来**：藏青烤漆面板、原木边框、白色丝印、奶白铭牌、
    胶木指针旋钮 / 滚花旋钮、LED、拨杆开关、VU 表、荧光（VFD）显示屏、示波器。
    配色取样自用户给的参考图（藏青面板 #213B56~#2F4963、原木 #D8B697、
    红旋钮 #C92C2E、黄旋钮 #E3D714、琥珀 LED、铭牌 #A5A79D 一类的米白）。

    几条从拟物设计资料里归纳的硬规矩（来源见 MANUAL 的开发附录）：
      - **只有一个光源，在正上方**。所有高光在上沿、阴影在下沿
      - **旋钮的光影不跟着转**：转的只有指针 / 指示线，明暗层是固定在屏幕上的。
        整张贴图一起转是拟物界面最常见的穿帮
      - 渐变只拉开 ~15% 亮度，阴影要淡——过头了就是塑料玩具
      - 字要能读：再拟物也不能把字缩到看不清（KnobSmith 那篇说的"cargo cult"）

    和 Grid 皮肤的 `theme::` 并存：图标、字体族、语义图形这些与风格无关的东西直接复用。 */
namespace hw
{
    //== 面板 ======================================================================
    inline const juce::Colour panel       { 0xff23394f };   ///< 藏青烤漆
    inline const juce::Colour panelHi     { 0xff2d4864 };
    inline const juce::Colour panelLo     { 0xff1b2d40 };
    inline const juce::Colour bay         { 0xff111d2a };   ///< 凹进去的机架仓 / 屏幕外框
    inline const juce::Colour seam        { 0xff0c151f };   ///< 面板接缝、深阴影

    //== 木头 ======================================================================
    inline const juce::Colour wood        { 0xffd4ac7f };   ///< 枫木（参考图 #D8B697）
    inline const juce::Colour woodDark    { 0xffb2824f };
    inline const juce::Colour woodLight   { 0xffe8cba3 };

    //== 丝印 / 铭牌 ===============================================================
    inline const juce::Colour legend      { 0xffebe5d2 };   ///< 米白丝印（不是纯白：老设备的油墨）
    inline const juce::Colour legendDim   { 0xffa9b4c0 };
    inline const juce::Colour legendFaint { 0xff74879b };
    inline const juce::Colour plate       { 0xffe8e3d3 };   ///< 铭牌
    inline const juce::Colour plateInk    { 0xff1c232b };

    //== 旋钮帽 ====================================================================
    inline const juce::Colour capRed      { 0xffc8302c };
    inline const juce::Colour capYellow   { 0xffe2c92c };
    inline const juce::Colour capGrey     { 0xff808e93 };
    inline const juce::Colour capBlack    { 0xff2c3036 };
    inline const juce::Colour capCream    { 0xffe5dcc3 };
    inline const juce::Colour rubber      { 0xff3b4047 };   ///< 橡胶按键

    //== LED =======================================================================
    inline const juce::Colour ledRed      { 0xffff3b2f };
    inline const juce::Colour ledAmber    { 0xffffa928 };
    inline const juce::Colour ledGreen    { 0xff5cff70 };
    inline const juce::Colour ledBlue     { 0xff52b4ff };
    inline const juce::Colour ledCyan     { 0xff45f2d2 };

    //== 屏幕 ======================================================================
    inline const juce::Colour glass       { 0xff060e10 };   ///< VFD 玻璃底
    inline const juce::Colour vfd         { 0xff72f3d4 };   ///< VFD 荧光（青绿）
    inline const juce::Colour vfdDim      { 0xff2f7568 };
    inline const juce::Colour vfdFaint    { 0xff1a4239 };
    inline const juce::Colour vfdAmber    { 0xffffb54d };
    inline const juce::Colour vfdRed      { 0xffff6a57 };
    inline const juce::Colour crt         { 0xff5cff9e };   ///< 示波器磷光
    inline const juce::Colour crtGlass    { 0xff04110b };

    //== VU 表 =====================================================================
    inline const juce::Colour vuFace      { 0xfff3e2ad };
    inline const juce::Colour vuInk       { 0xff2b2317 };
    inline const juce::Colour vuRed       { 0xffc3342b };

    //== 尺寸 ======================================================================
    inline constexpr int frame = 16;     ///< 原木边框宽度

    //== 字体 ======================================================================
    using theme::Weight;

    /** 中文 / 混排文字：和 Grid 同一个字族（Noto Sans SC → YaHei UI），按点高取。
        直接 using 同一个函数而不是另包一层——参数类型 theme::Weight 会让 ADL 把
        theme::font 也找出来，两个签名一样的函数并存就是调用歧义。 */
    using theme::font;

    /** 面板丝印用的工程字体（DIN 风格的 Bahnschrift），**只用于纯 ASCII**：
        数字刻度、英文小标题。它没有中文字形，中文必须走 font()。 */
    juce::Font legendFont (float pt, bool bold = true);

    /** 手写体（Segoe Print）：链位上那条美纹纸上"用记号笔写"的插件名。 */
    juce::Font scriptFont (float pt);

    /** 商标字体（Segoe Script，参考图里 SonEQ / Green 4 Mix EQ 那种连笔字）。 */
    juce::Font logoFont (float pt);

    /** 等宽数字（VFD 上的读数）。只用于 ASCII。 */
    juce::Font monoFont (float pt);

    /** 纯 ASCII 吗？工程字体 / 等宽字体没有中文字形，有中文就得换 font()。 */
    bool isAscii (const juce::String&);

    //== 纹理 ======================================================================
    /** 木纹。生成一次缓存到进程结束（横纹 / 竖纹各一张），之后只裁切着画——
        窗口拖着改尺寸时不能每帧重算几十万个像素。 */
    void fillWood (juce::Graphics&, juce::Rectangle<int> area, bool verticalGrain);

    /** 面板漆面的细噪点（256² 平铺，叠很淡的一层），让大面积的藏青不像一块塑料。 */
    void fillPanelGrain (juce::Graphics&, juce::Rectangle<int> area, float alpha = 1.0f);

    //== 面板元件 ==================================================================
    /** 整块面板：藏青渐变 + 漆面噪点 + 上沿高光。 */
    void drawFaceplate (juce::Graphics&, juce::Rectangle<float>);

    void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angleRad = 0.6f);

    /** 奶白色铭牌（参考图里 DRIVE / EQ 那种），里面一行深色字。返回铭牌矩形。 */
    juce::Rectangle<float> drawPlate (juce::Graphics&, juce::Point<float> topLeft, const juce::String& text,
                                      float pt = 11.5f, const juce::String& english = {});

    /** 一块分区：丝印的圆角框线，左上角压一块铭牌当标题。返回框内可用区域。 */
    juce::Rectangle<int> drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title,
                                      const juce::String& english);

    /** 凹槽 / 屏幕外框：上沿内阴影 + 下沿高光，看起来是压进面板里的。 */
    void drawRecess (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour fill = bay);

    /** LED。brightness 0 = 熄灭（深色透镜 + 一点反光），1 = 全亮（亮芯 + 光晕）。 */
    void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, float brightness);

    /** 6.35 插孔：六角螺母 + 黑洞。 */
    void drawJack (juce::Graphics&, juce::Point<float> centre, float radius);

    /** 跳线：带阴影和高光的粗线 + 两头插头。颜色就是信号颜色。 */
    void drawPatchCable (juce::Graphics&, juce::Point<float> from, juce::Point<float> to,
                         juce::Colour, float sag = 18.0f, float thickness = 5.0f);

    /** 拨杆开关。position 0 = 拨下（关），1 = 拨上（开），中间值是动画过程。 */
    void drawBatToggle (juce::Graphics&, juce::Rectangle<float>, float position, bool enabled);

    /** 一颗有厚度的键帽：面板上的键槽 + 帽子（侧面 + 上亮下暗的顶面 + 上沿高光）。
        按下时帽子沉下去 1.5px、侧面变薄。返回帽子顶面——字和 LED 画在它上面。 */
    juce::Rectangle<float> drawKey (juce::Graphics&, juce::Rectangle<float> well, bool down, bool hover,
                                    juce::Colour base = rubber, float radius = 4.0f, bool enabled = true);

    //== 旋钮 ======================================================================
    enum class Knob
    {
        pointerRed,     ///< 红色胶木指针旋钮（主强度）
        pointerYellow,  ///< 黄色指针旋钮（运动 / 音色）
        pointerBlack,   ///< 黑色鸡头旋钮（档位选择器）
        knurledGrey,    ///< 灰色滚花旋钮 + 白指示线（次要参数）
        knurledBlack    ///< 黑色滚花旋钮
    };

    /** 画一个旋钮。`angle` 是指针方向（弧度，0 = 正上方，顺时针）。
        光影固定在屏幕上，只有指针跟着转。 */
    void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float angle, Knob,
                   bool enabled, bool hover);

    /** 旋钮外圈的刻度：一圈点（major 处大一点）+ 可选的数字。 */
    void drawScale (juce::Graphics&, juce::Point<float> centre, float radius, float a0, float a1,
                    int numTicks, int majorEvery, const juce::StringArray& labels, juce::Colour,
                    float labelRadius = 0.0f);

    //== 显示 ======================================================================
    /** 荧光屏上的字：先画一层淡的光晕再画字芯，像 VFD 那样"发光"而不是印上去。 */
    void drawGlowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Colour,
                       const juce::Font&, juce::Justification, bool ellipsis = true, float glow = 0.35f);

    /** 七段数码管。支持 0-9、'-'、空格；`lit` 是亮段颜色，熄灭的段画成暗影。 */
    void drawSevenSegment (juce::Graphics&, juce::Rectangle<float>, const juce::String& chars, juce::Colour lit);

    /** 玻璃反光：一道从左上斜下来的很淡的白色渐变，盖在屏幕 / 表头最上层。 */
    void drawGlassSheen (juce::Graphics&, juce::Rectangle<float>, float radius, float strength = 1.0f);
}

} // namespace pr
