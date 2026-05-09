# Aura

<p align="center">
  <img src="branding/aura-logo.svg" alt="Aura logo" width="220">
</p>

<p align="center">
  A small, fast, statically typed programming language with clean syntax and a Windows-first toolchain.
</p>

<p align="center">
  Repository: <a href="https://github.com/auralanguage/aura-lang">github.com/auralanguage/aura-lang</a>
</p>

## What Aura Is Trying To Be

Aura is being built around a simple goal:

- less complex than C++
- easier to approach than Rust
- faster and more structured than Python for compiled CLI workflows
- clean syntax
- fast feedback loop

Today the project already includes:

- a lexer, parser, AST, type checker, and interpreter
- imports and module-qualified symbols
- structs, methods, arrays, ranges, slices, and string helpers
- a typed IR plus CFG/SSA lowering and verification
- an optimizer pass for safe constant and collection folding
- a default embedded build path that produces a standalone `.exe` without requiring MinGW on the end-user machine
- an optional `cpp` backend for generated C++ output

## Project Status

Aura is currently **pre-alpha**.

The language core is real and usable for experiments, but the syntax, standard library, packaging model, and backend choices are still moving.

## Quick Start

### Example Code
```aura
fn main() {
    print("Hello, World!");
}
```

### Use A Prebuilt Release On Windows

Download one of these from GitHub Releases:

- `aura-windows-x64.zip`
- `AuraSetup-x64.exe` (sorry there is no setup for now)

Then run:

```powershell
aura.exe run examples/code_syntax.aura
```

Build a standalone app:

```powershell
aura.exe build examples/code_syntax.aura
```

Important:

- the default `embedded` backend does **not** require MinGW or MSVC on the user's machine
- `aura build --backend cpp` is optional and still requires a C++ compiler

## Build From Source

```powershell
.\build.bat
.\build\aura.exe test --manifest tests/test_cases.json --executable build/aura.exe
```

## Example Commands

Run a file:

```powershell
aura.exe run examples/code_syntax.aura
```

Check a file without running it:

```powershell
aura.exe check examples/code_syntax.aura
```

Build a standalone executable:

```powershell
aura.exe build examples/code_syntax.aura
```

Build with the optional C++ backend:

```powershell
aura.exe build examples/code_syntax.aura --backend cpp
```

Use the release profile from `Aura.toml`:

```powershell
aura.exe build --profile release
```

## Repository Layout

- `src/` - compiler, runtime, and CLI implementation
- `include/` - public headers
- `examples/` - small Aura programs
- `tests/` - regression suites and fixtures
- `scripts/` - helper scripts
- `packaging/` - Windows release packaging files

## Suggested GitHub Topics

- `programming-language`
- `compiler`
- `interpreter`
- `language-design`
- `cpp`
- `ssa`
- `toolchain`
- `windows`

## License

Aura is released under the MIT License. See [LICENSE](LICENSE).
