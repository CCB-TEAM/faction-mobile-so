# faction-mobile-so

把 [KardsFactionToolkit](https://github.com/mdzz195/KardsFactionToolkit)（Windows x64 的 KARDS
新增盟国 DLL）移植到 **KARDS Android arm64** 的工作区。

**当前状态：只有逆向结论和骨架，没有任何可用产物。** 请先读完本文件再动手。

---

## 这个仓库里有什么

| 路径 | 内容 | 状态 |
| --- | --- | --- |
| `docs/ANDROID_RE.md` | 安卓 `libUnreal.so` 的逆向结论：地址、结构体、复现方法 | ✅ 静态分析确认 |
| `profiles/kards-1.57.26587-arm64.json` | 地址表（含已确认与未定位项） | ✅ 已确认部分可信 |
| `tools/find_faction_enum.py` | 从 ELF 重定位表还原枚举结构的扫描器 | ✅ 已跑通 |
| `src/` | arm64 移植骨架 | ⚠️ 编译通过，**未在真机运行** |
| `upstream/KardsFactionToolkit/` | Windows 原版源码（MIT，vendored 基线） | ✅ 上游 v0.2.0 |

**本仓库不包含任何游戏二进制。** 复现结论需要自行获取合法的游戏 APK。

---

## 已经确认的结论（可以直接用）

对 `libUnreal.so`（KARDS 1.57.26587 arm64）的静态分析确认：

### 1. 安卓版和 Windows 版的 `FEnumParams` ABI 完全一致

| 字段 | 安卓实测 | Windows 版校验值 |
| --- | --- | --- |
| `count` | `13` | `13` |
| `display` | `NULL` | `NULL` |
| `flags` | `0x45` | `0x45` |
| `enumFlags` | `0` | `0` |
| `cppForm` | `2` | `2` |
| `cppType == name` | 同一指针 | 要求相等 |
| `name` | `"EFactionEnum"` | `"EFactionEnum"` |
| 13 个条目名字与值 | 与 Windows 版逐项一致 | — |

结构体 48 字节、`count` 在偏移 44 —— `upstream/.../src/core.h` 里的
`static_assert` 在 arm64 上成立（骨架已在 `aarch64-unknown-linux-android24`
目标上零警告编译验证过）。

**所以 `core.h` 的结构体定义和 `Install()` 里的整段校验逻辑可以原样复用。**

### 2. 已定位的地址

```
Enumerator[13]  0x98ed1c8     (.data.rel.ro)
FEnumParams     0x98ed298     (.data.rel.ro)
"EFactionEnum"  0x261865e     (.rodata)
outer           0x6e874f4     (.text，语义待确认)
Params GOT 槽   0xa084b38     (.got)
```

### 3. 一个必须知道的坑

`.data.rel.ro` 里的指针**在文件中是 0**，真实值只存在于 `.rela.dyn`（145 万条重定位）。
直接对文件扫指针**什么都扫不到**，必须走重定位表还原。
`tools/find_faction_enum.py` 就是干这个的。

---

## 还没做的（接手人的任务）

按优先级：

### 1. 定位三个国家转换函数 —— 最高优先级，其他都卡在这

Windows 版要 hook `NameToEnum` / `Encode` / `Decode` 三个函数。安卓版**尚未找到**：

- 扫遍 88.7 MB `.text`，没有 `ADRP + ADD` 直接引用那几个结构
- 按 GOT 访问模式（`ADRP 0xa084000` + `LDR [xN, #0xb38]`）扫，也未命中

需要完整反汇编 + 动态调试（Frida / IDA / Ghidra）配合。推测 UE 走 `StaticEnum`
注册链 + 间接访问。

### 2. 确定入口时机

**ELF 没有 PE entry point 可以抢。** Windows 版 `ArmEntry()` / `EntryBootstrap` /
`RestoreEntry` 那一整套（DllMain 改入口跳板 → 入口处恢复机器码 → 安装 → 调原入口）
**在安卓上完全用不上**，要删掉而不是移植。

候选：`.init_array`（1664 项）/ `JNI_OnLoad` / 注入器显式调用。
`src/android_entry.cpp` 里标注了三个方案和各自的坑。

### 3. 找到 `Singleton` / `StaticSingleton`

Windows 版用它们判断枚举是否已构造（`NoEnumYet`），安卓版地址未知。

### 4. 接 hook 后端

Windows 用 MinHook。arm64 需要
[Dobby](https://github.com/jmpews/Dobby) 或
[ShadowHook](https://github.com/bytedance/android-inline-hook)，
加到 `vendor/` 并接进 `src/CMakeLists.txt` 的 `FACTION_HOOK_BACKEND`。

### 5. 解决"怎么被加载"

Windows 上"把 DLL 放到 exe 旁边"就完事了。安卓没有这个机制 —— APK 已签名、
`lib/` 只读、Android 7+ 有 linker namespace 隔离。三条路：

- root + Frida / ptrace 注入（最干净）
- LSPosed / Xposed 模块（也要 root）
- 重打包 APK + 重签名（**破坏 Play 完整性，且 1.1 GB 包迭代很慢**）

---

## 从 Windows 版继承时必须保留的语义

移植时这几条不能丢，否则会崩或泄漏（原因见 `upstream/.../docs/DEVELOPMENT.md`）：

1. **匹配成功也要调用原函数。** 原解析函数拥有传入的 by-value `FString` 并负责释放；
   跳过它就泄漏，自己释放则跨分配器。
2. **`Encode` 复用原分配器。** 先以 ANZAC 的值（10）调用原函数，拿到它用**自己的
   分配器**构造好的单字符 `FString`，然后**只改那一个字符**。不要自己分配。
3. **失败必须完整回滚** —— 卸 hook + 写回原结构，不留半装状态。
4. **枚举注册后不能卸载。** 名称数组和 detour 仍被 UE 反射引用。
5. **`expanded` 数组和替换后的 `EnumParams` 必须活到进程结束**（静态存储期）。

---

## 快速上手

```bash
# 1. 看看已确认的结构（需要自备 libUnreal.so）
python3 tools/find_faction_enum.py /path/to/libUnreal.so

# 2. 骨架编译（需要 NDK）
cd src
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28
cmake --build build

# 3. 没有 NDK 时，Termux 的 clang++ 也能做语法验证
clang++ -std=c++20 -Wall -Wextra -fsyntax-only core.cpp
```

---

## 版本漂移

`profiles/kards-1.57.26587-arm64.json` 里的地址**只对这一个构建有效**。

游戏更新后重跑 `tools/find_faction_enum.py` —— 扫描流程是结构化的（找字符串 →
查重定位 → 找 16 字节等距连续 13 条 → 回溯 `Params`），不依赖硬编码地址。

⚠️ 但目前**只能做到枚举结构逐字段校验**，因为三个关键函数还没定位，
无法建立 Windows 版那种"PE 字段 + 关键函数 SHA256"的完整版本指纹。

---

## 免责声明

- KARDS 是 1939 Games 的商标，其客户端、美术资源、卡牌数据的一切知识产权归 1939 Games 所有。
- 本仓库**不包含**游戏二进制、APK、资源文件或任何游戏数据。
- 修改在线游戏客户端可能违反 1939 Games 的服务条款（ToS）与 EULA，可能导致账号封禁。
- 本项目仅供**技术研究与学习**。请勿用于线上官服作弊、刷分或任何破坏游戏公平性的行为。
- 本仓库内容按"原样"（AS IS）提供，不提供任何担保。使用者自行承担全部风险与法律后果。

## 许可

本项目代码 MIT。`upstream/KardsFactionToolkit/` 为上游 MIT 项目，
版权归其贡献者；第三方组件许可见 `THIRD_PARTY_NOTICES.md`。
