#include "MotionEngine.h"
#include "Randomizer.h"   // subsample：「每次只动 N 个」在音色层和普通随机化上共用一份实现

namespace pr
{

namespace
{
    constexpr float kMoveEpsilon = 1.0e-5f;

    /** 名字末尾的连续数字。"level 3" → 3；"cutoff" → 0。 */
    int trailingNumber (const juce::String& s)
    {
        int i = s.length();
        while (i > 0 && juce::CharacterFunctions::isDigit (s[i - 1]))
            --i;

        return (i < s.length()) ? s.substring (i).getIntValue() : 0;
    }

    /** 第一个 token 是不是"某组包络"的头？`eg4` → 是，组号 4。
        `envelope` → 不是（去掉 env 之后剩的不是数字）。 */
    bool isEnvHead (const juce::String& head, int& group)
    {
        // 故意不收 "mod"：太泛了，"mod wheel" 会被误判成一组包络。
        // 收 "amp"：fauna 的 amp.a/.d/.s/.r 就是一组 ADSR。
        static const char* const prefixes[] = { "eg", "env", "lfo", "adsr", "amp" };

        for (auto* pre : prefixes)
        {
            if (! head.startsWith (pre))
                continue;

            const auto rest = head.substring ((int) std::strlen (pre));
            if (rest.isEmpty() || rest.containsOnly ("0123456789"))
            {
                group = rest.isEmpty() ? 1 : rest.getIntValue();
                return true;
            }
        }

        return false;
    }

    /** 包络内部的角色。顺序有讲究：
        - "delay time" 先撞上 delay，那是对的；
        - ADSR 四段必须排在 level / time 前面，否则 "release time" 会被认成轮廓时长。 */
    Role envRole (const juce::String& w, int& point)
    {
        point = 0;

        // fauna 用 ">" 表示调制路由（v>rate / v>amp）。那是"速度对速率的调制量"，
        // 不是速率本身，把它拉向速率锚点是错的。交给普通随机化。
        if (w.containsChar ('>'))
            return Role::None;

        // "...rate.type" 是速率的**模式选择器**（自由 / 同步），不是速率。
        if (w.contains ("type") || w.contains ("mode") || w.contains ("wave"))
            return Role::None;

        // 单字母 ADSR：fauna 的 amp.a / amp.d / amp.s / amp.r 是常见写法
        const auto t = w.trim();
        if (t == "a") return Role::EnvAttack;
        if (t == "d") return Role::EnvDecay;
        if (t == "s") return Role::EnvSustain;
        if (t == "r") return Role::EnvRelease;

        if (w.contains ("amount") || w.contains ("amt") || w.contains ("depth")) return Role::EnvAmount;
        if (w.contains ("rate")   || w.contains ("speed"))                      return Role::EnvRate;
        if (w.contains ("inv"))                                                 return Role::EnvInvert;

        if (w.contains ("attack"))  return Role::EnvAttack;
        if (w.contains ("sustain")) return Role::EnvSustain;
        if (w.contains ("release")) return Role::EnvRelease;
        if (w.contains ("decay"))   return Role::EnvDecay;

        if (w.contains ("delay"))                                               return Role::EnvDelay;
        if (w.contains ("stage"))                                               return Role::EnvStages;
        if (w.contains ("loop") || w.contains ("repeat"))                       return Role::EnvLoop;
        if (w.contains ("route")  || w.contains ("dest") || w.contains ("target")) return Role::EnvRoute;

        if (w.contains ("level") || w.contains ("lvl") || w.contains ("value"))
        {
            point = trailingNumber (w.trim());
            return Role::EnvLevel;
        }

        if (w.contains ("time") || w.contains ("dur") || w.contains ("len"))
        {
            point = trailingNumber (w.trim());
            return Role::EnvTime;
        }

        return Role::None;
    }

