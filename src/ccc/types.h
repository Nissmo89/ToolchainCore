#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>
#include <memory>

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
    Memory,
    ObjectFile
};

enum class BackendKind {
    Auto,
    ExternalCompiler,
    InternalTcc
};

enum class CompilerFamily {
    Gnu,  // GCC, Clang
    Msvc, // MSVC
    Tcc   // TinyCC
};

inline QString toString(CompilerFamily family)
{
    switch (family) {
    case CompilerFamily::Gnu:
        return QStringLiteral("gnu");
    case CompilerFamily::Msvc:
        return QStringLiteral("msvc");
    case CompilerFamily::Tcc:
        return QStringLiteral("tcc");
    }
    return QStringLiteral("gnu");
}

inline QString toString(Language language)
{
    switch (language) {
    case Language::Auto:
        return QStringLiteral("auto");
    case Language::C:
        return QStringLiteral("c");
    case Language::Cpp:
        return QStringLiteral("cpp");
    }
    return QStringLiteral("auto");
}

inline QString toString(OutputType outputType)
{
    switch (outputType) {
    case OutputType::Executable:
        return QStringLiteral("executable");
    case OutputType::StaticLibrary:
        return QStringLiteral("static-library");
    case OutputType::SharedLibrary:
        return QStringLiteral("shared-library");
    case OutputType::Memory:
        return QStringLiteral("memory");
    case OutputType::ObjectFile:
        return QStringLiteral("object-file");
    }
    return QStringLiteral("executable");
}

inline QString toString(BackendKind backendKind)
{
    switch (backendKind) {
    case BackendKind::Auto:
        return QStringLiteral("auto");
    case BackendKind::ExternalCompiler:
        return QStringLiteral("external-compiler");
    case BackendKind::InternalTcc:
        return QStringLiteral("internal-tcc");
    }
    return QStringLiteral("auto");
}

enum class OptimizationLevel {
    None,
    Debug,
    Speed,
    Size
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
    OptimizationLevel optimizationLevel = OptimizationLevel::None;
    bool debugInfo = false;
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
    std::shared_ptr<void> jitContext;
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

}

