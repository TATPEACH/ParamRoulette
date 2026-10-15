#!/usr/bin/env node
// 把编译好的插件装到本机 REAPER 实际加载的位置。
//
// 为什么需要它：编译产物在 build*/…_artefacts/ 下面，REAPER 加载的是 VST3 目录里那一份，
// 两者之间没有任何自动步骤（CMake 的 PR_COPY_AFTER_BUILD 默认关着——Program Files
// 一般要管理员权限）。结果就是"代码早就改了，REAPER 里还是旧界面"，而且从界面上
// 看不出自己用的是哪一版。
//
//   node Tools/install_plugins.mjs                    更新本机**已经装过**的副本
//   node Tools/install_plugins.mjs --add fx-x64 …     另外把还没装过的也装上
//   node Tools/install_plugins.mjs --dry-run          只打印要做什么，不动文件
//
// 产品名（--add 用）：synth-x86  fx-x86  synth-x64  fx-x64
//
// 安全措施：
//   - 只碰名字正好是这四个插件之一的 .vst3 bundle，**永远不删目录**，只逐个覆盖文件
//   - 覆盖前把旧 bundle 整个备份到 %LOCALAPPDATA%/ParamRoulette/backup/<时间>/（只留最近 5 次）
//   - 编译产物比源码旧就拒绝安装——忘了编译的话会装上一个旧版本，而且装完看不出来
//   - DLL 被占用（REAPER 开着并且加载过这个插件）时，在动任何文件之前就停下
//   - 装完逐文件比 SHA-256
//
// 退出码：0 = 成功（或 dry-run），1 = 有问题。

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const repo = path.resolve(import.meta.dirname, '..');

const PRODUCTS = {
  'synth-x86': 'build32/ParamRouletteSynth_artefacts/Release/VST3/ParamRoulette Synth x86.vst3',
  'fx-x86':    'build32/ParamRoulette_artefacts/Release/VST3/ParamRoulette x86.vst3',
  'synth-x64': 'build/ParamRouletteSynth_artefacts/Release/VST3/ParamRoulette Synth.vst3',
  'fx-x64':    'build/ParamRoulette_artefacts/Release/VST3/ParamRoulette.vst3',
};

/** --add 装到哪。REAPER 的 vstpath64 默认就包含这个目录，32 位的 bundle 放这里也能被找到
    （本机那份 x86 synth 一直就在这里）。 */
const DEFAULT_DEST = 'C:/Program Files/Common Files/VST3';

const KEEP_BACKUPS = 5;

//==============================================================================
const args = process.argv.slice(2);
const dryRun = args.includes('--dry-run');
const addIdx = args.indexOf('--add');
const toAdd = addIdx >= 0 ? args.slice(addIdx + 1).filter((a) => ! a.startsWith('--')) : [];

for (const n of toAdd)
  if (! PRODUCTS[n]) fail(`不认识的产品名：${n}（可选：${Object.keys(PRODUCTS).join(' / ')}）`);

function fail (msg) { console.error(`\n✗ ${msg}`); process.exit(1); }
function log (msg = '') { console.log(msg); }

function listFiles (dir) {
  const out = [];
  (function walk (d) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p); else out.push(p);
    }
  })(dir);
  return out;
}

const sha256 = (f) => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');

/** bundle 里真正的插件二进制：Contents/<arch>-win/<名字>.vst3 */
function binaryOf (bundle) {
  return listFiles(bundle).find((f) => /[\\/]Contents[\\/][^\\/]+-win[\\/][^\\/]+\.vst3$/i.test(f));
}

//==============================================================================
// 1. 本机都在哪些地方装了：标准 VST3 目录 + REAPER 自己配置的插件路径
function searchRoots () {
  const roots = new Set([DEFAULT_DEST, 'C:/Program Files (x86)/Common Files/VST3']);
  const ini = path.join(process.env.APPDATA ?? '', 'REAPER', 'reaper.ini');

  if (fs.existsSync(ini)) {
    const m = fs.readFileSync(ini, 'latin1').match(/^vstpath64=(.*)$/mi);
    if (m)
      for (let p of m[1].split(';')) {
        p = p.trim().replace(/%([^%]+)%/g, (_, v) => process.env[v] ?? `%${v}%`);
        if (p) roots.add(p);
      }
  }

  return [...roots].map((r) => r.replace(/\\/g, '/').replace(/\/+$/, ''));
}

function findInstalled (bundleName) {
  const hits = [];
  for (const root of searchRoots()) {
    if (! fs.existsSync(root)) continue;
    (function walk (d, depth) {
      if (depth > 4) return;
      let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
      for (const e of ents) {
        const p = path.join(d, e.name);
        if (e.name.toLowerCase() === bundleName.toLowerCase()) { hits.push(p); continue; }
        if (e.isDirectory() && ! /\.vst3$/i.test(e.name)) walk(p, depth + 1);
      }
    })(root, 0);
  }
  return [...new Set(hits.map((h) => path.resolve(h)))];
}

