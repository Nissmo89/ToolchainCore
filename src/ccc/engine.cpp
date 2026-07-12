#include "ccc/engine.h"

#include "ccc/backends/externalcompilerbackend.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace ccc {
namespace {

QString normalizedSourcePath(const QString &sourcePath, const QString &workingDirectory)
{
    if (sourcePath.isEmpty()) {
        return {};
    }

    const QFileInfo info(sourcePath);
    if (info.isAbsolute()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    return QDir(workingDirectory).absoluteFilePath(sourcePath);
}

}

Engine::Engine(QObject *parent)
    : QObject(parent)
{
}

QString Engine::resolveWorkingDirectory(const QString &sourcePath, const QString &configuredWorkingDirectory)
{
    if (!configuredWorkingDirectory.isEmpty()) {
        return QDir(configuredWorkingDirectory).absolutePath();
    }

    const QFileInfo sourceInfo(sourcePath);
    if (sourceInfo.exists()) {
        return sourceInfo.absolutePath();
    }

    return QDir::currentPath();
}

QString Engine::resolveAbsolutePath(const QString &path, const QString &workingDirectory)
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

CompilationResult Engine::compile(const QString &sourcePath,
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

    if (config.outputType == OutputType::Memory) {
        failure.errorMessage = QStringLiteral("Memory output is not enabled in the current ToolchainCore build.");
        return failure;
    }

    if (config.backendKind == BackendKind::InternalTcc) {
        failure.errorMessage = QStringLiteral("Internal TCC backend is not enabled yet.");
        return failure;
    }

    CompileConfig normalized = config;
    const QString workingDirectory = resolveWorkingDirectory(sourcePath, config.workingDirectory);
    const QString absoluteSourcePath = normalizedSourcePath(sourcePath, workingDirectory);
    const QString absoluteOutputPath = resolveAbsolutePath(outputPath, workingDirectory);
    normalized.workingDirectory = workingDirectory;

    if (normalized.language == Language::Auto) {
        normalized.language = Environment::languageFromSourceFile(absoluteSourcePath);
    }

    if (normalized.language == Language::Auto) {
        failure.errorMessage = QStringLiteral("Unable to infer source language. Set CompileConfig::language explicitly.");
        return failure;
    }

    ExternalCompilerBackend backend;
    CompilationResult result = backend.compile(absoluteSourcePath, absoluteOutputPath, normalized);
    if (result.outputPath.isEmpty()) {
        result.outputPath = absoluteOutputPath;
    }
    return result;
}

RunResult Engine::compileAndRun(const QString &sourcePath,
                                const QString &outputPath,
                                const CompileConfig &config,
                                const QStringList &runArguments) const
{
    RunResult result;
    if (config.outputType != OutputType::Executable) {
        result.compilation.errorMessage = QStringLiteral("compileAndRun requires OutputType::Executable.");
        result.execution.errorMessage = result.compilation.errorMessage;
        return result;
    }

    result.compilation = compile(sourcePath, outputPath, config);
    if (!result.compilation.success) {
        result.execution.success = false;
        result.execution.errorMessage = result.compilation.errorMessage;
        return result;
    }

    const QString workingDirectory = config.workingDirectory.isEmpty()
            ? QFileInfo(result.compilation.outputPath).absolutePath()
            : QDir(config.workingDirectory).absolutePath();

    result.execution = runExecutable(result.compilation.outputPath, runArguments, workingDirectory);
    return result;
}

ExecutionResult Engine::runExecutable(const QString &executablePath,
                                      const QStringList &arguments,
                                      const QString &workingDirectory) const
{
    ExecutionResult result;
    if (executablePath.isEmpty()) {
        result.errorMessage = QStringLiteral("Executable path is empty.");
        return result;
    }

    const QString runDirectory = workingDirectory.isEmpty()
            ? QDir::currentPath()
            : QDir(workingDirectory).absolutePath();
    const QString absoluteExecutablePath = resolveAbsolutePath(executablePath, runDirectory);
    result.program = absoluteExecutablePath;
    result.arguments = arguments;
    if (!QFileInfo::exists(absoluteExecutablePath)) {
        result.errorMessage = QStringLiteral("Executable does not exist: %1").arg(absoluteExecutablePath);
        return result;
    }

    QProcess process;
    process.setWorkingDirectory(runDirectory.isEmpty()
                                    ? QFileInfo(absoluteExecutablePath).absolutePath()
                                    : runDirectory);

    process.start(absoluteExecutablePath, arguments);
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
                ? QStringLiteral("Executable failed to finish.")
                : process.errorString();
        return result;
    }

    result.exitCode = process.exitCode();
    result.exitStatus = process.exitStatus();
    result.processError = process.error();
    result.success = (result.exitStatus == QProcess::NormalExit && result.exitCode == 0);
    result.stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.stdErr = QString::fromLocal8Bit(process.readAllStandardError());

    if (!result.success && result.errorMessage.isEmpty()) {
        if (result.exitStatus != QProcess::NormalExit) {
            result.errorMessage = QStringLiteral("Executable terminated abnormally.");
        } else {
            result.errorMessage = QStringLiteral("Executable exited with code %1.").arg(result.exitCode);
        }
    }

    return result;
}

}
