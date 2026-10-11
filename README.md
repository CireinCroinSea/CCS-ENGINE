# CCS engine

Native Windows process memory analysis utility written in C++ with the Win32 API. The interface and documentation are available in English and Russian.

**Author:** [cireincroinsea](https://t.me/cireincroinsea)

## Build requirements

- Windows 10/11 x64
- Visual Studio 2026 with **Desktop development with C++**
- Windows 10/11 SDK
- CMake

## Build

Open **x64 Native Tools Command Prompt for VS 2026**, change to this folder, and run:

```bat
build-windows-x64.bat
```

The executable is written to `Publish\CCS engine.exe`.

## Features

- Enumerate running processes
- Dark, DPI-aware Win32 interface with resizable panes and six selectable color themes
- Scan `Byte`, `2 Bytes`, `4 Bytes`, `8 Bytes`, `Float`, and `Double` values
- Refine scans by exact value, changed, unchanged, increased, and decreased comparisons
- Display matching addresses, current values, and value sizes
- Multi-select scan results and update selected writable values
- Change log, byte-pattern scan, memory editor, direct pointer-reference search, value freeze, and debugger event monitor
- English and Russian interface

## Limitations and responsible use

CCS engine is a user-mode memory-analysis utility, not a full Cheat Engine replacement. Process access and writes are subject to Windows permissions; the application does not bypass anti-cheat, inject code, or use a kernel driver. Only inspect processes you own or are authorized to analyze.

