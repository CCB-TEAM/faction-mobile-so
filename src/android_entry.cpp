// KARDS faction extension — Android arm64 port skeleton.
// STATUS: SKELETON, not runtime-verified. See docs/ANDROID_RE.md.
//
// Where the Windows build had DllMain + a hijacked PE entry point, Android has
// no equivalent, and none of that machinery transfers:
//
//   Windows                          Android
//   ------------------------------   ---------------------------------------
//   plant a fake dwmapi.dll          no loader search order to abuse; the APK
//                                    is signed and its lib/ dir is read-only
//   DllMain(DLL_PROCESS_ATTACH)      no DllMain; constructors run under the
//                                    same loader lock, with the same hazard
//   patch the PE entry point         ELF has no entry point to steal
//   forward every dwmapi export      nothing to forward -- we are injected,
//                                    not impersonating a system library
//
// So the whole proxy/forwarding half of upstream is deleted rather than
// ported. What remains is: get loaded, wait for the right moment, call
// KardsFaction::Install().
#include "core.h"

#include <cstdio>
#include <cstdlib>

namespace {

KardsFaction::Logger Logger() {
    return [](const char* message) { std::fprintf(stderr, "[faction] %s\n", message); };
}

// Filled in by the build from profiles/<id>.json. Conversion function
// addresses are still unknown -- see docs/ANDROID_RE.md §5.1.
KardsFaction::Addresses CurrentAddresses() {
    KardsFaction::Addresses addresses{};
    addresses.imageBase = reinterpret_cast<uintptr_t>(&CurrentAddresses);
    addresses.enumParams = 0x98ed298;
    addresses.enumerators = 0x98ed1c8;
    addresses.enumName = 0x261865e;
    addresses.outer = 0x6e874f4;
    // TODO: nameToEnum / encode / decode / singleton / staticSingleton
    return addresses;
}

}  // namespace

// The Windows build deliberately did almost nothing in DllMain and deferred the
// real work to the hijacked entry point, because MinHook initialisation and
// engine writes must not run under the loader lock. The same rule applies here:
// a constructor attribute runs under the dynamic linker's lock, so it may only
// do something bounded.
//
// TODO(entry-timing): pick and validate one of these. docs/ANDROID_RE.md §5.3.
//
//   (a) __attribute__((constructor)) below -- fires at dlopen time, i.e. before
//       the game's UE static registration. Closest to the Windows timing, but
//       the enum may not be constructed yet and hooking here is risky.
//   (b) JNI_OnLoad -- only fires if the library is loaded through
//       System.loadLibrary, which requires touching the Java side.
//   (c) An explicit init call from the injector (Frida / Zygote), after the
//       engine is up. Safest, but needs a way to pick the right moment.
//
// Which one is correct is an open question. Do not assume (a) works.
extern "C" __attribute__((constructor)) void FactionPreload() {
    std::fprintf(stderr, "[faction] preloaded; skeleton, install deferred\n");
}

// Called by the injector once the engine is running and EFactionEnum exists.
// Returns 0 on success, non-zero on refusal -- matching the upstream status
// convention (0 = not matched, 2 = installed, negative = failure).
extern "C" int KardsFactionInstall() {
    const auto addresses = CurrentAddresses();
    if (!KardsFaction::ValidateEnum(addresses, Logger())) {
        return -4;
    }
    if (!KardsFaction::Install(addresses, Logger())) {
        return -4;
    }
    return 2;
}

extern "C" int KardsFactionStatus() {
    // TODO: report real state once Install() is implemented.
    return 0;
}

extern "C" const char* KardsFactionVersion() {
    return "KardsFactionToolkit-android 0.1.0-skeleton";
}
