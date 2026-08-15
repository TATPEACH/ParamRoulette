#pragma once

#include "ParamModel.h"
#include <vector>

namespace pr
{

/** 运动语义随机化。

    普通随机化把每个参数**各自独立**地掷一遍，所以插件的"运动"（包络、LFO、
    音高轮廓）出来的是噪声：十六个轮廓点各随机一次，听起来就是抖动，不是走向。
    这个模块换一种做法——先说清"我要什么形状的运动"，再让相关的一组参数
    **协同**地去实现它。

    三个从 codex 的 Lua 版学来的做法，按重要性排：

      1. **锁定基座**。每次生成都从一份锁定的快照出发，而不是从当前值出发。
         不这么做的话连点十次会越走越远，那是随机游走不是"生成变体"。
      2. **锚点交叉淡入**：`v = base + (anchor - base) * motion`。
         直接给包络加深度往往没用——基座的静态值可能已经贴着量程上限了，
         正向调制没有余量，选什么方向都往下走。把**静态值**也朝方向锚点拉才有效。
      3. **分层强度**：运动层和音色层各有一个强度，音色层每个参数还有自己的
         抖动幅度（音高抖 6% 就跑调了，滤波抖 20% 才听得出来）。

    **关于 fauna**：codex 在 Lua 版里是直接改 REAPER 轨道状态块里 fauna 的
    不透明包络记录（base64 解码 → 按硬编码字节偏移改浮点 → 写回）。那条路在这里
    走不通，两个原因：一是 fauna 是 32 位 VST2，这个宿主根本加载不了它；
    二是 JUCE 里拿到的是 VST3 的状态块，没有 REAPER 的 chunk API 可用。
    所以这里搬的是它的**语义层**：角色按名字发现（fauna 的 `eg4.inv` /
    `tension` / `tract length` / `noise lowpass` 这些名字都能被下面的规则认出来，
    等于把它硬编码的参数下标换成了可移植的写法），锚点常量沿用它试听调出来的值。
    代价是**轮廓点**这一层只对"把包络点暴露成普通参数"的插件有效——
    fauna 把轮廓藏在不透明 blob 里，即使能加载也拿不到。
*/

//==============================================================================
enum class MotionSemantic
{
    Preserve = 0,   ///< 只抖音色，不动运动层
    Rise,           ///< 上扬
    Fall,           ///< 下降
    RiseFall,       ///< 先扬后抑
    FallRise,       ///< 先抑后扬
    Wander          ///< 漫游：带惯性的随机游走（不是噪声）
};

inline constexpr int kNumMotionSemantics = 6;

juce::String motionSemanticLabel (MotionSemantic);

/** 参数在"运动"里扮演的角色，按名字猜。猜错了不会坏事——最多是这个参数
    不参与运动层，退回普通随机化。 */
enum class Role
{
    None,
    EnvAmount,      ///< 包络深度
    EnvRate,        ///< 包络速率
    EnvInvert,      ///< 包络反相（方向开关）
    EnvDelay,
    EnvStages,      ///< 段数
    EnvLoop,
    EnvRoute,       ///< 调制目标。**有意不写**，见 applyMotion 的说明
    EnvLevel,       ///< 轮廓第 point 点的电平
    EnvTime,        ///< 轮廓第 point 点的时长

    // ADSR 四段单列。VST3 里 ADSR 比 16 点轮廓常见得多，但它**不能**表达任意形状：
    // 起音之后是单调衰减，所以"先抑后扬"在只有 ADSR 的插件上会退化成"先扬后抑"。
    // 这是有意的近似，不是漏了。
    EnvAttack,
    EnvDecay,
    EnvSustain,
    EnvRelease,

    Pitch,          ///< tension / tune / tract：抖一点点就跑调，幅度要小
    Tone            ///< 滤波 / 共振 / formant：音色层，可以抖大些
};

juce::String roleLabel (Role);

struct RoleHit
{
    int  modelIndex = -1;   ///< 在 model 里的下标（**不是**参数下标）
    Role role  = Role::None;
    int  group = 0;         ///< 第几组包络（eg4 → 4）。非包络角色为 0
    int  point = 0;         ///< 轮廓点序号（level 3 → 3）。非轮廓角色为 0
};

struct MotionProfile
{
    juce::Array<RoleHit> hits;
    int numEnvGroups     = 0;
    int numContourPoints = 0;   ///< 找到的最大轮廓点序号

    /** 认出 fauna 的参数命名了吗？认出来就走 codex 试听验证过的那套配方
        （只驱动 EG4、写调制目标、tension 按方向拉锚点），而不是通用启发式。
        通用引擎在不认识的插件上仍然是默认路径。 */
    bool looksLikeFauna = false;

