#pragma once

#include "SnapshotBank.h"
#include <deque>
#include <functional>

namespace pr
{

/** 随机化历史的游标算术。

    **单独成一个类是为了能测。** PluginProcessor 依赖 `JucePlugin_*` 宏，
    进不了无头探针；而"回退 / 前进"这套游标逻辑恰好是最容易悄悄出错的部分——
    错了不崩，只会跳到一个毫无关系的状态上，而且要连点好几次才暴露。

    不变式（改这个类之前先读一遍）：
      - `entries[i]` 是第 i 次动作**之前**的状态
      - `cursor == entries.size()` 表示"活着的那一份比记录里的都新"（还没被记下来）
      - 头部因为超上限被丢弃时 `cursor` 一起前移，否则它会指向另一条记录

    双上限（条数 + 字节）都要：只限条数的话，Serum 之类 MB 级状态块的插件
    会让 20 份历史吃掉几百兆常驻内存。
*/
class HistoryRing
{
public:
    HistoryRing (int maxEntriesIn, size_t maxBytesIn)
        : maxEntries (juce::jmax (1, maxEntriesIn)), maxBytes (maxBytesIn) {}

    void clear() noexcept
    {
        entries.clear();
        cursor = 0;
        bytes  = 0;
    }

    /** 改动状态的动作**之前**调：砍掉"未来"、把当前状态追加进去、游标推到尖端。

        砍掉未来是必须的：用户回退过之后再掷一次，旧的"下一个"已经不在这条时间线上，
        留着的话「下一个」会走到一个被覆盖掉的分支。 */
    void push (Snapshot live)
    {
        while ((int) entries.size() > cursor)
        {
            bytes -= entries.back().childState.getSize();
            entries.pop_back();
        }

        append (std::move (live));
        cursor = (int) entries.size();
    }

    /** 回退一步，返回要还原的那一份；nullptr = 已经到最早的记录。

        `captureLive` 只在真正需要时（游标在尖端、"现在"还没被记下来）才调用，
        所以不会每次回退都白抓一份状态块。

        返回的指针指向环内元素，**只在下一次 push / back / forward / clear 之前有效**。 */
    const Snapshot* back (const std::function<Snapshot()>& captureLive)
    {
        if (cursor <= 0)
            return nullptr;

        // 第一次回退：先把"现在"追加进去，否则「下一个」无处可回。
        if (cursor == (int) entries.size() && captureLive != nullptr)
            append (captureLive());

        // append 可能因为超上限丢掉头部、把游标一路推到 0。
        // 那种情况下确实没有更早的状态了。
        if (cursor <= 0)
            return nullptr;

        --cursor;
        return &entries[(size_t) cursor];
    }

    const Snapshot* forward()
    {
        if (! canForward())
            return nullptr;

        ++cursor;
        return &entries[(size_t) cursor];
    }

    bool canBack()    const noexcept { return cursor > 0; }
    bool canForward() const noexcept { return cursor < (int) entries.size() - 1; }

    int getPos() const noexcept { return cursor; }

    /** 一共有几个状态可回。游标在尖端时要把"活着的那一份"也算进去——
        它还没被记录，但用户能回到它。 */
    int getCount() const noexcept
    {
        return (int) entries.size() + (cursor == (int) entries.size() ? 1 : 0);
    }

    // —— 下面两个只给测试和诊断用
    int    getNumEntries() const noexcept { return (int) entries.size(); }
    size_t getBytes()      const noexcept { return bytes; }

private:
    void append (Snapshot s)
    {
        bytes += s.childState.getSize();
        entries.push_back (std::move (s));

        // 留至少一条，否则「上一个」永远是空的。
        while (entries.size() > 1
               && ((int) entries.size() > maxEntries || bytes > maxBytes))
        {
            bytes -= entries.front().childState.getSize();
            entries.pop_front();

            // 丢了头部，所有下标往前移一位。不跟着改的话游标会指向另一条记录。
            cursor = juce::jmax (0, cursor - 1);
        }
    }

    std::deque<Snapshot> entries;
    int    cursor = 0;
    size_t bytes  = 0;
    int    maxEntries;
    size_t maxBytes;
};

} // namespace pr
