#pragma once

#include <juce_core/juce_core.h>

namespace pr
{

/** UTF-8 字面量 → `juce::String`。**所有含非 ASCII 的字面量都必须带这个后缀。**

    为什么必须有：`juce::String (const char*)` 走的是 `CharPointer_ASCII`，
    而它的 `operator*` 是 `(juce_wchar) (uint8) *data` —— **每个字节当一个码点**，
    也就是 Latin-1 语义。于是 "未加载插件" 的 UTF-8 字节 `E6 9C AA ...`
    变成码点 `U+00E6 U+009C U+00AA ...`，界面上就是一片 `æ` 和方框。

    JUCE 自己在 `juce_String.cpp:310` 的注释里写了这件事：

    > If you get an assertion here, then you're trying to create a string from
    > 8-bit data that contains values greater than 127. These can NOT be correctly
    > converted to unicode because there's no way for the String class to know what
    > encoding was used to create them. ... you must be more explicit when you pass
    > an ambiguous 8-bit string.

    （Release 构建里 assert 被编掉了，所以这个错误在 Debug 之外是静默的。）

    **`/utf-8` 编译选项解决不了这件事。** 它保证的是字面量里躺着正确的 UTF-8 字节，
    而 JUCE 怎么解释这些字节是 JUCE 的事，两回事。

    用法：

        lStatus.setText ("未加载插件"_u8, juce::dontSendNotification);

    相邻字面量拼接时后缀只写在最后一个上就行（C++ 标准：拼接结果继承那个后缀）：

        "第一段……"
        "第二段。"_u8
*/
inline juce::String operator""_u8 (const char* text, size_t len)
{
    return juce::String (juce::CharPointer_UTF8 (text),
                         juce::CharPointer_UTF8 (text + len));
}

} // namespace pr
