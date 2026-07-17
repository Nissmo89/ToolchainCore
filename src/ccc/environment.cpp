#include "ccc/environment.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace ccc {
namespace {

void appendUnique(QStringList &list, const QString &value)
{
    if (!value.isEmpty() && !list.contains(value)) {
        list.append(QDir::cleanPath(value));
    }
}

QStringList normalizePaths(const QStringList &paths)
{
    QStringList normalized;
    for (const QString &path : paths) {
        appendUnique(normalized, path);
    }
    return normalized;
}

QStringList expandFromRoots(const QStringList &roots, const QStringList &subdirs)
{
    QStringList paths;
    for (const QString &root : roots) {
        if (root.isEmpty()) {
            continue;
        }

        const QFileInfo rootInfo(root);
        if (rootInfo.exists()) {
            appendUnique(paths, rootInfo.absoluteFilePath());
        }

        for (const QString &subdir : subdirs) {
            appendUnique(paths, QDir(root).filePath(subdir));
        }
    }
    return normalizePaths(paths);
}

QStringList envPathList(const QString &variableName)
{
    const QByteArray variableNameBytes = variableName.toLocal8Bit();
    const QString raw = qEnvironmentVariable(variableNameBytes.constData());
    if (raw.isEmpty()) {
        return {};
    }

    const QStringList parts = raw.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString &part : parts) {
        appendUnique(out, part);
    }
    return out;
}

bool isExecutableFile(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
}

QString resolveExecutable(const QString &candidate)
{
    if (candidate.isEmpty()) {
        return {};
    }

    if (candidate.contains(QDir::separator()) || candidate.contains(QLatin1Char('\\'))) {
        const QFileInfo info(candidate);
        if (isExecutableFile(info.absoluteFilePath())) {
            return info.absoluteFilePath();
        }
    }

    const QString found = QStandardPaths::findExecutable(candidate);
    return found;
}

QStringList compilerCandidatesForC()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("cl"), QStringLiteral("clang"), QStringLiteral("gcc"), QStringLiteral("cc")};
#else
    return {QStringLiteral("clang"), QStringLiteral("gcc"), QStringLiteral("cc")};
#endif
}

QStringList compilerCandidatesForCpp()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("cl"), QStringLiteral("clang++"), QStringLiteral("g++"), QStringLiteral("c++")};
#else
    return {QStringLiteral("clang++"), QStringLiteral("g++"), QStringLiteral("c++")};
#endif
}

}

CompilerFamily Environment::detectCompilerFamily(const QString &compilerPath)
{
    if (compilerPath.isEmpty()) {
        return CompilerFamily::Gnu;
    }

    const QString filename = QFileInfo(compilerPath).fileName().toLower();
    if ((filename.startsWith(QStringLiteral("cl")) && !filename.startsWith(QStringLiteral("clang"))) || filename == QStringLiteral("cl.exe")) {
        return CompilerFamily::Msvc;
    }
    if (filename.startsWith(QStringLiteral("tcc")) || filename == QStringLiteral("tcc.exe")) {
        return CompilerFamily::Tcc;
    }

    return CompilerFamily::Gnu;
}

QString Environment::executableSuffix()
{
#ifdef Q_OS_WIN
    return QStringLiteral(".exe");
#else
    return {};
#endif
}

QString Environment::executablePath(const QString &basePath)
{
    if (basePath.isEmpty()) {
        return {};
    }

    const QFileInfo info(basePath);
    QString path = info.absoluteFilePath();
    if (!executableSuffix().isEmpty() && !path.endsWith(executableSuffix(), Qt::CaseInsensitive)) {
        path += executableSuffix();
    }
    return QDir::cleanPath(path);
}

Language Environment::languageFromSourceFile(const QString &sourcePath)
{
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    if (suffix == QStringLiteral("c")) {
        return Language::C;
    }

    if (suffix == QStringLiteral("cc") || suffix == QStringLiteral("cpp") ||
        suffix == QStringLiteral("cxx") || suffix == QStringLiteral("c++") ||
        suffix == QStringLiteral("cp") || suffix == QStringLiteral("mm")) {
        return Language::Cpp;
    }

    return Language::Auto;
}

QStringList Environment::compilerCandidates(Language language)
{
    switch (language) {
    case Language::C:
        return compilerCandidatesForC();
    case Language::Cpp:
        return compilerCandidatesForCpp();
    case Language::Auto: {
        QStringList candidates = compilerCandidatesForC();
        for (const QString &candidate : compilerCandidatesForCpp()) {
            candidates << candidate;
        }
        return normalizePaths(candidates);
    }
    }

    return compilerCandidatesForC();
}

