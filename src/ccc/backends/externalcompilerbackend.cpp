#include "ccc/backends/externalcompilerbackend.h"

#include "ccc/environment.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

namespace ccc {
namespace {

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

QString languageStandardFor(Language language, const QString &configuredStandard)
{
    if (!configuredStandard.isEmpty()) {
        return configuredStandard;
    }

    switch (language) {
    case Language::Cpp:
        return QStringLiteral("c++17");
    case Language::C:
    case Language::Auto:
        return QStringLiteral("c11");
    }

    return QStringLiteral("c11");
}

}

QString ExternalCompilerBackend::name() const
{
    return QStringLiteral("external-compiler");
}

bool ExternalCompilerBackend::isAvailable() const
{
    return !Environment::findExternalCompiler(Language::C).isEmpty() ||
           !Environment::findExternalCompiler(Language::Cpp).isEmpty();
}

QString ExternalCompilerBackend::resolvedCompiler(Language language, const QString &preferredCompiler)
{
    if (!preferredCompiler.isEmpty()) {
        return Environment::findExternalCompiler(language, preferredCompiler);
    }

    return Environment::findExternalCompiler(language);
}

QString ExternalCompilerBackend::resolvedArchiver(const QString &preferredArchiver)
{
    if (!preferredArchiver.isEmpty()) {
        return Environment::findArchiver(preferredArchiver);
    }

    return Environment::findArchiver();
}

QStringList ExternalCompilerBackend::normalizePathArgs(const QStringList &paths,
                                                       const QString &workingDirectory,
                                                       const QString &prefix)
{
    QStringList args;
    for (const QString &path : paths) {
        const QString absolutePath = ensureAbsolutePath(path, workingDirectory);
        if (!absolutePath.isEmpty()) {
            args << (prefix + absolutePath);
        }
    }
    return args;
}

QStringList ExternalCompilerBackend::normalizeDefinitions(const QStringList &definitions, const QString &prefix)
{
    QStringList args;
    for (const QString &definition : definitions) {
        if (definition.isEmpty()) {
            continue;
        }

        if (definition.startsWith(QLatin1String("-D")) || definition.startsWith(QLatin1String("/D"))) {
            args << (prefix + definition.mid(2));
        } else {
            args << (prefix + definition);
        }
    }
    return args;
}

QStringList ExternalCompilerBackend::normalizeLibrariesGnu(const QStringList &libraries)
{
    QStringList args;
    for (const QString &library : libraries) {
        if (library.isEmpty()) {
            continue;
        }

        if (library.startsWith(QLatin1String("-l")) ||
            library.startsWith(QLatin1String("-Wl,")) ||
            library.startsWith(QLatin1String("-framework"))) {
            args << library;
            continue;
        }

        const QFileInfo info(library);
        if (info.isAbsolute() || library.contains(QLatin1Char('/')) || library.contains(QLatin1Char('\\'))) {
            args << info.absoluteFilePath();
        } else {
            args << (QStringLiteral("-l") + library);
        }
    }
    return args;
}

QStringList ExternalCompilerBackend::normalizeLibrariesMsvc(const QStringList &libraries)
{
    QStringList args;
    for (const QString &library : libraries) {
        if (library.isEmpty()) {
            continue;
        }

        const QFileInfo info(library);
        if (info.isAbsolute() || library.contains(QLatin1Char('/')) || library.contains(QLatin1Char('\\'))) {
            args << info.absoluteFilePath();
        } else {
            if (library.endsWith(QLatin1String(".lib"), Qt::CaseInsensitive)) {
                args << library;
            } else {
                args << (library + QLatin1String(".lib"));
            }
        }
    }
    return args;
}

CompilationResult ExternalCompilerBackend::runProcess(const QString &program,
                                                      const QStringList &arguments,
                                                      const QString &workingDirectory,
                                                      bool captureOutput)
{
    CompilationResult result;
    result.program = program;
    result.arguments = arguments;
    result.commandLog << commandString(program, arguments);
    result.outputPath.clear();

    QProcess process;
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }

    process.start(program, arguments);
    if (!process.waitForStarted()) {
        result.success = false;
        result.processError = process.error();
        result.errorMessage = process.errorString();
        return result;
    }

