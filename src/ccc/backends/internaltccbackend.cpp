#include "ccc/backends/internaltccbackend.h"

#include "ccc/environment.h"

#include <QLibrary>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#include <memory>

namespace ccc {
namespace {

struct TCCState;

using TccNewFn = TCCState * (*)();
using TccDeleteFn = void (*)(TCCState *);
using TccSetOutputTypeFn = int (*)(TCCState *, int);
using TccAddIncludePathFn = int (*)(TCCState *, const char *);
using TccAddLibraryPathFn = int (*)(TCCState *, const char *);
using TccDefineSymbolFn = int (*)(TCCState *, const char *, const char *);
using TccAddLibraryFn = int (*)(TCCState *, const char *);
using TccAddFileFn = int (*)(TCCState *, const char *);
using TccSetOptionsFn = int (*)(TCCState *, const char *);
using TccRelocateFn = int (*)(TCCState *, void *);
using TccGetSymbolFn = void * (*)(TCCState *, const char *);

constexpr int kTccOutputMemory = 0;

void *tccRelocateAuto()
{
    return reinterpret_cast<void *>(1);
}

QString commandString(const QString &program, const QStringList &arguments)
{
    auto quote = [](const QString &value) {
        if (value.isEmpty()) {
            return QStringLiteral("\"\"");
        }

        const bool needsQuoting = value.contains(QLatin1Char(' '))
                || value.contains(QLatin1Char('\t'))
                || value.contains(QLatin1Char('"'));
        if (!needsQuoting) {
            return value;
        }

        QString escaped = value;
        escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    };

    QStringList pieces;
    pieces << quote(program);
    for (const QString &argument : arguments) {
        pieces << quote(argument);
    }
    return pieces.join(QLatin1Char(' '));
}

void appendUnique(QStringList &list, const QString &value)
{
    if (!value.isEmpty() && !list.contains(value)) {
        list.append(QDir::cleanPath(value));
    }
}

QString ensureAbsolutePath(const QString &path, const QString &workingDirectory)
{
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo info(path);
    if (info.isAbsolute()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    return QDir(workingDirectory).absoluteFilePath(path);
}

QStringList normalizePaths(const QStringList &paths, const QString &workingDirectory)
{
    QStringList normalized;
    for (const QString &path : paths) {
        appendUnique(normalized, ensureAbsolutePath(path, workingDirectory));
    }
    return normalized;
}

QStringList tccLibraryCandidates()
{
    QStringList names;
#ifdef Q_OS_WIN
    names << QStringLiteral("libtcc.dll") << QStringLiteral("tcc.dll");
#elif defined(Q_OS_MACOS)
    names << QStringLiteral("libtcc.dylib");
#else
    names << QStringLiteral("libtcc.so.1") << QStringLiteral("libtcc.so");
#endif

    QStringList candidates;
    for (const QString &root : Environment::resolveTccLibraryPaths()) {
        for (const QString &name : names) {
            appendUnique(candidates, QDir(root).filePath(name));
        }
    }

    for (const QString &name : names) {
        appendUnique(candidates, name);
    }

    return candidates;
}

template <typename Fn>
bool resolveSymbol(QLibrary &library, Fn &function, const char *name)
{
    function = reinterpret_cast<Fn>(library.resolve(name));
    return function != nullptr;
}

struct TccRuntime {
    QLibrary library;
    TccNewFn tccNew = nullptr;
    TccDeleteFn tccDelete = nullptr;
    TccSetOutputTypeFn tccSetOutputType = nullptr;
    TccAddIncludePathFn tccAddIncludePath = nullptr;
    TccAddLibraryPathFn tccAddLibraryPath = nullptr;
    TccDefineSymbolFn tccDefineSymbol = nullptr;
    TccAddLibraryFn tccAddLibrary = nullptr;
    TccAddFileFn tccAddFile = nullptr;
    TccSetOptionsFn tccSetOptions = nullptr;
    TccRelocateFn tccRelocate = nullptr;
    TccGetSymbolFn tccGetSymbol = nullptr;
    bool available = false;
    QString errorMessage;
};

std::unique_ptr<TccRuntime> loadRuntime()
{
    auto runtime = std::make_unique<TccRuntime>();
    const QStringList candidates = tccLibraryCandidates();

    QString lastError;
    for (const QString &candidate : candidates) {
        runtime->tccNew = nullptr;
        runtime->tccDelete = nullptr;
        runtime->tccSetOutputType = nullptr;
        runtime->tccAddIncludePath = nullptr;
        runtime->tccAddLibraryPath = nullptr;
        runtime->tccDefineSymbol = nullptr;
        runtime->tccAddLibrary = nullptr;
        runtime->tccAddFile = nullptr;
        runtime->tccSetOptions = nullptr;
        runtime->tccRelocate = nullptr;
        runtime->tccGetSymbol = nullptr;

        runtime->library.setFileName(candidate);
        if (!runtime->library.load()) {
            lastError = runtime->library.errorString();
            continue;
        }

        if (!resolveSymbol(runtime->library, runtime->tccNew, "tcc_new") ||
            !resolveSymbol(runtime->library, runtime->tccDelete, "tcc_delete") ||
            !resolveSymbol(runtime->library, runtime->tccSetOutputType, "tcc_set_output_type") ||
            !resolveSymbol(runtime->library, runtime->tccAddIncludePath, "tcc_add_include_path") ||
            !resolveSymbol(runtime->library, runtime->tccAddLibraryPath, "tcc_add_library_path") ||
            !resolveSymbol(runtime->library, runtime->tccDefineSymbol, "tcc_define_symbol") ||
            !resolveSymbol(runtime->library, runtime->tccAddLibrary, "tcc_add_library") ||
            !resolveSymbol(runtime->library, runtime->tccAddFile, "tcc_add_file") ||
            !resolveSymbol(runtime->library, runtime->tccSetOptions, "tcc_set_options") ||
            !resolveSymbol(runtime->library, runtime->tccRelocate, "tcc_relocate") ||
            !resolveSymbol(runtime->library, runtime->tccGetSymbol, "tcc_get_symbol")) {
            lastError = QStringLiteral("Loaded TinyCC library is missing required symbols.");
            runtime->library.unload();
            continue;
        }

        runtime->available = true;
        runtime->errorMessage.clear();
        return runtime;
    }

    runtime->errorMessage = lastError.isEmpty()
            ? QStringLiteral("Unable to load libtcc. Install TinyCC or set TOOLCHAINCORE_TCC_ROOT.")
            : QStringLiteral("Unable to load libtcc: %1").arg(lastError);
    return runtime;
}

const TccRuntime &runtime()
{
    static const std::unique_ptr<TccRuntime> instance = loadRuntime();
    return *instance;
}

bool applyOption(const TccRuntime &runtime, TCCState *state, const QString &option, QString *errorMessage)
{
    if (option.isEmpty()) {
        return true;
    }

    const QByteArray optionBytes = option.toLocal8Bit();
    if (runtime.tccSetOptions(state, optionBytes.constData()) < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("TinyCC rejected compiler option: %1").arg(option);
        }
        return false;
    }

    return true;
}

bool applyCompilerOptions(const TccRuntime &runtime, TCCState *state, const CompileConfig &config, QString *errorMessage)
{
    if (!config.languageStandard.isEmpty()) {
        if (!applyOption(runtime, state, QStringLiteral("-std=%1").arg(config.languageStandard), errorMessage)) {
            return false;
        }
    }

    if (config.positionIndependentCode) {
        if (!applyOption(runtime, state, QStringLiteral("-fPIC"), errorMessage)) {
            return false;
        }
    }

    if (config.debugInfo) {
        if (!applyOption(runtime, state, QStringLiteral("-g"), errorMessage)) {
            return false;
        }
    }

    QString optFlag;
    switch (config.optimizationLevel) {
    case OptimizationLevel::None:
    case OptimizationLevel::Debug:
        optFlag = QStringLiteral("-O0");
        break;
    case OptimizationLevel::Speed:
        optFlag = QStringLiteral("-O2");
        break;
    case OptimizationLevel::Size:
        optFlag = QStringLiteral("-O1");
        break;
    }
    if (!applyOption(runtime, state, optFlag, errorMessage)) {
        return false;
    }

    for (const QString &flag : config.compilerFlags) {
        if (!applyOption(runtime, state, flag, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool applyIncludePaths(const TccRuntime &runtime,
                       TCCState *state,
                       const QStringList &paths,
                       const QString &workingDirectory,
                       QString *errorMessage)
{
    QStringList normalized = normalizePaths(paths, workingDirectory);
    for (const QString &path : Environment::resolveTccIncludePaths()) {
        if (!QFileInfo(path).exists()) {
            continue;
        }
        appendUnique(normalized, path);
    }

    for (const QString &path : normalized) {
        const QByteArray pathBytes = path.toLocal8Bit();
        if (runtime.tccAddIncludePath(state, pathBytes.constData()) < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TinyCC rejected include path: %1").arg(path);
            }
            return false;
        }
    }

    return true;
}

bool applyLibraryPaths(const TccRuntime &runtime,
                       TCCState *state,
                       const QStringList &paths,
                       const QString &workingDirectory,
                       QString *errorMessage)
{
    QStringList normalized = normalizePaths(paths, workingDirectory);
    for (const QString &path : Environment::resolveTccLibraryPaths()) {
        if (!QFileInfo(path).exists()) {
            continue;
        }
        appendUnique(normalized, path);
    }

    for (const QString &path : normalized) {
        const QByteArray pathBytes = path.toLocal8Bit();
        if (runtime.tccAddLibraryPath(state, pathBytes.constData()) < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TinyCC rejected library path: %1").arg(path);
            }
            return false;
        }
    }

    return true;
}

bool applyDefinitions(const TccRuntime &runtime,
                      TCCState *state,
                      const QStringList &definitions,
                      QString *errorMessage)
{
    for (const QString &definition : definitions) {
        QString normalized = definition.trimmed();
        if (normalized.isEmpty()) {
            continue;
        }

        if (normalized.startsWith(QLatin1String("-D"))) {
            normalized = normalized.mid(2);
        }

        QString name = normalized;
        QString value = QStringLiteral("1");
        const qsizetype equalsIndex = normalized.indexOf(QLatin1Char('='));
        if (equalsIndex >= 0) {
            name = normalized.left(equalsIndex);
            value = normalized.mid(equalsIndex + 1);
        }

        if (name.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Invalid preprocessor definition: %1").arg(definition);
            }
            return false;
        }

        const QByteArray nameBytes = name.toLocal8Bit();
        const QByteArray valueBytes = value.toLocal8Bit();
        if (runtime.tccDefineSymbol(state, nameBytes.constData(), valueBytes.constData()) < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TinyCC rejected definition: %1").arg(definition);
            }
            return false;
        }
    }

    return true;
}

bool applyLibraries(const TccRuntime &runtime,
                    TCCState *state,
                    const QStringList &libraries,
                    const QString &workingDirectory,
                    QString *errorMessage)
{
    for (const QString &library : libraries) {
        QString normalized = library.trimmed();
        if (normalized.isEmpty()) {
            continue;
        }

        if (normalized.startsWith(QLatin1String("-Wl,")) || normalized.startsWith(QLatin1String("-framework"))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TinyCC does not support linker option: %1").arg(library);
            }
            return false;
        }

        if (normalized.startsWith(QLatin1String("-l"))) {
            normalized = normalized.mid(2);
        }

        if (normalized.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Invalid library entry: %1").arg(library);
            }
            return false;
        }

