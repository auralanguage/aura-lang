# Architecture Overview

Aura is a statically typed programming language with a shared frontend pipeline and pluggable backends. This document provides a high-level overview of the compiler architecture and data flow.

## High-Level Pipeline

```
Source Code (.aura) → Frontend Pipeline → [Backend Selection]
                                      ↓
                         Interpreter (Direct Execution)
                                      ↓
                         Packaged App (.exe with embedded sources)
```

```
Source Code (.aura) → Frontend Pipeline → Lowering Pipeline → C++ Backend
                                      ↓                        ↓
                         Interpreter (Direct Execution)    Generated C++
                                      ↓                        ↓
                         Packaged App (.exe)              g++/MSVC → .exe
```

## Frontend Pipeline (Shared)

All commands (`run`, `check`, `build`) use the same frontend pipeline for consistency and code reuse.

### 1. Lexical Analysis (Lexer)
- **Input**: Source code text
- **Output**: Token stream with file/line/column metadata
- **Features**: Handles keywords, identifiers, literals, operators, comments
- **Error Handling**: Reports lexical errors with precise location

### 2. Syntax Analysis (Parser)
- **Input**: Token stream
- **Output**: Abstract Syntax Tree (AST)
- **Features**: Operator precedence, expression parsing, statement parsing
- **Error Handling**: Syntax errors with location and expected tokens

### 3. Module Loading
- **Input**: AST with import statements
- **Output**: Merged AST with resolved imports
- **Features**: File-based imports, recursive loading, cycle detection
- **Error Handling**: Import resolution failures, circular dependency detection

### 4. Semantic Analysis (Type Checker)
- **Input**: Merged AST
- **Output**: Typed AST ready for execution
- **Features**: Type validation, symbol resolution, qualified name handling
- **Error Handling**: Type errors, undefined symbols, type mismatches

## Backend Options

### Interpreter Backend (Default)
- **Execution**: Direct AST evaluation with runtime scope stack
- **Packaging**: Embeds source files into standalone .exe
- **Advantages**: No external compiler required, fast iteration
- **Use Case**: Development, standalone applications

### C++ Backend (Optional)
- **Lowering**: AST → Typed IR → CFG/SSA IR → C++ Code Generation
- **Execution**: Generated C++ compiled with g++/MSVC
- **Advantages**: Native performance, optimization opportunities
- **Use Case**: Performance-critical applications

## Intermediate Representations

### Typed IR
- **Purpose**: High-level representation after semantic analysis
- **Features**: Typed expressions, control flow, function calls
- **Used By**: Both backends for initial lowering

### CFG/SSA IR
- **Purpose**: Low-level representation for optimization and code generation
- **Features**: Static Single Assignment form, explicit basic blocks, value IDs
- **Components**:
  - Basic blocks with parameters
  - Phi nodes for control flow joins
  - Explicit value numbering
  - Control flow edges (jumps, branches, returns)

## Optimization Pipeline

Applied to CFG/SSA IR before code generation:

1. **Constant Folding**: Safe scalar and string literal evaluation
2. **Branch Simplification**: Remove unreachable code, simplify conditions
3. **Copy Propagation**: Eliminate redundant value copies
4. **Collection Optimization**: Fold provable collection operations
5. **Dead Code Elimination**: Remove unused values and blocks

## Runtime Support

### Embedded Runtime
- **Purpose**: Execution environment for interpreter and packaged apps
- **Features**: Scope management, builtin functions, error handling
- **Packaging**: Statically linked into executables

### Generated Runtime
- **Purpose**: Support library for C++ backend output
- **Features**: Memory management, collection operations, I/O helpers
- **Distribution**: Shared library linked with generated code

## CLI Architecture

The command-line interface provides multiple entry points:

- **`aura run`**: Frontend → Interpreter → Execution
- **`aura check`**: Frontend → Validation (with optional IR dumps)
- **`aura build`**: Frontend → Lowering → Selected Backend → Executable
- **`aura test`**: Manifest-driven regression execution

## Key Design Principles

1. **Shared Frontend**: Consistent parsing and type checking across all commands
2. **Pluggable Backends**: Easy to add new execution targets
3. **Fail Fast**: Early error detection in pipeline stages
4. **Windows-First**: Toolchain optimized for Windows development
5. **Clean Separation**: Clear boundaries between pipeline stages
6. **Testable Components**: Each stage can be validated independently

## Data Flow Summary

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Source Files  │───▶│ Frontend Pipeline│───▶│  Backend Choice │
│   (.aura)       │    │ (Lexer→Parser→  │    │                 │
│                 │    │  Modules→Types) │    │                 │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                                       │
                    ┌──────────────────────────────────┼──────────────────────────────────┐
                    │                                  │                                 │
                    ▼                                  ▼                                 ▼
         ┌────────────────────┐             ┌────────────────────┐             ┌────────────────────┐
         │   Interpreter      │             │   Lowering         │             │   C++ Backend      │
         │   (Direct AST      │             │   (AST→IR→CFG)     │             │   (Code Gen)       │
         │   Execution)       │             │                    │             │                    │
         └────────────────────┘             └────────────────────┘             └────────────────────┘
                    │                                  │                                 │
                    ▼                                  ▼                                 ▼
         ┌────────────────────┐             ┌────────────────────┐             ┌────────────────────┐
         │   Packaged App     │             │   Optimization     │             │   Generated C++    │
         │   (.exe)           │             │   (Constant Fold)  │             │   (Source)         │
         └────────────────────┘             └────────────────────┘             └────────────────────┘
                                                                                      │
                                                                                      ▼
                                                                       ┌────────────────────┐
                                                                       │   Native Build     │
                                                                       │   (g++/MSVC)       │
                                                                       │                    │
                                                                       └────────────────────┘
                                                                                      │
                                                                                      ▼
                                                                       ┌────────────────────┐
                                                                       │   Executable       │
                                                                       │   (.exe)           │
                                                                       └────────────────────┘
```