    /** 不属于任何包络的参数。只分两类：动了会跑调的，和只影响音色的。

        **必须按 token 匹配，不能只用子串。** 老插件的参数名全是缩写：fauna 的音高
        参数叫 `tens` / `trans` / `len`，滤波叫 `hp` / `lp.f` / `n.lp` / `spl.1.lp`。
        用 "tension" / "lowpass" 这种全称去 contains 一个都命中不了——实测在 fauna
        的 178 个参数上，音高和音色各命中 0 个。

        fauna 的 split 段（`spl.1.te` / `.tr` / `.le` / `.lp` / `.na` / `.nl` / `.pr`）
        是主参数的镜像缩写（tens / trans / len / lp / n.amp / n.lp / p(in)），
        按这个对应关系归类——这是从命名对称性推的，不是瞎猜。 */
    Role globalRole (const juce::String& n)
    {
        // 长名（子串匹配）
        static const char* const pitchySub[] = {
            "tension", "transpose", "detune", "pitch", "tract", "semitone", "cents", "glide"
        };
        static const char* const tonalSub[] = {
            "lowpass", "highpass", "bandpass", "cutoff", "filter", "formant", "resonance",
            "memory", "noise", "damp", "timbre", "brightness", "texture", "throat", "breath"
        };
        // 缩写（**整个 token 相等**才算）
        static const char* const pitchyTok[] = {
            "tens", "te", "trans", "tr", "tune", "len", "le", "pitch", "semi", "oct"
        };
        // 有意**不收** amp / amt / level / vol / gain：
        // 抖响度不是抖音色，而且一抖 A/B 就没法比了。send*.amt 同理——
        // 那是调制矩阵的深度，动它是重接线不是换音色。
        static const char* const tonalTok[] = {
            "lp", "hp", "bp", "lpf", "hpf", "cut", "res", "q", "f", "mem", "na", "nl",
            "pr", "damp", "tone", "col"
        };

        // 响度类一律不进音色层
        static const char* const loudness[] = { "volume", "gain", "master", "output", "level" };
        for (auto* w : loudness) if (n.contains (w)) return Role::None;

        for (auto* w : pitchySub) if (n.contains (w)) return Role::Pitch;
        for (auto* w : tonalSub)  if (n.contains (w)) return Role::Tone;

        auto tokens = juce::StringArray::fromTokens (n, " ._-/", "");
        tokens.removeEmptyStrings();

        // "amp" 单独一个 token = 主音量，跳过；"n.amp" 这种带前缀的才是噪声电平
        if (tokens.size() == 1 && (tokens[0] == "amp" || tokens[0] == "amt"))
            return Role::None;

        for (const auto& t : tokens) for (auto* w : pitchyTok) if (t == w) return Role::Pitch;
        for (const auto& t : tokens) for (auto* w : tonalTok)  if (t == w) return Role::Tone;

        // n.amp 之类：前缀 + amp
        if (tokens.size() >= 2 && tokens[tokens.size() - 1] == "amp")
            return Role::Tone;

        // fauna 的 20 个共振器：r1-1 .. r4-5。形如 rN 后面跟一个数字 token。
        if (tokens.size() >= 2 && tokens[0].length() == 2
            && tokens[0].startsWithChar ('r')
            && juce::CharacterFunctions::isDigit (tokens[0][1])
            && tokens[1].containsOnly ("0123456789"))
            return Role::Tone;

        return Role::None;
    }