//==============================================================================
// 2. 编译产物是不是最新的
function newestSourceTime () {
  let t = fs.statSync(path.join(repo, 'CMakeLists.txt')).mtimeMs;
  for (const f of listFiles(path.join(repo, 'Source')))
    if (/\.(h|cpp)$/.test(f)) t = Math.max(t, fs.statSync(f).mtimeMs);
  return t;
}

//==============================================================================
const plan = [];   // { key, src, dest, isNew }
const srcTime = newestSourceTime();

for (const [key, rel] of Object.entries(PRODUCTS)) {
  const src = path.join(repo, rel);
  const name = path.basename(src);
  const installed = findInstalled(name);

  for (const dest of installed) plan.push({ key, src, dest, isNew: false });

  if (toAdd.includes(key) && installed.length === 0)
    plan.push({ key, src, dest: path.join(DEFAULT_DEST, name), isNew: true });
}

log('ParamRoulette 插件安装');
log('');

if (plan.length === 0) {
  log('本机没有装过任何 ParamRoulette，也没指定 --add。什么都不用做。');
  log(`想装就：node Tools/install_plugins.mjs --add synth-x86 fx-x64 …`);
  process.exit(0);
}

// —— 事前检查：全部通过才动手，免得装到一半停下
for (const p of plan) {
  if (! fs.existsSync(p.src))
    fail(`没有编译产物：${p.src}\n  先编译：cmake --build ${p.src.includes('build32') ? 'build32' : 'build'} --config Release`);

  const bin = binaryOf(p.src);
  if (! bin) fail(`bundle 里找不到插件二进制：${p.src}`);

  if (fs.statSync(bin).mtimeMs + 1000 < srcTime)
    fail(`编译产物比源码旧（${new Date(fs.statSync(bin).mtimeMs).toLocaleString()}），先重新编译：${p.src}`);

  // 路径兜底：只碰我们自己那四个 bundle
  if (! Object.values(PRODUCTS).some((r) => path.basename(r).toLowerCase() === path.basename(p.dest).toLowerCase()))
    fail(`目标不是 ParamRoulette 的 bundle，拒绝：${p.dest}`);

  // DLL 被占用？用读写方式打开试一下——不写任何东西。
  if (! p.isNew) {
    const destBin = binaryOf(p.dest);
    if (destBin) {
      try { fs.closeSync(fs.openSync(destBin, 'r+')); }
      catch (e) {
        fail(`${destBin}\n  正被占用（${e.code}）。REAPER 开着并且加载过这个插件——关掉 REAPER 再跑。`
             + '\n  （没有动任何文件。）');
      }
    }
  }
}

for (const p of plan)
  log(`${p.isNew ? '新装' : '更新'}  [${p.key}]  ${p.dest}`);

if (dryRun) { log('\n--dry-run：不动文件。'); process.exit(0); }

// —— 备份
const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
const backupRoot = path.join(process.env.LOCALAPPDATA ?? repo, 'ParamRoulette', 'backup');
const backupDir = path.join(backupRoot, stamp);

for (const p of plan.filter((q) => ! q.isNew)) {
  const to = path.join(backupDir, path.basename(p.dest));
  fs.cpSync(p.dest, to, { recursive: true });
}

// 只留最近几次
if (fs.existsSync(backupRoot)) {
  const all = fs.readdirSync(backupRoot).filter((d) => /^\d{4}-/.test(d)).sort();
  for (const old of all.slice(0, Math.max(0, all.length - KEEP_BACKUPS)))
    fs.rmSync(path.join(backupRoot, old), { recursive: true, force: true });
}

// —— 覆盖。二进制先拷：它是唯一可能失败（被占用）的那个，失败了 bundle 里别的文件还是旧的、配套的。
let bad = 0;

for (const p of plan) {
  const files = listFiles(p.src).sort((a, b) => (a === binaryOf(p.src) ? -1 : b === binaryOf(p.src) ? 1 : 0));

  for (const f of files) {
    const to = path.join(p.dest, path.relative(p.src, f));
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.copyFileSync(f, to);
  }

  // —— 校验
  const mismatched = files.filter((f) => sha256(f) !== sha256(path.join(p.dest, path.relative(p.src, f))));
  const bin = binaryOf(p.dest);

  if (mismatched.length) {
    bad++;
    log(`✗ [${p.key}] 校验不一致：${mismatched.map((f) => path.relative(p.src, f)).join(', ')}`);
  } else {
    log(`✓ [${p.key}] ${files.length} 个文件，SHA-256 一致   ${(fs.statSync(bin).size / 1048576).toFixed(2)} MB`
        + `   sha256 ${sha256(bin).slice(0, 12)}…`);
  }
}

log('');
if (plan.some((q) => ! q.isNew)) log(`旧版本备份在：${backupDir}`);
log('REAPER 要重启（或者第一次打开）才会加载新的 DLL——插件二进制是在挂插件实例时才读的，');
log('已经在跑的 REAPER 手里还是旧的。界面会直接是新版；如果 FX 浏览器里显示的参数表还是旧的，');
log('到 Preferences → Plug-ins → VST 点一下 Re-scan。');

process.exit(bad ? 1 : 0);