    if (!process.waitForFinished(-1)) {
        process.kill();
        process.waitForFinished();
        result.success = false;
        result.processError = process.error();
        result.errorMessage = process.errorString().isEmpty()
                ? QStringLiteral("Process failed to finish.")
                : process.errorString();
        return result;
    }

    result.exitCode = process.exitCode();
    result.exitStatus = process.exitStatus();
    result.processError = process.error();
    result.success = (result.exitStatus == QProcess::NormalExit && result.exitCode == 0);

    const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());
    if (captureOutput) {
        result.stdOut = stdOut;
        result.stdErr = stdErr;
    }

    if (!result.success && result.errorMessage.isEmpty()) {
        if (result.exitStatus != QProcess::NormalExit) {
            result.errorMessage = QStringLiteral("Compiler terminated abnormally.");
        } else {
            result.errorMessage = QStringLiteral("Command failed with exit code %1.").arg(result.exitCode);
        }
    }

    return result;
}

CompilationResult ExternalCompilerBackend::compile(const QString &sourcePath,
                                                   const QString &outputPath,
                                                   const CompileConfig &config) const
{
    CompilationResult failure;

    if (sourcePath.isEmpty()) {
        failure.errorMessage = QStringLiteral("Source path is empty.");
        return failure;
    }

    if (outputPath.isEmpty()) {
        failure.errorMessage = QStringLiteral("Output path is empty.");
        return failure;
    }

    const QString workingDirectory = config.workingDirectory.isEmpty()
            ? QFileInfo(sourcePath).absolutePath()
            : config.workingDirectory;
    const QString absoluteSourcePath = ensureAbsolutePath(sourcePath, workingDirectory);
    const QString absoluteOutputPath = ensureAbsolutePath(outputPath, workingDirectory);
    failure.outputPath = absoluteOutputPath;

    if (!QFileInfo::exists(absoluteSourcePath)) {
        failure.errorMessage = QStringLiteral("Source file does not exist: %1").arg(absoluteSourcePath);
        return failure;
    }

    Language language = config.language;
    if (language == Language::Auto) {
        language = Environment::languageFromSourceFile(absoluteSourcePath);
    }

    if (language == Language::Auto) {
        failure.errorMessage = QStringLiteral("Unable to infer source language. Set CompileConfig::language explicitly.");
        return failure;
    }

    const QString compiler = resolvedCompiler(language, config.compilerPath);
    if (compiler.isEmpty()) {
        failure.errorMessage = QStringLiteral("No external compiler found for language %1.").arg(toString(language));
        return failure;
    }

    const CompilerFamily compilerFamily = Environment::detectCompilerFamily(compiler);
    const QString standard = languageStandardFor(language, config.languageStandard);

    QStringList commonArgs;
    if (compilerFamily == CompilerFamily::Msvc) {
        if (!standard.isEmpty()) {
            commonArgs << QStringLiteral("/std:%1").arg(standard);
        }
        if (config.debugInfo) {
            commonArgs << QStringLiteral("/Zi") << QStringLiteral("/FS");
        }
        switch (config.optimizationLevel) {
        case OptimizationLevel::None:
        case OptimizationLevel::Debug:
            commonArgs << QStringLiteral("/Od");
            break;
        case OptimizationLevel::Speed:
            commonArgs << QStringLiteral("/O2");
            break;
        case OptimizationLevel::Size:
            commonArgs << QStringLiteral("/O1");
            break;
        }
        commonArgs << normalizePathArgs(config.includePaths, workingDirectory, QStringLiteral("/I"));
        commonArgs << normalizeDefinitions(config.definitions, QStringLiteral("/D"));
        commonArgs << config.compilerFlags;
    } else {
        // GNU / TCC
        if (!standard.isEmpty()) {
            commonArgs << QStringLiteral("-std=%1").arg(standard);
        }
        if (config.positionIndependentCode || config.outputType == OutputType::SharedLibrary) {
            commonArgs << QStringLiteral("-fPIC");
        }
        if (config.debugInfo) {
            commonArgs << QStringLiteral("-g");
        }
        if (compilerFamily == CompilerFamily::Tcc) {
            switch (config.optimizationLevel) {
            case OptimizationLevel::None:
            case OptimizationLevel::Debug:
                commonArgs << QStringLiteral("-O0");
                break;
            case OptimizationLevel::Speed:
                commonArgs << QStringLiteral("-O2");
                break;
            case OptimizationLevel::Size:
                commonArgs << QStringLiteral("-O1");
                break;
            }
        } else {
            switch (config.optimizationLevel) {
            case OptimizationLevel::None:
                commonArgs << QStringLiteral("-O0");
                break;
            case OptimizationLevel::Debug:
                commonArgs << QStringLiteral("-Og");
                break;
            case OptimizationLevel::Speed:
                commonArgs << QStringLiteral("-O2");
                break;
            case OptimizationLevel::Size:
                commonArgs << QStringLiteral("-Os");
                break;
            }
        }
        commonArgs << normalizePathArgs(config.includePaths, workingDirectory, QStringLiteral("-I"));
        commonArgs << normalizeDefinitions(config.definitions, QStringLiteral("-D"));
        commonArgs << config.compilerFlags;
    }

    if (config.outputType == OutputType::Memory) {
        failure.errorMessage = QStringLiteral("Memory output is reserved for the internal TCC backend.");
        return failure;
    }

    if (config.outputType == OutputType::StaticLibrary) {
        QTemporaryDir objectDir;
        if (!objectDir.isValid()) {
            failure.errorMessage = QStringLiteral("Unable to create a temporary directory for static library compilation.");
            return failure;
        }

        const QString objectExt = (compilerFamily == CompilerFamily::Msvc) ? QStringLiteral(".obj") : QStringLiteral(".o");
        const QString objectName = QFileInfo(absoluteSourcePath).completeBaseName() + objectExt;
        const QString objectPath = objectDir.filePath(objectName);

        QStringList objectArgs = commonArgs;
        if (compilerFamily == CompilerFamily::Msvc) {
            objectArgs << QStringLiteral("/c") << absoluteSourcePath << QStringLiteral("/Fo%1").arg(objectPath);
        } else {
            objectArgs << QStringLiteral("-c") << absoluteSourcePath << QStringLiteral("-o") << objectPath;
        }

        CompilationResult objectResult = runProcess(compiler, objectArgs, workingDirectory, config.captureOutput);
        objectResult.outputPath = objectPath;
        if (!objectResult.success) {
            objectResult.outputPath = absoluteOutputPath;
            return objectResult;
        }

        const QString archiver = resolvedArchiver(config.archiverPath);
        if (archiver.isEmpty()) {
            CompilationResult archiveFailure = objectResult;
            archiveFailure.success = false;
            archiveFailure.errorMessage = QStringLiteral("No archiver found for static library output.");
            archiveFailure.outputPath = absoluteOutputPath;
            return archiveFailure;
        }

        enum class ArchiverFamily {
            Gnu,
            Msvc
        };

        const auto detectArchiverFamily = [](const QString &path) {
            if (path.isEmpty()) return ArchiverFamily::Gnu;
            const QString filename = QFileInfo(path).fileName().toLower();
            if (filename.startsWith(QStringLiteral("lib")) || filename == QStringLiteral("lib.exe") ||
                filename.startsWith(QStringLiteral("llvm-lib")) || filename == QStringLiteral("llvm-lib.exe")) {
                return ArchiverFamily::Msvc;
            }
            return ArchiverFamily::Gnu;
        };

        QStringList archiveArgs;
        ArchiverFamily archiverFamily = detectArchiverFamily(archiver);
        if (archiverFamily == ArchiverFamily::Msvc) {
            archiveArgs << QStringLiteral("/NOLOGO") << QStringLiteral("/OUT:%1").arg(absoluteOutputPath) << objectPath;
        } else {
            archiveArgs << QStringLiteral("rcs") << absoluteOutputPath << objectPath;
        }

        CompilationResult archiveResult = runProcess(archiver, archiveArgs, workingDirectory, config.captureOutput);
        archiveResult.outputPath = absoluteOutputPath;
        QStringList combinedLog = objectResult.commandLog;
        for (const QString &command : archiveResult.commandLog) {
            combinedLog << command;
        }
        archiveResult.commandLog = combinedLog;
        if (config.captureOutput) {
            if (!objectResult.stdOut.isEmpty()) {
                archiveResult.stdOut = objectResult.stdOut + QLatin1Char('\n') + archiveResult.stdOut;
            }
            if (!objectResult.stdErr.isEmpty()) {
                archiveResult.stdErr = objectResult.stdErr + QLatin1Char('\n') + archiveResult.stdErr;
            }
        }
        return archiveResult;
    }

    if (config.outputType == OutputType::ObjectFile) {
        QStringList objArgs = commonArgs;
        if (compilerFamily == CompilerFamily::Msvc) {
            objArgs << QStringLiteral("/c") << absoluteSourcePath << QStringLiteral("/Fo%1").arg(absoluteOutputPath);
        } else {
            objArgs << QStringLiteral("-c") << absoluteSourcePath << QStringLiteral("-o") << absoluteOutputPath;
        }
        CompilationResult objResult = runProcess(compiler, objArgs, workingDirectory, config.captureOutput);
        objResult.outputPath = absoluteOutputPath;
        return objResult;
    }

    // Executable or SharedLibrary
    QStringList args = commonArgs;
    if (compilerFamily == CompilerFamily::Msvc) {
        if (config.outputType == OutputType::SharedLibrary) {
            args << QStringLiteral("/LD");
        }
        args << absoluteSourcePath;
        args << QStringLiteral("/Fe%1").arg(absoluteOutputPath);
        if (!config.libraryPaths.isEmpty() || !config.libraries.isEmpty()) {
            args << QStringLiteral("/link");
            for (const QString &path : config.libraryPaths) {
                args << QStringLiteral("/LIBPATH:%1").arg(ensureAbsolutePath(path, workingDirectory));
            }
            args << normalizeLibrariesMsvc(config.libraries);
        }
    } else {
        // GNU / TCC
#ifdef Q_OS_MACOS
        if (config.outputType == OutputType::SharedLibrary) {
            args << QStringLiteral("-dynamiclib");
        }
#else
        if (config.outputType == OutputType::SharedLibrary) {
            args << QStringLiteral("-shared");
        }
#endif
        args << absoluteSourcePath << QStringLiteral("-o") << absoluteOutputPath;
        args << normalizePathArgs(config.libraryPaths, workingDirectory, QStringLiteral("-L"));
        args << normalizeLibrariesGnu(config.libraries);
    }

    CompilationResult result = runProcess(compiler, args, workingDirectory, config.captureOutput);
    result.outputPath = absoluteOutputPath;
    return result;
}