        const bool looksLikePath = normalized.contains(QLatin1Char('/')) || normalized.contains(QLatin1Char('\\'));
        if (looksLikePath) {
            const QString absolutePath = ensureAbsolutePath(normalized, workingDirectory);
            const QByteArray pathBytes = absolutePath.toLocal8Bit();
            if (runtime.tccAddFile(state, pathBytes.constData()) < 0) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("TinyCC rejected library file: %1").arg(absolutePath);
                }
                return false;
            }
            continue;
        }

        const QByteArray libraryBytes = normalized.toLocal8Bit();
        if (runtime.tccAddLibrary(state, libraryBytes.constData()) < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("TinyCC rejected library: %1").arg(library);
            }
            return false;
        }
    }

    return true;
}

QStringList buildPseudoArguments(const QString &sourcePath, const CompileConfig &config, const QString &workingDirectory)
{
    QStringList arguments;

    if (!config.languageStandard.isEmpty()) {
        arguments << QStringLiteral("-std=%1").arg(config.languageStandard);
    }

    if (config.positionIndependentCode) {
        arguments << QStringLiteral("-fPIC");
    }

    for (const QString &path : normalizePaths(config.includePaths, workingDirectory)) {
        arguments << (QStringLiteral("-I") + path);
    }

    for (const QString &path : normalizePaths(config.libraryPaths, workingDirectory)) {
        arguments << (QStringLiteral("-L") + path);
    }

    for (const QString &definition : config.definitions) {
        QString normalized = definition.trimmed();
        if (normalized.startsWith(QLatin1String("-D"))) {
            normalized = normalized.mid(2);
        }
        if (!normalized.isEmpty()) {
            arguments << (QStringLiteral("-D") + normalized);
        }
    }

    for (const QString &library : config.libraries) {
        QString normalized = library.trimmed();
        if (normalized.isEmpty()) {
            continue;
        }

        if (normalized.startsWith(QLatin1String("-l")) ||
            normalized.startsWith(QLatin1String("-Wl,")) ||
            normalized.startsWith(QLatin1String("-framework"))) {
            if (normalized.startsWith(QLatin1String("-l"))) {
                normalized = normalized.mid(2);
                if (normalized.isEmpty()) {
                    continue;
                }
                arguments << (QStringLiteral("-l") + normalized);
                continue;
            }
            arguments << normalized;
            continue;
        }

        const bool looksLikePath = normalized.contains(QLatin1Char('/')) || normalized.contains(QLatin1Char('\\'));
        if (looksLikePath) {
            arguments << ensureAbsolutePath(normalized, workingDirectory);
        } else {
            arguments << (QStringLiteral("-l") + normalized);
        }
    }

    arguments << config.compilerFlags;
    arguments << sourcePath;
    return arguments;
}

} // namespace