QString Environment::findExternalCompiler(Language language, const QString &preferred)
{
    if (!preferred.isEmpty()) {
        return resolveExecutable(preferred);
    }

    const QStringList candidates = compilerCandidates(language);
    for (const QString &candidate : candidates) {
        const QString resolved = resolveExecutable(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    return {};
}

QString Environment::findArchiver(const QString &preferred)
{
    if (!preferred.isEmpty()) {
        return resolveExecutable(preferred);
    }

    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << QStringLiteral("lib") << QStringLiteral("llvm-lib") << QStringLiteral("ar") << QStringLiteral("llvm-ar");
#else
    candidates << QStringLiteral("ar") << QStringLiteral("llvm-ar") << QStringLiteral("lib");
#endif

    for (const QString &candidate : candidates) {
        const QString resolved = resolveExecutable(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    return {};
}

QStringList Environment::resolveTccIncludePaths()
{
    QStringList includePaths;
    auto addPaths = [&](const QStringList &paths) {
        for (const QString &path : paths) {
            appendUnique(includePaths, path);
        }
    };

    addPaths(expandFromRoots(envPathList(QStringLiteral("TOOLCHAINCORE_TCC_ROOT")),
                             {QStringLiteral("include"), QStringLiteral("include/tcc"), QStringLiteral("include/libtcc")}));
    addPaths(expandFromRoots(envPathList(QStringLiteral("TCC_HOME")),
                             {QStringLiteral("include"), QStringLiteral("include/tcc"), QStringLiteral("include/libtcc")}));
    addPaths(expandFromRoots(envPathList(QStringLiteral("TCCROOT")),
                             {QStringLiteral("include"), QStringLiteral("include/tcc"), QStringLiteral("include/libtcc")}));
    addPaths(expandFromRoots(envPathList(QStringLiteral("TINYCC_HOME")),
                             {QStringLiteral("include"), QStringLiteral("include/tcc"), QStringLiteral("include/libtcc")}));

    const QString appDir = QCoreApplication::applicationDirPath();
    addPaths(expandFromRoots(
            {QDir(appDir).filePath(QStringLiteral("tcc")),
             QDir(appDir).filePath(QStringLiteral("../tcc")),
             QDir(appDir).filePath(QStringLiteral("../share/tcc"))},
            {QStringLiteral("include"), QStringLiteral("include/tcc"), QStringLiteral("include/libtcc")}));

#ifdef Q_OS_WIN
    includePaths << QStringLiteral("C:/TCC/include") << QStringLiteral("C:/Program Files/TinyCC/include");
#else
    includePaths << QStringLiteral("/usr/include") << QStringLiteral("/usr/local/include") << QStringLiteral("/opt/homebrew/include");
#endif

    return normalizePaths(includePaths);
}

QStringList Environment::resolveTccLibraryPaths()
{
    QStringList libraryPaths;
    auto addPaths = [&](const QStringList &paths) {
        for (const QString &path : paths) {
            appendUnique(libraryPaths, path);
        }
    };

    addPaths(expandFromRoots(envPathList(QStringLiteral("TOOLCHAINCORE_TCC_ROOT")),
                             {QStringLiteral("lib"), QStringLiteral("lib64"), QStringLiteral("lib/tcc")}));
    addPaths(expandFromRoots(envPathList(QStringLiteral("TCC_HOME")),
                             {QStringLiteral("lib"), QStringLiteral("lib64"), QStringLiteral("lib/tcc")}));
    addPaths(expandFromRoots(envPathList(QStringLiteral("TCCROOT")),
                             {QStringLiteral("lib"), QStringLiteral("lib64"), QStringLiteral("lib/tcc")}));
    addPaths(expandFromRoots(envPathList(QStringLiteral("TINYCC_HOME")),
                             {QStringLiteral("lib"), QStringLiteral("lib64"), QStringLiteral("lib/tcc")}));

    const QString appDir = QCoreApplication::applicationDirPath();
    addPaths(expandFromRoots(
            {QDir(appDir).filePath(QStringLiteral("tcc")),
             QDir(appDir).filePath(QStringLiteral("../tcc")),
             QDir(appDir).filePath(QStringLiteral("../share/tcc"))},
            {QStringLiteral("lib"), QStringLiteral("lib64"), QStringLiteral("lib/tcc")}));

#ifdef Q_OS_WIN
    libraryPaths << QStringLiteral("C:/TCC/lib") << QStringLiteral("C:/Program Files/TinyCC/lib");
#else
    libraryPaths << QStringLiteral("/usr/lib") << QStringLiteral("/usr/local/lib") << QStringLiteral("/usr/lib64")
                 << QStringLiteral("/usr/local/lib64") << QStringLiteral("/opt/homebrew/lib");
#endif

    return normalizePaths(libraryPaths);
}

}
