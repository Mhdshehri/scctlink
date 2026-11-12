#!/bin/sh
# Build the SCCT Fix (d3d9.dll, 32-bit) from fix/d3d9fix.c with the system's
# clang + lld only: no Windows SDK, no C runtime. The Windows functions it uses
# are listed below and turned into import libraries with llvm-dlltool.
# The same source and these flags give the same file every time.
set -eu
cd "$(dirname "$0")/.."
OUT=build/fix
mkdir -p "$OUT/lib"
mkdef() { # name, then exports
    name=$1; shift
    { echo "LIBRARY $name.dll"; echo "EXPORTS"; for e in "$@"; do echo "$e"; done; } > "$OUT/lib/$name.def"
    llvm-dlltool -m i386 -k -d "$OUT/lib/$name.def" -l "$OUT/lib/$name.lib"
}
mkdef kernel32 CreateFileA@28 DisableThreadLibraryCalls@4 GetModuleFileNameA@12 GetModuleHandleA@4 \
    GetProcAddress@8 GetSystemDirectoryA@8 GlobalFindAtomA@4 LoadLibraryA@4 QueryPerformanceCounter@4 \
    QueryPerformanceFrequency@4 SetUnhandledExceptionFilter@4 Sleep@4 VirtualProtect@16 WriteFile@20 VirtualAlloc@16 CreateThread@24 OpenEventA@12 CloseHandle@4 GetLastError@0 GetFileAttributesA@4 \
    GetPrivateProfileIntA@16 GetPrivateProfileStringA@24
mkdef user32 GetMonitorInfoA@8 GetWindowLongA@8 GetWindowRect@8 IsIconic@4 MonitorFromWindow@8 \
    RegisterHotKey@16 SetWindowLongA@12 SetWindowPos@28 ShowWindow@8 UnregisterHotKey@8 wsprintfA \
    RegisterClassExA@4 CreateWindowExA@48 RegisterRawInputDevices@12 GetRawInputData@20 GetMessageA@16 \
    DispatchMessageA@4 DefWindowProcA@16 GetForegroundWindow@0 GetAncestor@8 SetCursorPos@8 GetClientRect@8 ClientToScreen@8 EnumDisplaySettingsA@12 SetProcessDPIAware@0
mkdef winmm timeBeginPeriod@4

clang --target=i686-pc-windows-msvc -O2 -Wall -Wextra -fno-builtin -fno-stack-protector -ffreestanding \
    -c fix/d3d9fix.c -o "$OUT/d3d9fix.obj"
lld-link -dll -nodefaultlib -entry:DllEntry -safeseh:no -machine:x86 -subsystem:windows,6.0 \
    -export:Direct3DCreate9=_Direct3DCreate9@4 -Brepro -libpath:"$OUT/lib" \
    "$OUT/d3d9fix.obj" kernel32.lib user32.lib winmm.lib -out:"$OUT/d3d9.dll"
echo "built $OUT/d3d9.dll"
sha256sum "$OUT/d3d9.dll"

# The SCCT Versus Fix (testing): d3d8.dll for Versus\System
mkdef kernel32v CreateFileA@28 DisableThreadLibraryCalls@4 GetModuleFileNameA@12 GetProcAddress@8 \
    GetSystemDirectoryA@8 LoadLibraryA@4 Sleep@4 WriteFile@20 CloseHandle@4 CreateThread@24 VirtualQuery@12 \
    GetPrivateProfileStringA@24 SetThreadPriority@8 GetCurrentThread@0 GetCurrentProcess@0 \
    ReadProcessMemory@20 WriteProcessMemory@20 VirtualProtect@16 GetModuleHandleA@4 QueryPerformanceCounter@4 QueryPerformanceFrequency@4 \
    ReadFile@20 GetEnvironmentVariableA@12 FindFirstFileA@8 FindNextFileA@8 FindClose@4
sed -i 's/^LIBRARY kernel32v.dll/LIBRARY kernel32.dll/' "$OUT/lib/kernel32v.def"
llvm-dlltool -m i386 -k -d "$OUT/lib/kernel32v.def" -l "$OUT/lib/kernel32v.lib"
mkdef winmmv timeBeginPeriod@4
sed -i 's/^LIBRARY winmmv.dll/LIBRARY winmm.dll/' "$OUT/lib/winmmv.def"
llvm-dlltool -m i386 -k -d "$OUT/lib/winmmv.def" -l "$OUT/lib/winmmv.lib"
mkdef user32v wsprintfA GetAsyncKeyState@4
sed -i 's/^LIBRARY user32v.dll/LIBRARY user32.dll/' "$OUT/lib/user32v.def"
llvm-dlltool -m i386 -k -d "$OUT/lib/user32v.def" -l "$OUT/lib/user32v.lib"
clang --target=i686-pc-windows-msvc -O2 -Wall -Wextra -fno-builtin -fno-stack-protector -ffreestanding \
    -c fix/d3d8versus.c -o "$OUT/d3d8versus.obj"
lld-link -dll -nodefaultlib -entry:DllEntry -safeseh:no -machine:x86 -subsystem:windows,6.0 \
    -export:Direct3DCreate8=_Direct3DCreate8@4 -Brepro -libpath:"$OUT/lib" \
    "$OUT/d3d8versus.obj" kernel32v.lib user32v.lib winmmv.lib -out:"$OUT/d3d8.dll"
echo "built $OUT/d3d8.dll"
sha256sum "$OUT/d3d8.dll"
