#pragma once

namespace pr
{

/** 一条链上最多几个位置。

    **注意和快照槽（`kNumSlots`，4 个）区分开。** 两个都叫 slot 的话代码和手册
    立刻二义化。约定：
      - 链上的位置叫「**链位**」/ ChainSlot / kMaxChain —— 信号路径上的位置，串联跑
      - 存音色的叫「**快照**」/ kNumSlots —— 用来 morph 插值

    6 个足够放"全通 + disperser + 修饰性效果器"这类配方，再多就不好调了。

    这个常数单独一个头文件，是为了让 SnapshotBank 和 ChainRunner 都能用它而
    **不用互相 include**（HostedSlot 已经依赖 SnapshotBank，反向依赖会成环）。
*/
inline constexpr int kMaxChain = 6;

} // namespace pr
