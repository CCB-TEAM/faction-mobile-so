# KARDS 安卓版 `EFactionEnum` 逆向记录

本文记录对 KARDS **Android arm64** 客户端 `libUnreal.so` 的静态分析结论，目的是为
`KardsFactionToolkit` 的安卓移植提供已确认的地址与结构体依据。

> **样本来源**：`libUnreal.so` 取自 `KLink_1.57.26587.apk` 的 `lib/arm64-v8a/`。
> 该 APK 是第三方重打包产物（AOSP 测试密钥签名、包名被改为 `com.xuewu.KLink`），
> 但其中的 `libUnreal.so` 与 `classes.dex` 未被修改，可作为原始游戏二进制的样本。
>
> **本仓库不包含游戏二进制**。复现本文结论需要自行获取合法的游戏 APK。

---

## 1. 样本概况

| 项 | 值 |
| --- | --- |
| 文件 | `lib/arm64-v8a/libUnreal.so` |
| 大小 | 168,921,816 B（161 MiB） |
| 格式 | ELF64 aarch64，PIE，full RELRO，NX |
| 编译器 | LLD 18.0.3 / Android clang 9.0.9 |
| 符号表 | 已 strip（无 `.symtab`） |
| 引擎 | UE5（UE5 起 `libUE4.so` 改名为 `libUnreal.so`） |
| 同目录 | `libpsoservice.so`、`libswappy.so` |

### 关键 section

| Section | vaddr | size | 说明 |
| --- | --- | --- | --- |
| `.rodata` | `0x2156900` | `0x81d7a4` | 枚举名字符串 |
| `.eh_frame` | `0x2bbdb18` | `0xd3c8f8` | 函数边界来源（替代 PE 的 `.pdata`） |
| `.text` | `0x38ff000` | `0x58640f8` | 88.7 MB |
| `.data.rel.ro` | `0x9169440` | `0xe69180` | **枚举结构所在** |
| `.init_array` | `0x9fd25d0` | 1664 项 | UE 静态注册 |
| `.got` | `0x9fd5bf0` | `0x11dfa0` | `Params` 指针槽 |
| `.data` | `0xa0f8d40` | `0x2ae58` | |
| `.rela.dyn` | `0x85a0` | `0x214aeb0` | **1,454,578 条重定位** |

> ⚠️ **`.data.rel.ro` 里的指针在文件中是 0**，真实值只存在于 `.rela.dyn`。
> 直接对文件做字符串/指针扫描**扫不到**枚举数组 —— 必须走重定位表还原。
> 这是移植过程中最容易踩的坑，见 §4。

---

## 2. 已确认：`Enumerator[13]` 数组

**vaddr `0x98ed1c8`**（位于 `.data.rel.ro`）

每项 16 字节，布局为 `{ const char* name; int64_t value; }`：

| # | 文件槽位 vaddr | 名字 | 值 |
| --- | --- | --- | --- |
| 0 | `0x98ed1c8` | `EFactionEnum::NotAvailable` | 0 |
| 1 | `0x98ed1d8` | `EFactionEnum::Germany` | 1 |
| 2 | `0x98ed1e8` | `EFactionEnum::Britain` | 2 |
| 3 | `0x98ed1f8` | `EFactionEnum::Japan` | 3 |
| 4 | `0x98ed208` | `EFactionEnum::Soviet` | 4 |
| 5 | `0x98ed218` | `EFactionEnum::USA` | 5 |
| 6 | `0x98ed228` | `EFactionEnum::France` | 6 |
| 7 | `0x98ed238` | `EFactionEnum::Italy` | 7 |
| 8 | `0x98ed248` | `EFactionEnum::Poland` | 8 |
| 9 | `0x98ed258` | `EFactionEnum::Finland` | 9 |
| 10 | `0x98ed268` | `EFactionEnum::Anzac` | 10 |
| 11 | `0x98ed278` | `EFactionEnum::Allies` | 11 |
| 12 | `0x98ed288` | `EFactionEnum::Neutral` | 12 |

`name` 指针走重定位；`value` 是内联整数，直接在文件中可读。

**顺序与名称和 Windows 版 `src/core.cpp` 的 `OriginalNames[]` 逐项一致。**

### 名字字符串 vaddr

| 字符串 | vaddr |
| --- | --- |
| `EFactionEnum::NotAvailable` | `0x267497d` |
| `EFactionEnum::Germany` | `0x257ee0c` |
| `EFactionEnum::Britain` | `0x2613d6f` |
| `EFactionEnum::Japan` | `0x2617026` |
| `EFactionEnum::Soviet` | `0x25ac884` |
| `EFactionEnum::USA` | `0x26c5f8b` |
| `EFactionEnum::France` | `0x268a2e8` |
| `EFactionEnum::Italy` | `0x2580c08` |
| `EFactionEnum::Poland` | `0x269140f` |
| `EFactionEnum::Finland` | `0x2691424` |
| `EFactionEnum::Anzac` | `0x26abe1d` |
| `EFactionEnum::Allies` | `0x25d4d73` |
| `EFactionEnum::Neutral` | `0x2624832` |
| `EFactionEnum`（裸名） | `0x261865e` |

---

## 3. 已确认：`FEnumParams`

**vaddr `0x98ed298`**（`.data.rel.ro`），48 字节：

