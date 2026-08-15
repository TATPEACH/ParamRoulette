#!/usr/bin/env node
// 找出**含非 ASCII 却没带 `_u8` 后缀**的字符串字面量。
//
// 为什么需要一个工具来查这个：`juce::String (const char*)` 走 CharPointer_ASCII，
// 每个字节当一个码点（Latin-1 语义），于是 UTF-8 的中文字面量在界面上变成乱码。
// JUCE 自己有个 jassert 提醒这件事，但 **Release 构建里它被编掉了**——
// 也就是说这个 bug 在正式构建里完全静默，只能靠扫源码发现。
// 详见 Source/Utf8.h。
//
//   node Tools/check_utf8_literals.mjs          # 扫 Source/ 和 Tests/
//   node Tools/check_utf8_literals.mjs a.cpp …  # 只扫指定文件
//
// 退出码 0 = 干净，1 = 有问题。
//
// 认这几种安全写法：
//   "中文"_u8
//   juce::String::fromUTF8 ("中文")
//   juce::CharPointer_UTF8 ("中文")

import fs from 'node:fs';
import path from 'node:path';

const ROOTS = ['Source', 'Tests'];

/** 极简 C++ 词法扫描。要点：
 *  - 注释、字符字面量、预处理行都要跳过（`#include <x>` 里的尖括号不是字符串）
 *  - **相邻字面量会被编译器拼接**，后缀只需出现在最后一个上，所以要整组一起判 */
function scan (src) {
  const bad = [];
  let i = 0, line = 1;
  const n = src.length;

  while (i < n) {
    const c = src[i];

    if (c === '\n') { line++; i++; continue; }

    if (c === '#') {
      const bol = src.lastIndexOf('\n', i - 1) + 1;
      if (/^[ \t]*$/.test(src.slice(bol, i))) {         // 行首（只有空白）才是预处理
        while (i < n && src[i] !== '\n') i++;
        continue;
      }
    }

    if (c === '/' && src[i + 1] === '/') { while (i < n && src[i] !== '\n') i++; continue; }

    if (c === '/' && src[i + 1] === '*') {
      i += 2;
      while (i < n && !(src[i] === '*' && src[i + 1] === '/')) { if (src[i] === '\n') line++; i++; }
      i += 2;
      continue;
    }

    if (c === "'") {                                    // 字符字面量：'\'' 之类
      i++;
      while (i < n && src[i] !== "'") { if (src[i] === '\\') i++; i++; }
      i++;
      continue;
    }

    if (c === '"') {
      const startLine = line, openAt = i;
      let group = '', lastEnd = -1;

      for (;;) {
        i++;                                            // 越过开引号
        while (i < n && src[i] !== '"') {
          if (src[i] === '\\') { group += src[i]; i++; }
          if (src[i] === '\n') line++;
          group += src[i];
          i++;
        }
        i++;                                            // 越过闭引号
        lastEnd = i;

        // 往后跳过空白和注释，看下一个 token 是不是还是字面量（拼接）
        let j = i, l2 = line;
        for (;;) {
          if (j < n && (src[j] === ' ' || src[j] === '\t' || src[j] === '\r')) { j++; continue; }
          if (j < n && src[j] === '\n') { l2++; j++; continue; }
          if (src[j] === '/' && src[j + 1] === '/') { while (j < n && src[j] !== '\n') j++; continue; }
          if (src[j] === '/' && src[j + 1] === '*') {
            j += 2;
            while (j < n && !(src[j] === '*' && src[j + 1] === '/')) { if (src[j] === '\n') l2++; j++; }
            j += 2;
            continue;
          }
          break;
        }
        if (j < n && src[j] === '"') { i = j; line = l2; continue; }
        break;
      }

      const hasU8   = /^_u8/.test(src.slice(lastEnd, lastEnd + 3));
      const before  = src.slice(Math.max(0, openAt - 40), openAt);
      const wrapped = /(fromUTF8|CharPointer_UTF8)\s*\(\s*$/.test(before);

      // eslint-disable-next-line no-control-regex
      if (/[^\x00-\x7F]/.test(group) && !hasU8 && !wrapped)
        bad.push({ line: startLine, text: group.slice(0, 58) });

      continue;
    }

    i++;
  }

  return bad;
}

const repo = path.resolve(import.meta.dirname, '..');
let targets = process.argv.slice(2);

if (targets.length === 0) {
  targets = ROOTS.flatMap((r) => {
    const dir = path.join(repo, r);
    if (!fs.existsSync(dir)) return [];
    return fs.readdirSync(dir).filter((f) => /\.(h|cpp)$/.test(f)).map((f) => path.join(dir, f));
  });
}

let total = 0;
for (const file of targets) {
  const bad = scan(fs.readFileSync(file, 'utf8'));
  if (bad.length === 0) continue;

  console.log(`\n${path.relative(repo, file)}`);
  for (const b of bad) console.log(`  :${b.line}  ${JSON.stringify(b.text)}`);
  total += bad.length;
}

console.log(
  total === 0
    ? `\nok - ${targets.length} 个文件，没有缺 _u8 的非 ASCII 字面量`
    : `\nFAIL - ${total} 个字面量含非 ASCII 但没带 _u8（界面/控制台上会是乱码）`
);

process.exit(total === 0 ? 0 : 1);
