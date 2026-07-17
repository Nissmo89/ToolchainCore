#include "ccc/engine.h"

#include "ccc/backends/externalcompilerbackend.h"
#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
#include "ccc/backends/internaltccbackend.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>

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

void writeCompilationDatabaseEntry(const QString &dbPath,
                                    const QString &sourcePath,
                                    const QString &outputPath,
                                    const QString &directory,
                                    const QString &program,
                                    const QStringList &arguments)
{
    if (dbPath.isEmpty() || sourcePath.isEmpty()) {
        return;
    }

    QString jsonFilePath = dbPath;
    QFileInfo dbInfo(dbPath);
    if (dbInfo.isDir()) {
        jsonFilePath = QDir(dbPath).filePath(QStringLiteral("compile_commands.json"));
    } else if (dbInfo.exists() && dbInfo.isFile()) {
        // use it directly
    } else {
        if (!dbPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
            QDir().mkpath(dbPath);
            jsonFilePath = QDir(dbPath).filePath(QStringLiteral("compile_commands.json"));
        } else {
            QDir().mkpath(dbInfo.absolutePath());
        }
    }

    QJsonArray array;
    QFile readFile(jsonFilePath);
    if (readFile.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray()) {
            array = doc.array();
        }
        readFile.close();
    }

    const QString absoluteSource = QFileInfo(sourcePath).absoluteFilePath();
    const QString absoluteOutput = outputPath.isEmpty() ? QString() : QFileInfo(outputPath).absoluteFilePath();

    bool updated = false;
    for (int i = 0; i < array.size(); ++i) {
        QJsonObject obj = array[i].toObject();
        if (QDir::cleanPath(obj.value(QStringLiteral("file")).toString()) == QDir::cleanPath(absoluteSource)) {
            QJsonObject newObj;
            newObj[QStringLiteral("directory")] = directory;
            newObj[QStringLiteral("file")] = absoluteSource;
            if (!absoluteOutput.isEmpty()) {
                newObj[QStringLiteral("output")] = absoluteOutput;
            }

            QStringList fullCmdList;
            fullCmdList << program;
            fullCmdList << arguments;
            newObj[QStringLiteral("command")] = fullCmdList.join(QLatin1Char(' '));

            QJsonArray argsArray;
            for (const QString &arg : fullCmdList) {
                argsArray.append(arg);
            }
            newObj[QStringLiteral("arguments")] = argsArray;

            array[i] = newObj;
            updated = true;
            break;
        }
    }

    if (!updated) {
        QJsonObject newObj;
        newObj[QStringLiteral("directory")] = directory;
        newObj[QStringLiteral("file")] = absoluteSource;
        if (!absoluteOutput.isEmpty()) {
            newObj[QStringLiteral("output")] = absoluteOutput;
        }

        QStringList fullCmdList;
        fullCmdList << program;
        fullCmdList << arguments;
        newObj[QStringLiteral("command")] = fullCmdList.join(QLatin1Char(' '));

        QJsonArray argsArray;
        for (const QString &arg : fullCmdList) {
            argsArray.append(arg);
        }
        newObj[QStringLiteral("arguments")] = argsArray;

        array.append(newObj);
    }

    QSaveFile saveFile(jsonFilePath);
    if (saveFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(array);
        saveFile.write(doc.toJson(QJsonDocument::Indented));
        saveFile.commit();
    }
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

    if (config.backendKind == BackendKind::InternalTcc) {
        if (config.outputType != OutputType::Memory) {
            failure.errorMessage = QStringLiteral("Internal TCC backend currently only supports OutputType::Memory.");
            return failure;
        }
    }

    CompileConfig normalized = config;
    const QString workingDirectory = resolveWorkingDirectory(sourcePath, config.workingDirectory);
    const QString absoluteSourcePath = normalizedSourcePath(sourcePath, config.workingDirectory.isEmpty() ? QDir::currentPath() : config.workingDirectory);
    normalized.workingDirectory = workingDirectory;

    if (normalized.language == Language::Auto) {
        normalized.language = Environment::languageFromSourceFile(absoluteSourcePath);
    }

    if (normalized.language == Language::Auto) {
        failure.errorMessage = QStringLiteral("Unable to infer source language. Set CompileConfig::language explicitly.");
        return failure;
    }

    const bool memoryOutput = normalized.outputType == OutputType::Memory;
    const bool useInternalTcc = [memoryOutput, backendKind = normalized.backendKind]() {
        switch (backendKind) {
        case BackendKind::InternalTcc:
            return true;
        case BackendKind::Auto:
            return memoryOutput;
        case BackendKind::ExternalCompiler:
            return false;
        }
        return false;
    }();

    if (memoryOutput && !useInternalTcc) {
        failure.errorMessage = QStringLiteral("OutputType::Memory requires the optional internal TCC backend.");
        return failure;
    }

    if (useInternalTcc) {
#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
        if (normalized.language != Language::C) {
            failure.errorMessage = QStringLiteral("Internal TCC backend only supports C sources.");
            return failure;
        }

        InternalTccBackend backend;
        CompilationResult result = backend.compile(absoluteSourcePath, QString(), normalized);
        if (result.success && !config.compilationDatabasePath.isEmpty()) {
            writeCompilationDatabaseEntry(config.compilationDatabasePath,
                                           absoluteSourcePath,
                                           QString(),
                                           workingDirectory,
                                           result.program,
                                           result.arguments);
        }
        return result;
#else
        failure.errorMessage = QStringLiteral("Internal TCC backend is not enabled in this build.");
        return failure;
#endif
    }

    if (memoryOutput) {
        failure.errorMessage = QStringLiteral("OutputType::Memory requires the optional internal TCC backend.");
        return failure;
    }

    if (outputPath.isEmpty()) {
        failure.errorMessage = QStringLiteral("Output path is empty.");
        return failure;
    }

    const QString absoluteOutputPath = resolveAbsolutePath(outputPath, config.workingDirectory.isEmpty() ? QDir::currentPath() : config.workingDirectory);
    ExternalCompilerBackend backend;
    CompilationResult result = backend.compile(absoluteSourcePath, absoluteOutputPath, normalized);
    if (result.outputPath.isEmpty()) {
        result.outputPath = absoluteOutputPath;
    }
    if (result.success && !config.compilationDatabasePath.isEmpty()) {
        writeCompilationDatabaseEntry(config.compilationDatabasePath,
                                       absoluteSourcePath,
                                       result.outputPath,
                                       workingDirectory,
                                       result.program,
                                       result.arguments);
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

CompilationResult Engine::link(const QStringList &objectPaths,
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

    if (config.backendKind == BackendKind::InternalTcc) {
        failure.errorMessage = QStringLiteral("Internal TCC backend does not support linking object files.");
        return failure;
    }

    QStringList absoluteObjectPaths;
    const QString baseDir = config.workingDirectory.isEmpty() ? QDir::currentPath() : config.workingDirectory;
    for (const QString &path : objectPaths) {
        if (!path.isEmpty()) {
            absoluteObjectPaths << resolveAbsolutePath(path, baseDir);
        }
    }

    CompileConfig normalized = config;
    const QString workingDirectory = config.workingDirectory.isEmpty()
            ? (absoluteObjectPaths.isEmpty() ? QDir::currentPath() : QFileInfo(absoluteObjectPaths.first()).absolutePath())
            : QDir(config.workingDirectory).absolutePath();
    normalized.workingDirectory = workingDirectory;

    const QString absoluteOutputPath = resolveAbsolutePath(outputPath, config.workingDirectory.isEmpty() ? QDir::currentPath() : config.workingDirectory);

    ExternalCompilerBackend backend;
    CompilationResult result = backend.link(absoluteObjectPaths, absoluteOutputPath, normalized);
    if (result.outputPath.isEmpty()) {
        result.outputPath = absoluteOutputPath;
    }
    return result;
}

void* Engine::getSymbolAddress(const CompilationResult &result, const QString &symbolName)
{
#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
    return InternalTccBackend::getSymbolAddress(result.jitContext, symbolName);
#else
    (void)result;
    (void)symbolName;
    return nullptr;
#endif
}

}