CompilationResult ExternalCompilerBackend::link(const QStringList &objectPaths,
                                                const QString &outputPath,
                                                const CompileConfig &config) const
{
    CompilationResult failure;

    if (objectPaths.isEmpty()) {
        failure.errorMessage = QStringLiteral("Object paths list is empty.");
        return failure;
    }

    if (outputPath.isEmpty()) {
        failure.errorMessage = QStringLiteral("Output path is empty.");
        return failure;
    }

    const QString workingDirectory = config.workingDirectory.isEmpty()
            ? QFileInfo(objectPaths.first()).absolutePath()
            : config.workingDirectory;
    const QString absoluteOutputPath = ensureAbsolutePath(outputPath, workingDirectory);
    failure.outputPath = absoluteOutputPath;

    QStringList absoluteObjectPaths;
    for (const QString &path : objectPaths) {
        if (path.isEmpty()) {
            continue;
        }
        const QString absPath = ensureAbsolutePath(path, workingDirectory);
        if (!QFileInfo::exists(absPath)) {
            failure.errorMessage = QStringLiteral("Object file does not exist: %1").arg(absPath);
            return failure;
        }
        absoluteObjectPaths << absPath;
    }

    if (absoluteObjectPaths.isEmpty()) {
        failure.errorMessage = QStringLiteral("No valid object files to link.");
        return failure;
    }

    if (config.outputType == OutputType::Memory) {
        failure.errorMessage = QStringLiteral("Memory output is reserved for the internal TCC backend.");
        return failure;
    }

    if (config.outputType == OutputType::ObjectFile) {
        failure.errorMessage = QStringLiteral("Cannot link to an OutputType::ObjectFile.");
        return failure;
    }

    if (config.outputType == OutputType::StaticLibrary) {
        const QString archiver = resolvedArchiver(config.archiverPath);
        if (archiver.isEmpty()) {
            failure.errorMessage = QStringLiteral("No archiver found for static library linking.");
            return failure;
        }

        enum class ArchiverFamily {
            Gnu,
            Msvc
        };

        const auto detectArchiverFamily = [](const QString &path) {
            if (path.isEmpty()) return ArchiverFamily::Gnu;
            const QString filename = QFileInfo(path).fileName().toLower();
            if (filename.startsWith(QStringLiteral("lib")) || filename == QStringLiteral("lib.exe") ||
                filename.startsWith(QStringLiteral("llvm-lib")) || filename == QStringLiteral("llvm-lib.exe")) {
                return ArchiverFamily::Msvc;
            }
            return ArchiverFamily::Gnu;
        };

        QStringList archiveArgs;
        ArchiverFamily archiverFamily = detectArchiverFamily(archiver);
        if (archiverFamily == ArchiverFamily::Msvc) {
            archiveArgs << QStringLiteral("/NOLOGO") << QStringLiteral("/OUT:%1").arg(absoluteOutputPath) << absoluteObjectPaths;
        } else {
            archiveArgs << QStringLiteral("rcs") << absoluteOutputPath << absoluteObjectPaths;
        }

        CompilationResult archiveResult = runProcess(archiver, archiveArgs, workingDirectory, config.captureOutput);
        archiveResult.outputPath = absoluteOutputPath;
        return archiveResult;
    }

    // Executable or SharedLibrary
    Language language = config.language;
    if (language == Language::Auto) {
        language = Language::Cpp;
    }

    const QString compiler = resolvedCompiler(language, config.compilerPath);
    if (compiler.isEmpty()) {
        failure.errorMessage = QStringLiteral("No external compiler found for linking.");
        return failure;
    }

    const CompilerFamily compilerFamily = Environment::detectCompilerFamily(compiler);
    const QString standard = languageStandardFor(language, config.languageStandard);

    QStringList args;
    if (compilerFamily == CompilerFamily::Msvc) {
        if (!standard.isEmpty()) {
            args << QStringLiteral("/std:%1").arg(standard);
        }
        if (config.outputType == OutputType::SharedLibrary) {
            args << QStringLiteral("/LD");
        }
        args << absoluteObjectPaths;
        args << QStringLiteral("/Fe%1").arg(absoluteOutputPath);
        args << config.compilerFlags;
        if (!config.libraryPaths.isEmpty() || !config.libraries.isEmpty() || config.debugInfo) {
            args << QStringLiteral("/link");
            if (config.debugInfo) {
                args << QStringLiteral("/DEBUG");
            }
            for (const QString &path : config.libraryPaths) {
                args << QStringLiteral("/LIBPATH:%1").arg(ensureAbsolutePath(path, workingDirectory));
            }
            args << normalizeLibrariesMsvc(config.libraries);
        }
    } else {
        // GNU / TCC
        if (!standard.isEmpty()) {
            args << QStringLiteral("-std=%1").arg(standard);
        }
        if (config.positionIndependentCode || config.outputType == OutputType::SharedLibrary) {
            args << QStringLiteral("-fPIC");
        }
        if (config.debugInfo) {
            args << QStringLiteral("-g");
        }
        if (config.outputType == OutputType::SharedLibrary) {
#ifdef Q_OS_MACOS
            args << QStringLiteral("-dynamiclib");
#else
            args << QStringLiteral("-shared");
#endif
        }
        args << absoluteObjectPaths;
        args << QStringLiteral("-o") << absoluteOutputPath;
        args << config.compilerFlags;
        args << normalizePathArgs(config.libraryPaths, workingDirectory, QStringLiteral("-L"));
        args << normalizeLibrariesGnu(config.libraries);
    }

    CompilationResult result = runProcess(compiler, args, workingDirectory, config.captureOutput);
    result.outputPath = absoluteOutputPath;
    return result;
}

}