    /** 音色层每个角色自己的抖动幅度（音色强度 = 1.0 时的上限）。

        数值沿用 codex 规格里那张表（tract length / tension ±0.06，
        memory / lowpass ±0.18~0.22），按角色归并成两档。

        他后来针对 bird 5 给过一套更小的建议幅度（tens ±0.02~0.04、
        lp.f ±0.015~0.03、mem ±0.04~0.08…）——**那是给用户的配置建议，
        不是改这里的默认值**。想要那种细粒度就把音色强度调小：
        这两档是线性缩放的，音色 0.3 大约对应 ±0.018 / ±0.06。 */
    float jitterSpan (Role r)
    {
        switch (r)
        {
            case Role::Pitch: return 0.06f;     // 音高：6% 已经明显跑调
            case Role::Tone:  return 0.20f;
            default:          return 0.0f;      // 运动层的参数不参与音色抖动
        }
    }
}

//==============================================================================
juce::String motionSemanticLabel (MotionSemantic s)
{
    switch (s)
    {
        case MotionSemantic::Preserve: return "保持原语义"_u8;
        case MotionSemantic::Rise:     return "上扬"_u8;
        case MotionSemantic::Fall:     return "下降"_u8;
        case MotionSemantic::RiseFall: return "先扬后抑"_u8;
        case MotionSemantic::FallRise: return "先抑后扬"_u8;
        case MotionSemantic::Wander:   return "漫游"_u8;
    }
    return "上扬"_u8;
}

juce::String roleLabel (Role r)
{
    switch (r)
    {
        case Role::EnvAmount: return "包络深度"_u8;
        case Role::EnvRate:   return "包络速率"_u8;
        case Role::EnvInvert: return "包络反相"_u8;
        case Role::EnvDelay:  return "包络延迟"_u8;
        case Role::EnvStages: return "段数"_u8;
        case Role::EnvLoop:   return "循环"_u8;
        case Role::EnvRoute:  return "调制目标"_u8;
        case Role::EnvLevel:  return "轮廓电平"_u8;
        case Role::EnvTime:   return "轮廓时长"_u8;
        case Role::EnvAttack: return "起音"_u8;
        case Role::EnvDecay:  return "衰减"_u8;
        case Role::EnvSustain:return "持续"_u8;
        case Role::EnvRelease:return "释放"_u8;
        case Role::Pitch:     return "音高相关"_u8;
        case Role::Tone:      return "音色相关"_u8;
        case Role::None:      break;
    }
    return {};
}

void MotionSettings::normalise() noexcept
{
    motion  = juce::jlimit (0.0f, 1.0f, motion);
    tone    = juce::jlimit (0.0f, 1.0f, tone);
    maxTone = juce::jmax (0, maxTone);      // 负数和 0 同义（不限制），统一成 0
}

//==============================================================================
bool MotionProfile::hasMotion() const noexcept
{
    for (const auto& h : hits)
        if (h.role != Role::None && h.role != Role::Pitch && h.role != Role::Tone)
            return true;

    return false;
}

bool MotionProfile::hasInvert() const noexcept
{
    for (const auto& h : hits)
        if (h.role == Role::EnvInvert)
            return true;

    return false;
}

int MotionProfile::find (Role r, int group, int point) const
{
    for (const auto& h : hits)
        if (h.role == r && (group == 0 || h.group == group) && (point == 0 || h.point == point))
            return h.modelIndex;

    return -1;
}

juce::String MotionProfile::summary() const
{
    if (hits.isEmpty())
        return "没认出任何运动相关参数——运动模式对这个插件无效"_u8;

    int nPitch = 0, nTone = 0, nEnv = 0, nAdsr = 0;
    for (const auto& h : hits)
    {
        switch (h.role)
        {
            case Role::Pitch: ++nPitch; break;
            case Role::Tone:  ++nTone;  break;
            case Role::EnvAttack:
            case Role::EnvDecay:
            case Role::EnvSustain:
            case Role::EnvRelease: ++nAdsr; ++nEnv; break;
            default: ++nEnv; break;
        }
    }

    juce::String s;
    s << "包络 "_u8 << numEnvGroups << " 组 / "_u8 << nEnv << " 项"_u8;

    if (nAdsr > 0)             s << "（含 ADSR "_u8 << nAdsr << " 段）"_u8;
    if (numContourPoints > 0)  s << "  ·  轮廓 "_u8 << numContourPoints << " 点"_u8;
    else if (nAdsr == 0)       s << "  ·  轮廓点未暴露"_u8;

    s << "  ·  音高 "_u8 << nPitch << "  ·  音色 "_u8 << nTone;
    return s;
}

MotionProfile detectProfile (const juce::Array<ParamInfo>& model)
{
    MotionProfile prof;
    int maxGroup = 0;

    for (int k = 0; k < model.size(); ++k)
    {
        const auto& info = model.getReference (k);

        // blob 是插件的数据槽，别当参数用
        if (info.blob)
            continue;

        auto tokens = juce::StringArray::fromTokens (info.lowerName, " ._-/", "");
        tokens.removeEmptyStrings();

        if (tokens.isEmpty())
            continue;

        RoleHit h;
        h.modelIndex = k;

        int group = 0;
        if (isEnvHead (tokens[0], group))
        {
            // 剩下的 token 拼回去当"角色词"。"eg4" "amount" → "amount"
            const auto rest = tokens.joinIntoString (" ", 1, tokens.size() - 1);
            int point = 0;
            const auto r = envRole (rest, point);

            if (r != Role::None)
            {
                h.role  = r;
                h.group = group;
                h.point = point;

                maxGroup = juce::jmax (maxGroup, group);
                if (r == Role::EnvLevel || r == Role::EnvTime)
                    prof.numContourPoints = juce::jmax (prof.numContourPoints, point);

                prof.hits.add (h);
                continue;
            }
        }

        if (const auto r = globalRole (info.lowerName); r != Role::None)
        {
            h.role = r;
            prof.hits.add (h);
        }
    }

    // 组号是名字里的数字，可能从 1 开始也可能跳号；用最大值当"有几组"是近似，
    // 但这个数只上界面，不参与计算。
    prof.numEnvGroups = maxGroup;

    // —— 认 fauna。判据是几个只有它才会同时具备的名字，不是插件名字符串
    // （名字可能被改，参数命名不会）。认出来就走 codex 试听验证过的配方。
    const auto byName = [&model] (const char* exact) -> int
    {
        for (int k = 0; k < model.size(); ++k)
            if (model.getReference (k).lowerName == exact)
                return k;
        return -1;
    };

    prof.idxEg3Amount  = byName ("eg3 amt");
    prof.idxEg4Amount  = byName ("eg4 amt");
    prof.idxEg4Invert  = byName ("eg4 inv");
    prof.idxEg4Rate    = byName ("eg4 rate");
    prof.idxEg4Repeat  = byName ("eg4 repeat");
    prof.idxEg4Stages  = byName ("eg4 stages");
    prof.idxEg4Sustain = byName ("eg4 sustain");
    prof.idxEg4Route   = byName ("eg4.dest");
    prof.idxTension    = byName ("tens");

    prof.looksLikeFauna = prof.idxEg4Invert >= 0 && prof.idxEg4Route >= 0
                       && prof.idxTension  >= 0 && prof.idxEg4Amount >= 0;

    return prof;
}

//==============================================================================
Contour makeContour (MotionSemantic s, float motion, int numPoints, juce::Random& rng,
                     bool bakeDirection)
{
    Contour c;

    const int n = juce::jmax (1, numPoints);
    c.levels.assign ((size_t) n, 0.5f);
    c.times .assign ((size_t) n, 0.5f);

    const float amp = juce::jlimit (0.0f, 1.0f, motion);

    // 方向不烧进曲线时，下降/先抑后扬折叠成对应的正向形状——方向由 invert 参数负责。
    if (! bakeDirection)
    {
        if (s == MotionSemantic::Fall)     s = MotionSemantic::Rise;
        if (s == MotionSemantic::FallRise) s = MotionSemantic::RiseFall;
    }

    // 拐点位置每次抖一下。钉死在正中间的话，"随机"生成十次是同一条曲线。
    const float pivot = 0.30f + rng.nextFloat() * 0.40f;

    if (s == MotionSemantic::Wander)
    {
        // 带惯性的随机游走。逐点 rand() 出来的是噪声不是曲线——
        // 惯性负责让它有走向，后面的平滑负责削掉尖角。
        float v = rng.nextFloat(), vel = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            vel = vel * 0.60f + (rng.nextFloat() - 0.5f) * 0.50f;
            v = juce::jlimit (0.0f, 1.0f, v + vel);
            c.levels[(size_t) i] = v;
        }

        const auto raw = c.levels;
        for (int i = 0; i < n; ++i)
        {
            const float a = raw[(size_t) juce::jmax (0, i - 1)];
            const float b = raw[(size_t) i];
            const float d = raw[(size_t) juce::jmin (n - 1, i + 1)];

            c.levels[(size_t) i] = juce::jlimit (0.0f, 1.0f,
                                                 0.5f + ((a + b + d) / 3.0f - 0.5f) * amp);
        }
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const float t = (n <= 1) ? 0.0f : (float) i / (float) (n - 1);
            float v = 0.5f;

            switch (s)
            {
                case MotionSemantic::Rise: v = t;        break;
                case MotionSemantic::Fall: v = 1.0f - t; break;

                case MotionSemantic::RiseFall:
                    v = (t <= pivot) ? t / pivot
                                     : 1.0f - (t - pivot) / (1.0f - pivot);
                    break;

                case MotionSemantic::FallRise:
                    v = (t <= pivot) ? 1.0f - t / pivot
                                     : (t - pivot) / (1.0f - pivot);
                    break;

                case MotionSemantic::Preserve:
                case MotionSemantic::Wander:
                    break;
            }

            // amp 从 0.5 这条中线往外撑：强度 0 是一条平线，1 才铺满整个量程。
            c.levels[(size_t) i] = juce::jlimit (0.0f, 1.0f, 0.5f + (v - 0.5f) * amp);
        }
    }

    for (int i = 0; i < n; ++i)
        c.times[(size_t) i] = juce::jlimit (0.05f, 1.0f,
                                            0.5f + (rng.nextFloat() - 0.5f) * 0.60f * amp);

    return c;
}

