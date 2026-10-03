# KARDS faction extension — Android arm64 port skeleton.
#
# Cross-compile for Android with the NDK toolchain file:
#   cmake -S . -B build -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28
#
# NOTE: UE 5.3+ no longer ships libc++_shared.so in the APK and links the C++
# runtime statically. This library must do the same, hence c++_static below --
# mixing the two will crash on load.
cmake_minimum_required(VERSION 3.22)
project(faction_mobile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Hook backend: the Windows build used MinHook. Pick one of these on arm64.
#   Dobby       https://github.com/jmpews/Dobby
#   ShadowHook  https://github.com/bytedance/android-inline-hook
# Neither is vendored here -- add it under vendor/ and link it below.
set(FACTION_HOOK_BACKEND "" CACHE STRING "Dobby or ShadowHook; required to actually install")

add_library(faction_core STATIC core.cpp)
target_include_directories(faction_core PUBLIC .)
target_compile_options(faction_core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

# Address table for the supported game build. Once tools/find_faction_enum.py has
# located the three conversion functions, generate this header instead of
# hand-editing, mirroring upstream's tools/generate_profile.py.
add_library(faction_android SHARED android_entry.cpp)
target_link_libraries(faction_android PRIVATE faction_core)

if(FACTION_HOOK_BACKEND)
  message(STATUS "hook backend: ${FACTION_HOOK_BACKEND}")
else()
  message(WARNING "FACTION_HOOK_BACKEND is unset -- Install() cannot complete")
endif()

# Statically link the C++ runtime to match UE 5.3+ packaging.
target_link_options(faction_android PRIVATE
  -static-libstdc++
  -Wl,--no-undefined
  -Wl,-z,relro,-z,now)
