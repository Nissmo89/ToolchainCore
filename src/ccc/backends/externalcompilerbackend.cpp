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

QStringList ExternalCompilerBackend::normalizeDefinitions(const QStringList &definitions)
{
    QStringList args;
    for (const QString &definition : definitions) {
        if (definition.isEmpty()) {
            continue;
        }

        if (definition.startsWith(QLatin1String("-D"))) {
            args << definition;
        } else {
            args << (QStringLiteral("-D") + definition);
        }
    }
    return args;
}

QStringList ExternalCompilerBackend::normalizeLibraries(const QStringList &libraries)
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

    const QString standard = languageStandardFor(language, config.languageStandard);

    QStringList commonArgs;
    if (!standard.isEmpty()) {
        commonArgs << QStringLiteral("-std=%1").arg(standard);
    }

    if (config.positionIndependentCode || config.outputType == OutputType::SharedLibrary) {
        commonArgs << QStringLiteral("-fPIC");
    }

    commonArgs << normalizePathArgs(config.includePaths, workingDirectory, QStringLiteral("-I"));
    commonArgs << normalizeDefinitions(config.definitions);
    commonArgs << config.compilerFlags;

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

        const QString objectName = QFileInfo(absoluteSourcePath).completeBaseName() + QStringLiteral(".o");
        const QString objectPath = objectDir.filePath(objectName);

        QStringList objectArgs = commonArgs;
        objectArgs << QStringLiteral("-c") << absoluteSourcePath << QStringLiteral("-o") << objectPath;

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

        QStringList archiveArgs;
#ifdef Q_OS_WIN
        archiveArgs << QStringLiteral("/NOLOGO") << QStringLiteral("/OUT:%1").arg(absoluteOutputPath) << objectPath;
#else
        archiveArgs << QStringLiteral("rcs") << absoluteOutputPath << objectPath;
#endif

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

    QStringList args = commonArgs;

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
    args << normalizeLibraries(config.libraries);

    CompilationResult result = runProcess(compiler, args, workingDirectory, config.captureOutput);
    result.outputPath = absoluteOutputPath;
    return result;
}

}
