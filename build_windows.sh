#!/bin/sh
# Build SCCTLink.exe (32-bit Windows) with the system's clang + lld only.
# No Windows SDK and no downloaded toolchain: the few Windows functions used are
# listed below and turned into import libraries with llvm-dlltool.
# Every DLL imported is a core Windows "KnownDLL" (loaded from System32 only);
# winmm.dll and iphlpapi.dll are loaded at run time from System32 only.
set -eu
cd "$(dirname "$0")"
OUT=build/win
mkdir -p "$OUT/lib"

mkdef() { # name, then exports
    name=$1; shift
    { echo "LIBRARY $name.dll"; echo "EXPORTS"; for e in "$@"; do echo "$e"; done; } > "$OUT/lib/$name.def"
    llvm-dlltool -m i386 -k -d "$OUT/lib/$name.def" -l "$OUT/lib/lib$name.a"
}
mkdef kernel32 Sleep@4 GetTickCount64@0 SetConsoleCtrlHandler@8 GetModuleFileNameA@12 \
    SetCurrentDirectoryA@4 GlobalAlloc@8 GlobalLock@4 GlobalUnlock@4 SetConsoleTitleA@4 \
    SetConsoleOutputCP@4 LoadLibraryExA@12 GetProcAddress@8 OpenProcess@12 \
    QueryFullProcessImageNameA@16 CloseHandle@4 GetCurrentProcessId@0
mkdef user32 OpenClipboard@4 EmptyClipboard@0 SetClipboardData@8 CloseClipboard@0
mkdef shell32 ShellExecuteA@24
mkdef ws2_32 WSAStartup@8 socket@12 bind@12 connect@12 listen@8 accept@12 send@16 recv@16 \
    sendto@24 recvfrom@24 select@20 closesocket@4 ioctlsocket@12 getsockname@12 setsockopt@20 \
    gethostname@8 gethostbyname@4 WSAGetLastError@0 WSAIoctl@36
mkdef msvcrt printf _vsnprintf strlen strcmp strncmp strchr strstr memcpy memmove memset memcmp \
    atoi malloc calloc realloc free fopen fread fwrite fclose fgets fflush exit __iob_func \
    _kbhit _getch _ui64toa _strtoui64 __getmainargs

CFLAGS="--target=i686-w64-windows-gnu -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
  -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fno-asynchronous-unwind-tables
  -fno-ident -fno-builtin -D_WIN32"
SRC="src/crt0_win.c src/scctlink.c src/plat_win.c src/steam_win.c src/sha256.c"

# shellcheck disable=SC2086
clang $CFLAGS -fuse-ld=lld -nostdlib -nostartfiles \
    -Wl,-e,mainCRTStartup -Wl,--subsystem,console:6.0 -Wl,--dynamicbase -Wl,--nxcompat \
    -Wl,--no-insert-timestamp -Wl,--build-id=none \
    -o "$OUT/SCCTLink.exe" $SRC \
    -L"$OUT/lib" -lkernel32 -luser32 -lshell32 -lws2_32 -lmsvcrt
echo "built $OUT/SCCTLink.exe"
sha256sum "$OUT/SCCTLink.exe"
