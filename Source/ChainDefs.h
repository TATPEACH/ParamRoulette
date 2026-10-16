#pragma once

#include <array>
#include <algorithm>
#include <cstddef>

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

/** 把位置 `from` 上的东西挪到位置 `to`，中间的依次让位——**插入语义，不是交换**。

        [A B _ C _ _]  把 A 从 0 挪到 3  →  [B _ C A _ _]

    这正是在机架里拖一个模块的意思："放到那里"，而不是"和那里的那个对调"。
    链位数组、快照里每链位那一份、界面上每链位一个的子插件窗口，三处都必须用
    **同一个**函数挪，否则挪完以后它们会指向不同的插件。所以它是模板，也因此
    能在无头探针里单独测。 */
template <typename T, std::size_t N>
void moveChainPosition (std::array<T, N>& a, int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= (int) N || to >= (int) N)
        return;

    if (from < to)
        std::rotate (a.begin() + from, a.begin() + from + 1, a.begin() + to + 1);
    else
        std::rotate (a.begin() + to, a.begin() + from, a.begin() + from + 1);
}

/** `moveChainPosition (from, to)` 之后，原来在 `i` 的东西去了哪一位。
    聚焦的链位要跟着它的插件走——用户点着的是"这个插件"，不是"第 3 位"。 */
constexpr int chainPositionAfterMove (int i, int from, int to) noexcept
{
    if (i == from)                         return to;
    if (from < to && i > from && i <= to)  return i - 1;
    if (from > to && i >= to && i < from)  return i + 1;
    return i;
}

} // namespace pr
