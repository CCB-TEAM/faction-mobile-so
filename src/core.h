// KARDS faction extension — Android arm64 port skeleton.
//
// STATUS: SKELETON. Not runtime-verified. See docs/ANDROID_RE.md.
//
// What IS verified (by static analysis of libUnreal.so 1.57.26587):
//   * the FEnumParams / Enumerator / FString layouts and their static_asserts
//   * every field value that ValidateEnum() checks
//   * the addresses in profiles/kards-1.57.26587-arm64.json
//
// What is NOT verified:
//   * the three conversion function addresses (not yet located)
//   * hook installation and entry timing
//   * that any of this produces a working in-game faction
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <cstddef>

namespace KardsFaction {

// UE's TCHAR is char16_t on Android/Linux (PLATFORM_TCHAR_IS_CHAR16), NOT
// wchar_t -- wchar_t is 4 bytes there. The Windows original used wchar_t*;
// getting this wrong still passes the size assert below (the pointer is 8
// bytes either way) and only fails at runtime as a total match miss.
using TChar = char16_t;

// TArray<TCHAR> = { TCHAR* Data; int32 ArrayNum; int32 ArrayMax }
struct FString {
    TChar* data;
    int32_t num;
    int32_t capacity;
};

// Reflection enum entry. Layout is identical to the Windows build.
struct Enumerator {
    const char* name;
    int64_t value;
};

// UE's FEnumParams, as it exists in the shipping binary.
struct EnumParams {
    void* outer;
    void* display;
    const char* name;
    const char* cppType;
    const Enumerator* entries;
    uint32_t flags;
    int16_t count;
    uint8_t enumFlags;
    uint8_t cppForm;
};

// Verified against libUnreal.so 1.57.26587 arm64 (see docs/ANDROID_RE.md §3).
static_assert(sizeof(FString) == 16);
static_assert(sizeof(Enumerator) == 16);
static_assert(sizeof(EnumParams) == 48);
static_assert(offsetof(EnumParams, count) == 44);

using Logger = void (*)(const char*);
using ParseFn = uint8_t (*)(void*, FString*);
using EncodeFn = FString* (*)(void*, FString*, uint8_t);

// Addresses for one game build. Populated from profiles/<id>.json by the
// build, or hardcoded here once the toolchain generates a header.
struct Addresses {
    uintptr_t imageBase;
    uintptr_t enumParams;      // 0x98ed298 for 1.57.26587
    uintptr_t enumerators;     // 0x98ed1c8
    uintptr_t enumName;        // 0x261865e
    uintptr_t outer;           // 0x6e874f4
    // NOT YET LOCATED -- must be filled in before Install() can work.
    uintptr_t nameToEnum;      // TODO
    uintptr_t encode;          // TODO
    uintptr_t decode;          // TODO
    uintptr_t singleton;       // TODO
    uintptr_t staticSingleton; // TODO
};

// Writes to a possibly read-only page. Android's equivalent of the Windows
// VirtualProtect path: mprotect the surrounding pages, copy, restore.
bool WriteProtected(void* address, const void* data, size_t size);

// Reads the live FEnumParams and verifies it still matches the expected
// original layout. This is the port of the Windows-side validation and the
// only part of the pipeline confirmed to be structurally sound.
bool ValidateEnum(const Addresses& addresses, Logger log);

// True when neither singleton has been set yet, i.e. the enum has not been
// constructed. Mirrors the Windows NoEnumYet().
bool NoEnumYet(const Addresses& addresses);

// Installs the extension. SKELETON: the hook wiring is not implemented.
bool Install(const Addresses& addresses, Logger log);

}  // namespace KardsFaction
