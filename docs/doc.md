# ToolchainCore API Documentation

ToolchainCore is the headless Qt C++ toolchain module for `nord_C`.

Current scope:

- compiler and archiver discovery
- include and library path normalization
- compile configuration and result types
- external compiler based disk builds
- executable launch and output capture

LSP, diagnostics, and code navigation are not part of this module.

## Header Map

- `src/ccc/ccc_global.h`
- `src/ccc/types.h`
- `src/ccc/environment.h`
- `src/ccc/backends/compilerbackend.h`
- `src/ccc/backends/externalcompilerbackend.h`
- `src/ccc/engine.h`

## Namespace

All public APIs live in the `ccc` namespace.

---

## `ccc::Language`

Represents the source language the toolchain should use.

- `Language::Auto` - infer from file extension
- `Language::C` - compile as C
- `Language::Cpp` - compile as C++

Helper:

```cpp
QString text = ccc::toString(ccc::Language::Cpp); // "cpp"
```

Use `Auto` when you want the engine to infer the language from the source file name. The current inference logic recognizes common C and C++ file extensions.

Example:

```cpp
ccc::CompileConfig config;
config.language = ccc::Language::Auto;
```

---

## `ccc::OutputType`

Describes the kind of artifact to produce.

- `OutputType::Executable`
- `OutputType::StaticLibrary`
- `OutputType::SharedLibrary`
- `OutputType::Memory`

Helper:

```cpp
QString text = ccc::toString(ccc::OutputType::SharedLibrary); // "shared-library"
```

Current behavior:

- `Executable` is supported.
- `StaticLibrary` is supported through the external compiler backend plus an archiver.
- `SharedLibrary` is supported through the external compiler backend.
- `Memory` is reserved for the future internal `libtcc` backend and currently returns a failure in `ccc::Engine::compile`.

Example:

```cpp
ccc::CompileConfig config;
config.outputType = ccc::OutputType::Executable;
```

---

## `ccc::BackendKind`

Selects which backend should handle the build.

- `BackendKind::Auto`
- `BackendKind::ExternalCompiler`
- `BackendKind::InternalTcc`

Helper:

```cpp
QString text = ccc::toString(ccc::BackendKind::ExternalCompiler); // "external-compiler"
```

Current behavior:

- `Auto` resolves to the external compiler path in this build.
- `ExternalCompiler` is the implemented backend.
- `InternalTcc` is reserved for a later phase and currently returns a failure from `ccc::Engine::compile`.

Example:

```cpp
ccc::CompileConfig config;
config.backendKind = ccc::BackendKind::ExternalCompiler;
```

---

## `ccc::CompileConfig`

Describes how a compile job should run.

Fields:

- `language` - source language, or `Auto` to infer it
- `outputType` - executable, shared library, static library, or memory
- `backendKind` - backend selection
- `compilerPath` - explicit compiler to use, if you do not want PATH lookup
- `archiverPath` - explicit archiver for static library builds
- `languageStandard` - overrides the default standard flag
- `includePaths` - additional include search paths
- `libraryPaths` - additional library search paths
- `libraries` - libraries or linker entries to pass through
- `definitions` - preprocessor defines
- `compilerFlags` - raw compiler flags
- `workingDirectory` - directory used to resolve relative paths
- `captureOutput` - store stdout and stderr in the result
- `positionIndependentCode` - add `-fPIC` for code that needs it

Example:

```cpp
ccc::CompileConfig config;
config.language = ccc::Language::Cpp;
config.outputType = ccc::OutputType::Executable;
config.backendKind = ccc::BackendKind::ExternalCompiler;
config.languageStandard = "c++20";
config.includePaths = {"/opt/mylib/include"};
config.libraryPaths = {"/opt/mylib/lib"};
config.libraries = {"mylib"};
config.definitions = {"NDEBUG"};
config.compilerFlags = {"-O2", "-Wall"};
config.captureOutput = true;
```

How it is used:

- `Engine::compile(...)` consumes the config and normalizes the paths.
- `ExternalCompilerBackend::compile(...)` turns it into a compiler command line.

Important behavior:

- If `compilerPath` is set, that exact path is treated as authoritative.
- If `archiverPath` is set for static library builds, that exact path is treated as authoritative.
- If `language` is `Auto`, the engine infers it from the source file extension.

---

## `ccc::CompilationResult`

Represents the result of a compile step.

Fields:

