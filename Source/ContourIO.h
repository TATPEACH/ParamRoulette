#pragma once

#include "MotionEngine.h"

namespace pr
{

/** CK Envelope 轮廓的读写——把「曲线随机化」从参数层挪到状态块层。

    **为什么需要这一层**：有些插件的包络轮廓不是普通参数。fauna 的四条 16 段包络
    在参数表里只表现为四个叫 `CK_GUI_Envelope_16` 的参数，而它们的"值"是一个
    不透明的二进制串——`setValue(float)` 根本表达不了 16 个断点。所以走参数
    这条路，曲线永远动不了，而曲线恰恰是"上扬 / 下降"这类语义的载体。

    唯一的入口是插件自己的状态块（`getStateInformation` / `setStateInformation`）。
    读出来 → 认签名 → 改浮点 → 写回去。

    **这是逆向工程的产物，不是公开格式。** 所以：
      - 认不出签名就什么都不做，绝不猜着写；
      - 每次写之前都重新读一遍当前状态，不缓存偏移；
      - 偏移集中在 `ContourLayout` 里，格式一变一眼能看完。

    ## 已核实的布局（fauna，239292 字节状态块）

    状态块里有 4 处 9 字节签名 `01 00 00 00 | 80 00 00 00 | EB`
    （uint32 1、uint32 128=program 数、byte 235=记录长度），对应 4 条包络。
    每处签名后 8 字节是组头，接着是 128 条记录，stride 236：

        [0]        0xEB 长度标记
        [1..5]     5 字节浮点，未设置时是 NaN
        [6..10]    同上
        [11..90]   16 个电平，每个 5 字节，归一化 0..1
        [91..170]  16 个时长，每个 5 字节，原生单位（实测 0..47）
        [171..234] 尾部，不动

    5 字节浮点编码：IEEE-754 的 32 位按小端拆成 5 个 7-bit 组，每组 **+1** 避开 NUL
    （所以字节值域是 0x01..0x80）。这一条是从 codex 的 Lua 版拿来的，独立验证过往返。

    **和 codex 的偏移差 4 个字节**：他读的是 REAPER 轨道状态块里 base64 解码后的
    数据，那边插件数据前多 4 字节头，被他的 15 / 95 吸收了。我们从
    `getStateInformation` 拿到的是插件自己的块，所以是 11 / 91。
    在 512 条记录（4 组 × 128 program）上暴力扫描确认：偏移 11 和 91 上
    16 个值 512/512 全部落在合理范围，而 15 得 275、95 只得 22。
*/
struct ContourLayout
{
    static constexpr int kNumGroups   = 4;
    static constexpr int kNumPrograms = 128;
    static constexpr int kRecordBytes = 235;
    static constexpr int kStride      = 236;
    static constexpr int kHeaderBytes = 8;      ///< 签名起点到第 0 条记录的距离
    static constexpr int kPoints      = 16;
    static constexpr int kValueBytes  = 5;
    static constexpr int kLevelOffset = 11;     ///< 记录内，第 0 个电平
    static constexpr int kTimeOffset  = 91;     ///< 记录内，第 0 个时长
};

/** 5 字节 CK 浮点编解码。暴露出来是为了能单独测。 */
float ckDecodeFloat (const juce::uint8* p) noexcept;
void  ckEncodeFloat (juce::uint8* dest, float value) noexcept;

/** 一条包络在状态块里的位置。 */
struct ContourSlot
{
    int groupIndex = -1;    ///< 第几条包络（0..3）
    int byteOffset = -1;    ///< 签名在状态块里的字节偏移
};

/** 在状态块里定位所有轮廓记录组。认不出就返回空数组。 */
juce::Array<ContourSlot> findContourGroups (const juce::MemoryBlock&);

/** 原生轮廓：电平归一化 0..1，时长是插件的原生单位。
    和 MotionEngine 的 `Contour` 区别在时长的量纲，别混用。 */
struct NativeContour
{
    std::array<float, ContourLayout::kPoints> levels {};
    std::array<float, ContourLayout::kPoints> times  {};

    /** 实际在用的段数 = 时长非零的段数。写回时要保住这个数，
        否则一条 5 段的包络会变成 16 段，音色面目全非。 */
    int usedSegments() const noexcept;
};

bool readContour  (const juce::MemoryBlock&, const ContourSlot&, int program, NativeContour& out);
bool writeContour (juce::MemoryBlock&,       const ContourSlot&, int program, const NativeContour&);

/** 两种轮廓形状。**方向不在这里** ——「上扬」和「下降」用的是同一条 ramp，
    差别只在包络的 invert 参数。曲线里再取一次反等于双重取反，出来的方向是反的。

    形状本身是 codex 在 fauna（bird 5）上试听定下来的：单段 ramp 或双段 peak，
    其余段全部清零。比 16 点重塑简单得多，而且**从零合成、不读当前值**，
    所以反复生成不会累积漂移。 */
enum class ContourShape { Keep, Ramp, Peak };

/** 语义 → 形状 + 该用几段。 */
ContourShape shapeForSemantic (MotionSemantic) noexcept;
int          stagesForShape   (ContourShape) noexcept;

/** 合成一条轮廓。`base` 只用来保留尾部和未使用段的原始字节。
    `jitter` > 0 时给时长和 peak 的拐点位置加一点随机——量很小，
    而且每次都从固定形状重算，不会漂。 */
NativeContour makeShapeContour (const NativeContour& base, ContourShape,
                                float jitter, juce::Random&);

struct ContourResult
{
    int written = 0;            ///< 改了几条曲线
    int stages  = 0;            ///< 用了几段（要同步写进插件的 stages 参数）
    bool failed = false;        ///< 读到了格式但写回失败
};

/** 读状态 → 合成轮廓 → 写回。

    - `baseState` 非空时，轮廓的"未改动部分"从**基座**取，而不是从当前状态取。
      这是「每次生成都从锁定基座重算」的落点。
    - `onlyGroup >= 0` 时只改那一条包络（fauna 只改 EG4 = 组 3）。
    - `expectedProgram >= 0` 时，当前 program 与它不一致就整个中止——
      用户中途换了 preset，基座已经对不上了，硬写只会毁掉新 preset。

    **必须在消息线程上调用。** 调用方要知道 `setStateInformation` 会把整个状态
    （含所有参数值）覆盖成状态块里的内容，所以顺序是「先改轮廓写回，再改参数」。 */
ContourResult randomiseContours (juce::AudioPluginInstance&,
                                 MotionSemantic, float motion, juce::Random&,
                                 const juce::MemoryBlock* baseState = nullptr,
                                 int onlyGroup = -1,
                                 int expectedProgram = -1);

} // namespace pr