| 偏移 | 字段 | 实测值 |
| --- | --- | --- |
| 0 | `outer` | `0x6e874f4`（落在 `.text`，语义待确认） |
| 8 | `display` | `NULL` |
| 16 | `name` | `0x261865e` → `"EFactionEnum"` |
| 24 | `cppType` | `0x261865e`（**与 `name` 同一指针**） |
| 32 | `entries` | `0x98ed1c8` |
| 40 | `flags` | `0x45` |
| 44 | `count` | `13` |
| 46 | `enumFlags` | `0` |
| 47 | `cppForm` | `2` |

`Params` 自身的指针存放在 `.got` 的 **`0xa084b38`**。

### 与 Windows 版校验逻辑的逐字段对照

Windows 版 `src/core.cpp` 的 `Install()` 会逐字段校验原始结构后才安装。
下表把那些条件与安卓实测值对齐：

| Windows 校验条件 | 安卓实测 | 结果 |
| --- | --- | --- |
| `params->count != 13` | `13` | ✅ 一致 |
| `params->display`（须为 `NULL`） | `NULL` | ✅ 一致 |
| `params->flags != 0x45` | `0x45` | ✅ 一致 |
| `params->enumFlags != 0` | `0` | ✅ 一致 |
| `params->cppForm != 2` | `2` | ✅ 一致 |
| `params->cppType != params->name` | 同一指针 | ✅ 一致 |
| `strcmp(params->name, "EFactionEnum")` | `"EFactionEnum"` | ✅ 一致 |
| 13 个条目的名字与值 | 完全一致 | ✅ 一致 |

> **结论：安卓版与 Windows 版的 `FEnumParams` ABI 与取值完全相同。**
>
> `upstream/KardsFactionToolkit/src/core.h` 里的
> `static_assert(sizeof(EnumParams) == 48 && offsetof(EnumParams, count) == 44)`
> 在 arm64 上成立 —— 实测结构就是 48 字节，`count` 位于偏移 44。
> 该结构体定义、以及 `Install()` 中的整段校验逻辑可以**原样复用**。

---

## 4. 复现方法

`.data.rel.ro` 中的指针在文件里是 0，必须通过 `.rela.dyn` 还原。步骤：

1. 解析 ELF program header，建立 **文件偏移 ↔ vaddr** 双向映射。
2. 在可读段中扫描所有 `EFactionEnum::` 字符串，记录其 vaddr。
3. 解析 `.rela.dyn`（`Elf64_Rela`，24 字节/条），筛出 `r_addend` 落在上一步字符串 vaddr
   集合中的条目 —— 这些就是枚举名字指针。
4. 按 `r_offset` 排序，找 **间距恰为 16 字节的连续 13 条** —— 这就是 `Enumerator[13]` 数组。
5. 在 `.rela.dyn` 中再找 `r_addend == 数组首地址` 的条目，其 `r_offset - 32` 即
   `FEnumParams` 起始地址（`entries` 在结构体偏移 32）。
6. 读取该 48 字节，逐字段核对 §3 表格。

`tools/find_faction_enum.py` 实现了以上全部步骤，直接对 `libUnreal.so` 运行即可：

```bash
python3 tools/find_faction_enum.py /path/to/libUnreal.so
```

脚本同时输出 §2、§3 两张表，可用于**游戏更新后重新生成地址表**。

---

## 5. 尚未确认的部分

以下内容本轮**没有**找到，是移植的主要阻塞点：

### 5.1 三个国家转换函数

Windows 版需要 hook 三个函数（`NameToEnum` / `Encode` / `Decode`）。安卓版**尚未定位**：

- 扫描全部 88.7 MB `.text`，**没有** `ADRP + ADD` 直接引用
  `Params` / `Enumerators` / `EnumName` / `Outer`
- 按 GOT 访问模式（`ADRP 0xa084000` + `LDR [xN, #0xb38]`）扫描，**也未命中**

推测 UE 走 `StaticEnum` 注册链 + 间接访问，需要完整反汇编配合动态调试才能定位。
**这是接手人的首要任务。**

### 5.2 `Singleton` / `StaticSingleton`

Windows 版 `NoEnumYet()` 通过读这两个指针判断枚举是否已构造，以拒绝晚安装。
安卓版对应地址未定位。

### 5.3 入口时机

**ELF 没有 PE entry point 可以抢占。** Windows 版的
`ArmEntry()` / `EntryBootstrap` / `RestoreEntry` 那一整套（DllMain 里改入口跳板 →
入口处恢复机器码 → 安装 → 调用原入口）**在安卓上完全用不上**。

候选时机：

- `.init_array`（1664 项）—— UE 静态注册所在，需确认 `EFactionEnum` 由哪一项触发
- `JNI_OnLoad`
- UE 的 `ProcessNewlyLoadedUObjects`

### 5.4 其他

- `outer = 0x6e874f4` 的语义（是否 UObject outer / package）
- 牌组编码在移动端是否仍是 `b..z` 留给新增、`0..9a` 给原版
- 移动端是否存在与桌面版等价的 `Encode` 中「ANZAC 编码为 `a`」行为
- 是否有完整性校验 / 反调试

---

## 6. 版本漂移

本文所有地址**只对 1.57.26587 这一个构建有效**。

游戏更新后 `libUnreal.so` 变化会导致全部地址失效。重新定位的方法是重跑
`tools/find_faction_enum.py`（§4 的流程是结构化的，不依赖硬编码地址）。

Windows 版用「PE 字段 + 关键函数 SHA256 + 枚举结构逐字段校验」把版本判定做实；
安卓版目前**只能做到枚举结构逐字段校验**，因为三个关键函数还没定位 ——
在 §5.1 解决之前，无法建立与 Windows 版等价的版本指纹。