- `success` - `true` when the compiler finished successfully
- `exitCode` - process exit code
- `exitStatus` - normal exit or crash/abort
- `processError` - `QProcess` error state
- `program` - resolved compiler or archiver program
- `arguments` - raw process arguments
- `commandLog` - readable command line log
- `outputPath` - resolved artifact path
- `stdOut` - captured stdout when `captureOutput` is enabled
- `stdErr` - captured stderr when `captureOutput` is enabled
- `errorMessage` - human-readable failure message

Example:

```cpp
ccc::CompilationResult result = engine.compile("main.cpp", "build/app", config);
if (!result.success) {
    qWarning() << result.errorMessage;
    qWarning() << result.stdErr;
}
```

How to read it:

- `success == true` means the backend completed without a process failure.
- `outputPath` is the final path the backend targeted.
- `commandLog` is useful for showing the exact build command in the IDE.

---

## `ccc::ExecutionResult`

Represents the result of running an executable.

Fields:

- `success`
- `exitCode`
- `exitStatus`
- `processError`
- `program`
- `arguments`
- `stdOut`
- `stdErr`
- `errorMessage`

Example:

```cpp
ccc::ExecutionResult runResult = engine.runExecutable("./build/app", {"--help"});
if (!runResult.success) {
    qWarning() << runResult.errorMessage;
}
```

Use it when the executable already exists and you only want launch and capture behavior.

---

## `ccc::RunResult`

Combines the compile and execution steps.

- `compilation` - result from `Engine::compile(...)`
- `execution` - result from `Engine::runExecutable(...)`

Example:

```cpp
ccc::RunResult result = engine.compileAndRun("main.cpp", "build/app", config);
if (!result.compilation.success) {
    qWarning() << "compile failed:" << result.compilation.errorMessage;
} else if (!result.execution.success) {
    qWarning() << "run failed:" << result.execution.errorMessage;
}
```

Current restriction:

- `compileAndRun(...)` requires `OutputType::Executable`.
- It will fail early for static library, shared library, or memory output types.

---

## `ccc::Environment`

Static helper class for toolchain discovery and path normalization.

### `executableSuffix()`

Returns the platform executable suffix.

Example:

```cpp
QString suffix = ccc::Environment::executableSuffix();
```

Behavior:

- returns `.exe` on Windows
- returns an empty string on other platforms

### `executablePath(const QString &basePath)`

Normalizes a base path and appends the executable suffix when needed.

Example:

```cpp
QString path = ccc::Environment::executablePath("/opt/toolchain/mytool");
```

### `languageFromSourceFile(const QString &sourcePath)`

Infers the language from file extension.

Recognized examples:

- `.c` -> `Language::C`
- `.cc`, `.cpp`, `.cxx`, `.c++`, `.cp`, `.mm` -> `Language::Cpp`

Example:

```cpp
ccc::Language lang = ccc::Environment::languageFromSourceFile("main.cpp");
```

### `compilerCandidates(Language language)`

Returns candidate compiler names for the given language.

Examples:

- C: `clang`, `gcc`, `cc`
- C++: `clang++`, `g++`, `c++`

### `findExternalCompiler(Language language = Language::Auto, const QString &preferred = QString())`

Resolves a compiler in this order:

1. if `preferred` is non-empty, try that exact path or name first
2. search PATH for language-specific compiler candidates

Example:

```cpp
QString compiler = ccc::Environment::findExternalCompiler(ccc::Language::Cpp);
```

### `findArchiver(const QString &preferred = QString())`

Finds an archiver for static library builds.

Resolution order:

1. `preferred`, if provided
2. PATH search for common archiver names

Example:

```cpp
QString archiver = ccc::Environment::findArchiver();
```

### `resolveTccIncludePaths()`

Returns candidate include paths for a future TCC backend.

It currently probes:

- `TOOLCHAINCORE_TCC_ROOT`
- `TCC_HOME`
- `TCCROOT`
- `TINYCC_HOME`
- app-local `tcc` directories
- common system paths

### `resolveTccLibraryPaths()`

Returns candidate library paths for a future TCC backend.

It uses the same root discovery strategy as the include-path resolver.

Note:

- These helpers are part of the public API, but the current engine does not yet consume the internal TCC backend.

---

## `ccc::CompilerBackend`

Abstract interface for compiler backends.

Methods:

- `name() const`
- `isAvailable() const`
- `compile(const QString &sourcePath, const QString &outputPath, const CompileConfig &config) const`