//==============================================================================
MotionResult applyMotion (juce::AudioPluginInstance& plugin,
                          const juce::Array<ParamInfo>& model,
                          const juce::Array<bool>& checked,
                          const MotionProfile& prof,
                          const std::vector<float>& base,
                          MotionSettings st,
                          juce::Random& rng,
                          int contourStages)
{
    st.normalise();

    MotionResult out;
    if (prof.hits.isEmpty())
    {
        out.note = "没认出运动相关参数"_u8;
        return out;
    }

    const auto& params = plugin.getParameters();

    const auto baseOf = [&base] (int paramIndex)
    {
        return ((size_t) paramIndex < base.size()) ? base[(size_t) paramIndex] : 0.5f;
    };

    // 先把目标值全算出来，最后一次性写。分层写的话同一个参数会被 setValue 两三次，
    // 每次都要过一遍子插件——没必要，而且中间态可能出怪声。
    std::vector<float> want ((size_t) prof.hits.size(), 0.5f);
    std::vector<float> anchorBase ((size_t) prof.hits.size(), 0.5f);

    for (int i = 0; i < prof.hits.size(); ++i)
    {
        const auto& h = prof.hits.getReference (i);
        const float b = baseOf (model.getReference (h.modelIndex).index);
        want[(size_t) i]       = b;      // 一切从基座出发
        anchorBase[(size_t) i] = b;
    }

    /** 按 model 下标覆写目标值。fauna 配方用它精确指定那 9 个参数。 */
    const auto setWant = [&] (int modelIndex, float v)
    {
        if (modelIndex < 0)
            return;

        for (int i = 0; i < prof.hits.size(); ++i)
            if (prof.hits.getReference (i).modelIndex == modelIndex)
                want[(size_t) i] = v;
    };
    const auto wantBaseOf = [&] (int modelIndex)
    {
        return juce::isPositiveAndBelow (modelIndex, model.size())
                 ? baseOf (model.getReference (modelIndex).index) : 0.5f;
    };

    // —— 音色层：围绕基座抖，幅度按角色分档（可被逐参数 span 覆盖）
    //
    // 先收候选、再抽样、最后才抖。候选**必须先过勾选**：抽中两个没勾的参数
    // 等于这一轮什么都没抖，用户设「只动 2」会时不时得到"一个都没动"。
    // （落笔那一轮会跳过没勾的，所以没勾的参数占掉名额就是纯浪费。）
    {
        juce::Array<int> toneHits;
        toneHits.ensureStorageAllocated (prof.hits.size());

        // 幅度：逐参数设定优先于角色默认档，然后一起乘音色强度。
        //
        // **span 只改幅度，不改"谁参与"**：角色默认档为 0 的参数（包络深度、
        // 反相那些运动层的东西）即使设了 span 也不抖——运动层随后会整个覆写
        // 它们的目标值，先抖一下纯属白费，还会让用户以为设定没生效。
        const auto spanOf = [&model] (const RoleHit& h)
        {
            const float roleSpan = jitterSpan (h.role);

            if (roleSpan <= 0.0f)
                return 0.0f;            // 不参与音色层，和 span 无关

            if (juce::isPositiveAndBelow (h.modelIndex, model.size()))
                if (const auto& info = model.getReference (h.modelIndex); info.hasCustomSpan())
                    return info.span;

            return roleSpan;
        };

        for (int i = 0; i < prof.hits.size(); ++i)
        {
            const auto& h = prof.hits.getReference (i);

            if (spanOf (h) <= 0.0f)
                continue;

            if (h.modelIndex < checked.size() && ! checked.getUnchecked (h.modelIndex))
                continue;

            toneHits.add (i);
        }

        subsample (toneHits, st.maxTone, rng);

        for (const int i : toneHits)
        {
            const float span = spanOf (prof.hits.getReference (i));
            want[(size_t) i] += (rng.nextFloat() * 2.0f - 1.0f) * span * st.tone;
        }
    }

    // —— 运动层
    if (st.semantic != MotionSemantic::Preserve)
    {
        const bool rising   = (st.semantic == MotionSemantic::Rise
                            || st.semantic == MotionSemantic::RiseFall
                            // 漫游没有方向，每次掷一个——否则它在 fauna 上恒等于下降
                            || (st.semantic == MotionSemantic::Wander && rng.nextBool()));
        const bool twoStage = (st.semantic == MotionSemantic::RiseFall
                            || st.semantic == MotionSemantic::FallRise
                            || st.semantic == MotionSemantic::Wander);

        const float m = st.motion;
        const MotionAnchors a;

        // 曲线只在 invert 缺席时才承担方向——否则双重取反
        const bool bakeDirection = ! prof.hasInvert();

        Contour contour;
        if (prof.numContourPoints > 0)
            contour = makeContour (st.semantic, m, prof.numContourPoints, rng, bakeDirection);

        for (int i = 0; i < prof.hits.size(); ++i)
        {
            const auto& h = prof.hits.getReference (i);
            float&      v = want[(size_t) i];
            const float b = anchorBase[(size_t) i];

            switch (h.role)
            {
                case Role::EnvInvert: v = rising ? 0.0f : 1.0f; break;
                case Role::EnvAmount: v = m;    break;
                case Role::EnvDelay:  v = 0.0f; break;
                case Role::EnvLoop:   v = 0.0f; break;

                case Role::EnvStages:
                {
                    const int steps  = model.getReference (h.modelIndex).numSteps;
                    const int stages = (contourStages > 0) ? contourStages : (twoStage ? 2 : 1);

                    // 有档位信息就按档位算，没有就给个小的非零值——
                    // 猜量程比硬写 1.0（= 段数拉满）安全。
                    v = (steps > 1) ? (float) (stages - 1) / (float) (steps - 1)
                                    : (float) (stages - 1) / a.faunaStageSpan;
                    break;
                }

                case Role::EnvRate:
                    v = b + (a.rate - b) * m;
                    break;

                // —— ADSR 四段。只有起音 / 衰减 / 持续参与方向；释放段有意不动：
                // "音尾多长"是编曲决定，跟"往上走还是往下走"没关系。
                case Role::EnvAttack:
                {
                    const float anchor = twoStage ? a.twoStageAttack
                                                  : (rising ? a.riseAttack : a.fallAttack);
                    v = b + (anchor - b) * m;
                    break;
                }

                case Role::EnvDecay:
                    v = b + ((twoStage ? a.twoStageDecay : a.decay) - b) * m;
                    break;

                case Role::EnvSustain:
                {
                    const float anchor = twoStage ? a.twoStageSustain
                                                  : (rising ? a.riseSustain : a.fallSustain);
                    v = b + (anchor - b) * m;
                    break;
                }

                case Role::EnvRelease:
                    break;

                case Role::Pitch:
                {
                    // 这一步是让"上扬"真的听起来上扬的关键。
                    // 只给包络加深度往往没用：基座的静态音高可能已经贴着量程上限，
                    // 正向调制没有余量，选哪个方向听着都是往下走。所以把静态值
                    // 也朝方向锚点拉。用 += 是为了保留上面那层音色抖动。
                    const float anchor = rising ? a.riseTension : a.fallTension;
                    v += (anchor - b) * m;
                    break;
                }

                case Role::EnvLevel:
                    if (h.point >= 1 && (size_t) h.point <= contour.levels.size())
                        v = contour.levels[(size_t) h.point - 1];
                    break;

                case Role::EnvTime:
                    if (h.point >= 1 && (size_t) h.point <= contour.times.size())
                        v = contour.times[(size_t) h.point - 1];
                    break;

                case Role::EnvRoute:    // 通用路径下有意不写，见头文件
                case Role::Tone:
                case Role::None:
                    break;
            }
        }

        // —— fauna 专用配方，覆写上面那一轮的通用结果。
        // 每一条都是 codex 在 bird 5 上试听得出的，不是推导出来的：
        if (prof.looksLikeFauna)
        {
            const float anchor = rising ? a.riseTension : a.fallTension;

            setWant (prof.idxEg3Amount,  0.0f);                     // EG3->length 实测听不出差别，关掉
            setWant (prof.idxEg4Route,   a.faunaRouteToTension);    // 目标不指到 tension，深度加再多也没用
            setWant (prof.idxEg4Repeat,  0.0f);
            setWant (prof.idxEg4Sustain, 0.0f);
            setWant (prof.idxEg4Invert,  rising ? 0.0f : 1.0f);
            setWant (prof.idxEg4Amount,  m);
            setWant (prof.idxEg4Rate,    wantBaseOf (prof.idxEg4Rate)
                                         + (a.rate - wantBaseOf (prof.idxEg4Rate)) * m);

            // tension 是唯一真正听得出音高的杠杆，而 bird 5 的基座已经贴着上限
            // （实测 ≈0.85），所以必须把静态值往方向锚点拉才有余量。
            setWant (prof.idxTension,    wantBaseOf (prof.idxTension)
                                         + (anchor - wantBaseOf (prof.idxTension)) * m);

            if (contourStages > 0)
                setWant (prof.idxEg4Stages, (float) (contourStages - 1) / a.faunaStageSpan);
        }
    }

    // —— 落笔
    for (int i = 0; i < prof.hits.size(); ++i)
    {
        const auto& h = prof.hits.getReference (i);

        if (! juce::isPositiveAndBelow (h.modelIndex, model.size()))
            continue;

        // filter 在这里和随机化 / morph 含义一致：勾选 = 允许改动
        if (h.modelIndex < checked.size() && ! checked.getUnchecked (h.modelIndex))
            continue;

        const auto& info = model.getReference (h.modelIndex);
        if (! juce::isPositiveAndBelow (info.index, params.size()))
            continue;

        auto* p = params[info.index];
        if (p == nullptr)
            continue;

        float v = juce::jlimit (0.0f, 1.0f, want[(size_t) i]);

        // 伪二值 / 开关只收精确端点，中间值会被插件原地弹回。
        // 体检认出来的那批参数 kind 已经被改写成 Toggle，走这条分支。
        if (info.kind == Kind::Toggle)
            v = (v >= 0.5f) ? 1.0f : 0.0f;
        else if (info.kind == Kind::Stepped && info.numSteps > 1)
        {
            const float steps = (float) (info.numSteps - 1);
            v = std::floor (v * steps + 0.5f) / steps;   // 运动层是确定性的，这里不要随机取整
        }

        const float previous = p->getValue();
        p->setValueNotifyingHost (v);

        ++out.touched;
        if (std::abs (v - previous) > kMoveEpsilon)
            ++out.moved;
    }

    out.note = motionSemanticLabel (st.semantic)
             + "  ·  运动 "_u8 + juce::String (juce::roundToInt (st.motion * 100.0f)) + "%"
             + "  ·  音色 "_u8 + juce::String (juce::roundToInt (st.tone   * 100.0f)) + "%";

    return out;
}

} // namespace pr
