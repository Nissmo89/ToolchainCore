# ToolchainCore: Scope and Architecture

This document defines the toolchain-only module for `nord_C`.

`ToolchainCore` is a headless Qt C++ library that owns compiler discovery, toolchain normalization, build configuration, disk compilation, and executable launch. It does not own editor diagnostics, code navigation, or any LSP plumbing.

The implementation strategy is intentionally narrow:

1. Make the external compiler path reliable first.
2. Model the public API with explicit data types.
3. Add internal `libtcc` execution as an optional backend after the compile/run core is stable.
4. Keep the module UI-agnostic so `nord_C` can call it from any layer.

---

## 1. Goals

The module should answer these questions:

- Which compiler is available on this machine?
- Which include and library paths should be passed to it?
- How should a compile job be described and reported?
- Can the produced binary be launched and captured from the IDE?

The code should be predictable, testable, and easy to embed into the editor.

---

## 2. Non-Goals

These are explicitly out of scope for ToolchainCore:

- LSP / `clangd` integration
- semantic diagnostics and code completion
- editor UI code
- project dependency resolution
- package manager integration
- build graph orchestration across multiple targets

Those pieces can live in separate modules and be merged later.

---

## 3. Refined Architecture

### 3.1 Namespace and Directory Layout

```text
src/
└── ccc/
    ├── ccc_global.h
    ├── types.h
    ├── environment.h/.cpp
    ├── engine.h/.cpp
    └── backends/
        ├── compilerbackend.h
        └── externalcompilerbackend.h/.cpp
```

This first pass intentionally focuses on the external compiler backend. The internal TCC backend is reserved for a later phase and should remain optional behind a build flag.

### 3.2 Public API Contract

```cpp
namespace ccc {

enum class Language {
    Auto,
    C,
    Cpp
};

enum class OutputType {
    Executable,
    StaticLibrary,
    SharedLibrary,
    Memory
};

enum class BackendKind {
    Auto,
    ExternalCompiler,
    InternalTcc
};

struct CompileConfig {
    Language language = Language::Auto;
    OutputType outputType = OutputType::Executable;
    BackendKind backendKind = BackendKind::Auto;
    QString compilerPath;
    QString archiverPath;
    QString languageStandard;
    QStringList includePaths;
    QStringList libraryPaths;
    QStringList libraries;
    QStringList definitions;
    QStringList compilerFlags;
    QString workingDirectory;
    bool captureOutput = true;
    bool positionIndependentCode = false;
};

struct CompilationResult {
    bool success = false;
    int exitCode = -1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QProcess::ProcessError processError = QProcess::UnknownError;
    QString program;
    QStringList arguments;
    QStringList commandLog;
    QString outputPath;
    QString stdOut;
    QString stdErr;
    QString errorMessage;
};

struct ExecutionResult {
    bool success = false;
    int exitCode = -1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QProcess::ProcessError processError = QProcess::UnknownError;
    QString program;
    QStringList arguments;
    QString stdOut;
    QString stdErr;
    QString errorMessage;
};

struct RunResult {
    CompilationResult compilation;
    ExecutionResult execution;
};

class Environment;
class Engine;
class CompilerBackend;

}
```

The key refinement is that compilation and execution are separate results. That avoids overloading a single struct with too many responsibilities.

---

## 4. Toolchain Responsibilities

### 4.1 `ccc::Environment`

`Environment` owns platform-aware discovery and path normalization:

- find a usable C compiler
- find a usable C++ compiler
- find an archiver for static libraries
- resolve executable suffixes
- normalize TCC include and library paths
- infer language from file extension when possible

### 4.2 `ccc::CompilerBackend`

This is the backend interface used by the engine. The first backend is the external compiler backend. Static-library output is expected to use an archiver when needed.

### 4.3 `ccc::Engine`

`Engine` is the public entry point for Nord_C:

- `compile(...)`
- `compileAndRun(...)`
- `runExecutable(...)`

Later, the engine can expose an optional memory-compilation path once the TCC backend is packaged and stable.

---

## 5. Backend Policy

The implementation policy is:

- external compiler is the default backend
- internal TCC is opt-in
- if a backend is unavailable, return a structured failure instead of guessing
- keep compiler flags and paths explicit so the IDE can show the exact command that ran

This keeps the module boring in the best possible way.

---

## 6. Build System

Primary build path for this repository is CMake.

Later, if `Nord_C` still needs qmake integration, the module can expose a `.pri` or `.pro` bridge. That is not the first step.

---

## 7. Phase Plan

### Phase 1

- create the public types
- implement toolchain discovery
- implement external compiler compilation
- implement executable launch and output capture

### Phase 2

- add optional internal TCC support
- support memory compilation and plugin-style execution

### Phase 3

- add tests and integration points for `Nord_C`
- remove any duplicated compile logic from the editor layer