This is the extension point for future backends.

Example backend contract:

```cpp
class MyBackend final : public ccc::CompilerBackend {
public:
    QString name() const override { return "my-backend"; }
    bool isAvailable() const override { return true; }
    ccc::CompilationResult compile(const QString &sourcePath,
                                   const QString &outputPath,
                                   const ccc::CompileConfig &config) const override;
};
```

Use this when you want to add a different compiler implementation without changing the engine API.

---

## `ccc::ExternalCompilerBackend`

Concrete backend that drives the system compiler and archiver.

Public methods:

- `name() const`
- `isAvailable() const`
- `compile(...) const`

How it works:

1. resolves the compiler path
2. normalizes include paths, library paths, defines, and flags
3. invokes the compiler through `QProcess`
4. if `OutputType::StaticLibrary` is requested, it compiles an object file and archives it
5. captures stdout/stderr when configured to do so

Example:

```cpp
ccc::ExternalCompilerBackend backend;
if (backend.isAvailable()) {
    ccc::CompilationResult result = backend.compile("main.cpp", "build/app", config);
}
```

Internal details exposed by behavior, not by the API:

- `OutputType::Memory` is rejected here.
- static libraries use `archiverPath` when set, otherwise the backend searches for a system archiver.
- `compilerPath` and `archiverPath` are treated as authoritative when provided.

---

## `ccc::Engine`

The main entry point for `nord_C`.

### Constructor

```cpp
ccc::Engine engine;
```

The class is a `QObject`, so it can be owned by Qt object trees, but it currently does not expose signals or slots.

### `compile(const QString &sourcePath, const QString &outputPath, const CompileConfig &config) const`

Compiles a source file to a target artifact.

Behavior:

- normalizes the working directory
- infers language if needed
- rejects `OutputType::Memory` in the current build
- rejects `BackendKind::InternalTcc` in the current build
- uses the external compiler backend for supported builds

Example:

```cpp
ccc::Engine engine;
ccc::CompileConfig config;
config.language = ccc::Language::Cpp;
config.outputType = ccc::OutputType::Executable;
config.backendKind = ccc::BackendKind::ExternalCompiler;

ccc::CompilationResult result = engine.compile("main.cpp", "build/app", config);
```

### `compileAndRun(const QString &sourcePath, const QString &outputPath, const CompileConfig &config, const QStringList &runArguments = QStringList()) const`

Compiles a source file and then runs the produced executable.

Current restriction:

- `config.outputType` must be `OutputType::Executable`

Example:

```cpp
ccc::RunResult result = engine.compileAndRun("main.cpp", "build/app", config, {"--verbose"});
```

Typical use:

- IDE "build and run" action
- quick execution of the current file

### `runExecutable(const QString &executablePath, const QStringList &arguments = QStringList(), const QString &workingDirectory = QString()) const`

Launches an existing executable and captures output.

Example:

```cpp
ccc::ExecutionResult result = engine.runExecutable("build/app", {"--version"});
```

Use it when the binary already exists and you do not want to compile first.

---

## Suggested Usage Pattern

For a typical IDE flow:

```cpp
ccc::Engine engine;
ccc::CompileConfig config;
config.language = ccc::Language::Auto;
config.outputType = ccc::OutputType::Executable;
config.backendKind = ccc::BackendKind::ExternalCompiler;
config.languageStandard = "c++20";
config.captureOutput = true;

auto result = engine.compileAndRun("src/main.cpp", "build/main", config);

if (!result.compilation.success) {
    qWarning() << "compile failed:" << result.compilation.errorMessage;
    qWarning() << result.compilation.stdErr;
    return;
}

if (!result.execution.success) {
    qWarning() << "run failed:" << result.execution.errorMessage;
    qWarning() << result.execution.stdErr;
    return;
}

qDebug() << result.execution.stdOut;
```

For a build-only action:

```cpp
ccc::CompilationResult result = engine.compile("src/main.cpp", "build/main", config);
```

For a launch-only action:

```cpp
ccc::ExecutionResult result = engine.runExecutable("build/main");
```

---

## Notes For Integrators

- `compilerPath` is optional. Leave it empty to search the PATH.
- `archiverPath` is optional. Leave it empty to search the PATH for an archiver.
- `workingDirectory` is important when you pass relative include or library paths.
- `captureOutput` should usually stay enabled for IDE integration.
- `OutputType::Memory` and `BackendKind::InternalTcc` are placeholders for a later phase.

