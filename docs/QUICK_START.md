# Quick Start For Aura

## Install the current version from releases

Download one of these from GitHub Releases:

- `aura-windows-x64.zip`
- `AuraSetup-x64.exe`(there is no setup for now)

Then run:

```powershell
aura.exe run examples/code_syntax.aura
```

Build a standalone app (executeable):

```powershell
aura.exe build examples/code_syntax.aura
```

Important:

- the default `embedded` backend does **not** require MinGW or MSVC on the user's machine
- `aura build --backend cpp` is optional and still requires a C++ compiler