    /** fauna 配方要用的几个具体下标（-1 = 没找到）。按名字查出来的，不硬编码数字。 */
    int idxEg3Amount = -1, idxEg4Amount = -1, idxEg4Invert = -1, idxEg4Rate = -1;
    int idxEg4Repeat = -1, idxEg4Stages = -1, idxEg4Sustain = -1, idxEg4Route = -1;
    int idxTension   = -1;

    /** 有没有 invert 类角色。有的话方向交给它，曲线只生成正向形状；
        没有的话方向必须烧进曲线里。**两个都做等于双重取反。** */
    bool hasInvert() const noexcept;

    /** 有没有认出任何包络类角色。没有的话运动层无事可做，只剩音色抖动。 */
    bool hasMotion() const noexcept;

    /** 给界面看的一行摘要。 */
    juce::String summary() const;

    /** 找第一个匹配的角色，返回 model 下标；-1 = 没有。 */
    int find (Role, int group = 0, int point = 0) const;
};

/** 按参数名发现角色。纯启发式，不联网不猜插件身份。 */
MotionProfile detectProfile (const juce::Array<ParamInfo>& model);

//==============================================================================
/** 方向锚点。

    这几个数是 codex 在 fauna（bird 5 那个 preset）上试听调出来的，直接沿用。
    它们是**默认值不是真理**——换插件八成要另调，所以集中放在这一处，
    改的时候一眼能看完。 */
struct MotionAnchors
{
    float riseTension = 0.25f;      ///< "上扬"时静态音高往哪儿靠
    float fallTension = 0.85f;      ///< "下降"时往哪儿靠
    float rate        = 0.65f;      ///< 包络速率的目标值

    // ADSR：上扬 = 慢起 + 高持续；下降 = 瞬起 + 低持续；两段 = 中起 + 低持续。
    // 这几个是按 ADSR 的常识定的，不是试听出来的，换插件更要自己调。
    float riseAttack     = 0.55f;
    float fallAttack     = 0.02f;
    float twoStageAttack = 0.30f;
    float riseSustain     = 0.85f;
    float fallSustain     = 0.10f;
    float twoStageSustain = 0.20f;
    float decay         = 0.55f;
    float twoStageDecay = 0.45f;

    /** fauna 的 EG4 调制目标里 tension 那一档的归一化值。
        codex 实测：不把目标指到 tension，深度加再多都听不出音高在动。 */
    float faunaRouteToTension = 5.0f / 32.0f;

    /** stages 参数的显示跨度。fauna 的 stages 显示 1..16.9，
        所以 N 段对应的归一化值是 (N-1)/15.9。 */
    float faunaStageSpan = 15.9f;
};

struct MotionSettings
{
    MotionSemantic semantic = MotionSemantic::Rise;
    float motion = 0.65f;   ///< 运动层强度
    float tone   = 0.35f;   ///< 音色层抖动强度

    /** 每次最多抖动几个**音色层**参数。0 = 不限制。

        **只限音色层，不限运动层。** 运动层那几个（包络深度 / 反相 / 速率 / 段数）
        就是语义本身——漏写 invert 方向就反了，漏写 amt 深度就没了，所以它们
        每次必写。音色层才是 codex 说的「每次只随机其中 1~2 个」：
        一次抖动全部 20 个共振器系数是换物种，不是换个体。

        以后别把这条改成"限制全部参数"，那会让语义时不时整个失效。 */
    int maxTone = 0;

    void normalise() noexcept;
};

/** 一条生成好的轮廓。levels / times 都是归一化 0..1，长度 = numPoints。 */
struct Contour
{
    std::vector<float> levels, times;
};

/** 生成 numPoints 个点的轮廓。

    `bakeDirection = false` 时「下降」和「先抑后扬」会退化成对应的正向形状
    （ramp / peak）——因为方向要交给包络的 invert 参数。**两处都做方向等于
    双重取反，出来的方向是反的。** 有 invert 角色时必须传 false。 */
Contour makeContour (MotionSemantic, float motion, int numPoints, juce::Random&,
                     bool bakeDirection = true);

struct MotionResult
{
    int          touched = 0;
    int          moved   = 0;
    juce::String note;
};

/** 从基座生成一个变体。

    - `base` 按**参数下标**对齐（和 `Snapshot::values` 同一个约定）。
    - 只动 `checked[i] == true` 的参数：filter 在这里和随机化 / morph 含义一致，
      都是"改动范围"。
    - 必须在消息线程上调用。

    `Role::EnvRoute` 认得出来但**有意不写**：调制目标是个音乐决定，在不认识的
    插件上瞎猜会把"随机一下运动"变成"悄悄重接我的合成器"。fauna 那边这个值是
    5/32（选中 tension），要复现得手动去子插件界面里设。
*/
MotionResult applyMotion (juce::AudioPluginInstance&,
                          const juce::Array<ParamInfo>& model,
                          const juce::Array<bool>& checked,
                          const MotionProfile&,
                          const std::vector<float>& base,
                          MotionSettings,
                          juce::Random&,
                          int contourStages = 0);

} // namespace pr