QString InternalTccBackend::name() const
{
    return QStringLiteral("internal-tcc");
}

bool InternalTccBackend::isAvailable() const
{
    return runtime().available;
}

CompilationResult InternalTccBackend::compile(const QString &sourcePath,
                                              const QString &outputPath,
                                              const CompileConfig &config) const
{
    CompilationResult failure;

    if (sourcePath.isEmpty()) {
        failure.errorMessage = QStringLiteral("Source path is empty.");
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    if (config.outputType != OutputType::Memory) {
        failure.errorMessage = QStringLiteral("Internal TCC backend currently only supports OutputType::Memory.");
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    const TccRuntime &tcc = runtime();
    if (!tcc.available) {
        failure.errorMessage = tcc.errorMessage;
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    const QString workingDirectory = config.workingDirectory.isEmpty()
            ? QFileInfo(sourcePath).absolutePath()
            : QDir(config.workingDirectory).absolutePath();
    const QString absoluteSourcePath = ensureAbsolutePath(sourcePath, workingDirectory);
    (void)outputPath;

    failure.program = tcc.library.fileName().isEmpty() ? QStringLiteral("libtcc") : tcc.library.fileName();
    failure.arguments = buildPseudoArguments(absoluteSourcePath, config, workingDirectory);
    failure.commandLog << commandString(failure.program, failure.arguments);

    if (!QFileInfo::exists(absoluteSourcePath)) {
        failure.errorMessage = QStringLiteral("Source file does not exist: %1").arg(absoluteSourcePath);
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    Language language = config.language;
    if (language == Language::Auto) {
        language = Environment::languageFromSourceFile(absoluteSourcePath);
    }

    if (language != Language::C) {
        failure.errorMessage = QStringLiteral("Internal TCC backend only supports C sources.");
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    std::shared_ptr<TCCState> state(tcc.tccNew(), tcc.tccDelete);
    if (state == nullptr) {
        failure.errorMessage = QStringLiteral("Unable to create a TinyCC compilation state.");
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    if (tcc.tccSetOutputType(state.get(), kTccOutputMemory) < 0) {
        failure.errorMessage = QStringLiteral("TinyCC rejected memory output mode.");
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    QString compileError;
    if (!applyCompilerOptions(tcc, state.get(), config, &compileError) ||
        !applyIncludePaths(tcc, state.get(), config.includePaths, workingDirectory, &compileError) ||
        !applyLibraryPaths(tcc, state.get(), config.libraryPaths, workingDirectory, &compileError) ||
        !applyDefinitions(tcc, state.get(), config.definitions, &compileError) ||
        !applyLibraries(tcc, state.get(), config.libraries, workingDirectory, &compileError)) {
        failure.errorMessage = compileError.isEmpty()
                ? QStringLiteral("TinyCC rejected one or more configuration values.")
                : compileError;
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    const QByteArray sourceBytes = absoluteSourcePath.toLocal8Bit();
    if (tcc.tccAddFile(state.get(), sourceBytes.constData()) < 0) {
        failure.errorMessage = QStringLiteral("TinyCC failed to compile source file: %1").arg(absoluteSourcePath);
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    if (tcc.tccRelocate(state.get(), tccRelocateAuto()) < 0) {
        failure.errorMessage = QStringLiteral("TinyCC failed to relocate memory output.");
        if (config.captureOutput) {
            failure.stdErr = failure.errorMessage;
        }
        return failure;
    }

    failure.success = true;
    failure.outputPath.clear();
    failure.errorMessage.clear();
    failure.jitContext = state;
    return failure;
}

CompilationResult InternalTccBackend::link(const QStringList &objectPaths,
                                           const QString &outputPath,
                                           const CompileConfig &config) const
{
    (void)objectPaths;
    (void)outputPath;
    (void)config;
    CompilationResult failure;
    failure.errorMessage = QStringLiteral("Internal TCC backend does not support linking object files.");
    if (config.captureOutput) {
        failure.stdErr = failure.errorMessage;
    }
    return failure;
}

void* InternalTccBackend::getSymbolAddress(const std::shared_ptr<void> &context, const QString &symbolName)
{
    if (!context) {
        return nullptr;
    }
    const TccRuntime &tcc = runtime();
    if (!tcc.available || tcc.tccGetSymbol == nullptr) {
        return nullptr;
    }

    TCCState *state = static_cast<TCCState *>(context.get());
    const QByteArray nameBytes = symbolName.toLocal8Bit();
    return tcc.tccGetSymbol(state, nameBytes.constData());
}

} // namespace ccc
