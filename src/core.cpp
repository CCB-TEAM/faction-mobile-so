// KARDS faction extension — Android arm64 port skeleton.
// STATUS: SKELETON, not runtime-verified. See docs/ANDROID_RE.md.
#include "core.h"

#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace KardsFaction {
namespace {

constexpr const char* OriginalNames[] = {
    "EFactionEnum::NotAvailable", "EFactionEnum::Germany", "EFactionEnum::Britain",
    "EFactionEnum::Japan", "EFactionEnum::Soviet", "EFactionEnum::USA",
    "EFactionEnum::France", "EFactionEnum::Italy", "EFactionEnum::Poland",
    "EFactionEnum::Finland", "EFactionEnum::Anzac", "EFactionEnum::Allies",
    "EFactionEnum::Neutral",
};
constexpr int16_t OriginalCount =
    static_cast<int16_t>(sizeof(OriginalNames) / sizeof(OriginalNames[0]));

Logger logger{};

void Say(const char* message) {
    if (logger) logger(message);
}

}  // namespace

// Android's equivalent of the Windows VirtualProtect dance. mprotect works on
// whole pages, so the range is expanded outward and the original protection is
// restored afterwards.
bool WriteProtected(void* address, const void* data, size_t size) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    const uintptr_t pageStart = start & ~static_cast<uintptr_t>(pageSize - 1);
    const uintptr_t end = start + size;
    const uintptr_t pageEnd =
        (end + static_cast<uintptr_t>(pageSize) - 1) & ~static_cast<uintptr_t>(pageSize - 1);
    const size_t span = pageEnd - pageStart;

    if (mprotect(reinterpret_cast<void*>(pageStart), span,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
    std::memcpy(address, data, size);
    __builtin___clear_cache(reinterpret_cast<char*>(const_cast<void*>(address)),
                            reinterpret_cast<char*>(const_cast<void*>(address)) + size);
    // Best effort: restore read+execute. Failure here is not fatal for a
    // one-shot write, so the result is deliberately not checked.
    mprotect(reinterpret_cast<void*>(pageStart), span, PROT_READ | PROT_EXEC);
    return true;
}

bool NoEnumYet(const Addresses& addresses) {
    if (!addresses.singleton || !addresses.staticSingleton) {
        // TODO: both addresses are still unknown for 1.57.26587 -- see
        // docs/ANDROID_RE.md §5.2. Until they are located this check cannot be
        // performed and the caller must decide whether to proceed anyway.
        Say("WARN: singleton addresses unknown; cannot confirm the enum is unconstructed");
        return true;
    }
    const auto* first = reinterpret_cast<void* const*>(addresses.singleton);
    const auto* second = reinterpret_cast<void* const*>(addresses.staticSingleton);
    return *first == nullptr && *second == nullptr;
}

bool ValidateEnum(const Addresses& addresses, Logger log) {
    logger = log;
    if (!addresses.imageBase) {
        Say("ERROR: image base is zero");
        return false;
    }
    auto* params = reinterpret_cast<EnumParams*>(addresses.enumParams);

    // Mirrors the Windows-side field checks one-for-one. Every expected value
    // below was read out of libUnreal.so 1.57.26587 and is recorded in
    // profiles/kards-1.57.26587-arm64.json.
    if (params->entries != reinterpret_cast<Enumerator*>(addresses.enumerators) ||
        params->count != OriginalCount ||
        params->outer != reinterpret_cast<void*>(addresses.outer) ||
        params->display != nullptr ||
        params->flags != 0x45 ||
        params->enumFlags != 0 ||
        params->cppForm != 2 ||
        params->name != reinterpret_cast<const char*>(addresses.enumName) ||
        params->cppType != params->name ||
        strcmp(params->name, "EFactionEnum") != 0) {
        Say("ERROR: FEnumParams no longer matches the expected layout; refusing to install");
        return false;
    }

    // Each original entry must point inside the image and carry value == index.
    const uintptr_t lo = addresses.imageBase;
    for (int i = 0; i < OriginalCount; ++i) {
        const Enumerator& item = params->entries[i];
        const uintptr_t name = reinterpret_cast<uintptr_t>(item.name);
        if (name < lo || item.value != i || strcmp(item.name, OriginalNames[i]) != 0) {
            char message[192]{};
            snprintf(message, sizeof message,
                     "ERROR: original enum entry %d differs (value=%lld)", i,
                     static_cast<long long>(item.value));
            Say(message);
            return false;
        }
    }
    Say("OK: FEnumParams and all 13 original entries match the expected layout");
    return true;
}

bool Install(const Addresses& addresses, Logger log) {
    logger = log;

    // ---- Step 1: validate. This part is implemented and structurally sound. ----
    if (!ValidateEnum(addresses, log)) return false;
    if (!NoEnumYet(addresses)) {
        Say("ERROR: EFactionEnum already constructed; refusing a late install");
        return false;
    }

    // ---- Step 2: locate the three conversion functions. NOT DONE. ----
    // docs/ANDROID_RE.md §5.1: scanning .text for ADRP+ADD and for the GOT
    // access pattern both came up empty, so the addresses are unknown. Nothing
    // below can be completed until this is resolved by full disassembly plus
    // dynamic debugging.
    if (!addresses.nameToEnum || !addresses.encode || !addresses.decode) {
        Say("ERROR: conversion function addresses not located; skeleton cannot install");
        return false;
    }

    // ---- Step 3: build the expanded entry array. ----
    // Same shape as the Windows implementation: original entries first, then
    // configured factions with values continuing from OriginalCount.
    //
    //   static Enumerator expanded[OriginalCount + Config::FactionCount];
    //   copy original entries, append configured ones,
    //   then publish a replacement EnumParams whose `entries`/`count` point at it.
    //
    // NOTE: `expanded` and the replacement params must stay alive for the whole
    // process lifetime -- UE's reflection keeps referencing them after
    // registration completes.

    // ---- Step 4: install hooks. NOT DONE. ----
    // Windows used MinHook. On arm64 use Dobby or ShadowHook. The Windows
    // handler semantics must be preserved:
    //   * always call the original, even on a match -- the original owns and
    //     frees the by-value FString, so skipping it leaks (and freeing it
    //     ourselves would cross allocators);
    //   * Encode must reuse the original allocator: call it with ANZAC's value
    //     and then rewrite only the single output character, never allocate.

    // ---- Step 5: publish the enum. ----
    //   EnumParams replacement = *params;   // capture the original first
    //   replacement.entries = expanded;
    //   replacement.count = OriginalCount + Config::FactionCount;
    //   WriteProtected(params, &replacement, sizeof replacement);
    // Any failure past this point must roll back both the write and the hooks.

    Say("ERROR: Install() is an unimplemented skeleton -- see docs/ANDROID_RE.md");
    return false;
}

}  // namespace KardsFaction
