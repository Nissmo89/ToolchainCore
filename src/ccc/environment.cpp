#include "ccc/environment.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QProcessEnvironment>

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

QStringList Environment::systemIncludePaths(Language language, CompilerFamily family, const QString &compilerPath)
{
    QStringList paths;

    if (family == CompilerFamily::Tcc) {
        return resolveTccIncludePaths();
    }

    if (family == CompilerFamily::Msvc) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString envInclude = env.value(QStringLiteral("INCLUDE"));
        if (!envInclude.isEmpty()) {
            const QStringList list = envInclude.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString &p : list) {
                paths << QDir::cleanPath(p.trimmed());
            }
            return paths;
        }

#ifdef Q_OS_WIN
        QString vsInstallPath;
        QString programFilesX86 = env.value(QStringLiteral("ProgramFiles(x86)"), QStringLiteral("C:/Program Files (x86)"));
        QString programFiles = env.value(QStringLiteral("ProgramFiles"), QStringLiteral("C:/Program Files"));
        QString vswherePath = QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/Installer/vswhere.exe"));
        
        if (QFile::exists(vswherePath)) {
            QProcess proc;
            proc.start(vswherePath, {QStringLiteral("-latest"), QStringLiteral("-property"), QStringLiteral("installationPath")});
            if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
                vsInstallPath = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            }
        }

        if (vsInstallPath.isEmpty()) {
            QStringList candidateRoots = {
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/Community")),
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/Professional")),
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/Enterprise")),
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/BuildTools")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/Community")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/Professional")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/Enterprise")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/BuildTools"))
            };
            for (const QString &cand : candidateRoots) {
                if (QDir(cand).exists()) {
                    vsInstallPath = cand;
                    break;
                }
            }
        }

        if (!vsInstallPath.isEmpty()) {
            QDir msvcBase(QDir(vsInstallPath).filePath(QStringLiteral("VC/Tools/MSVC")));
            if (msvcBase.exists()) {
                const QStringList versions = msvcBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
                if (!versions.isEmpty()) {
                    QString latestMsvcInclude = msvcBase.filePath(QDir(versions.first()).filePath(QStringLiteral("include")));
                    if (QDir(latestMsvcInclude).exists()) {
                        paths << QDir::cleanPath(latestMsvcInclude);
                    }
                }
            }
        }

        QDir sdkBase(QStringLiteral("C:/Program Files (x86)/Windows Kits/10/Include"));
        if (sdkBase.exists()) {
            const QStringList versions = sdkBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
            if (!versions.isEmpty()) {
                QString sdkVer = versions.first();
                QStringList subfolders = {QStringLiteral("ucrt"), QStringLiteral("um"), QStringLiteral("shared"), QStringLiteral("winrt")};
                for (const QString &sub : subfolders) {
                    QString path = sdkBase.filePath(QDir(sdkVer).filePath(sub));
                    if (QDir(path).exists()) {
                        paths << QDir::cleanPath(path);
                    }
                }
            }
        }
#endif
        return paths;
    }

    QString compiler = compilerPath;
    if (compiler.isEmpty()) {
        compiler = findExternalCompiler(language);
    }
    if (compiler.isEmpty()) {
        return paths;
    }

    QProcess proc;
    QStringList args;
    args << QStringLiteral("-E") << QStringLiteral("-v");
    if (language == Language::Cpp) {
        args << QStringLiteral("-xc++");
    } else {
        args << QStringLiteral("-xc");
    }
#ifdef Q_OS_WIN
    args << QStringLiteral("NUL");
#else
    args << QStringLiteral("/dev/null");
#endif

    proc.start(compiler, args);
    if (proc.waitForFinished(3000)) {
        const QString output = QString::fromUtf8(proc.readAllStandardError());
        const QStringList lines = output.split(QLatin1Char('\n'));
        bool inSearchList = false;
        for (const QString &line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("#include <...> search starts here:"))) {
                inSearchList = true;
                continue;
            }
            if (trimmed.startsWith(QStringLiteral("End of search list."))) {
                inSearchList = false;
                break;
            }
            if (inSearchList && !trimmed.isEmpty()) {
                paths << QDir::cleanPath(trimmed);
            }
        }
    }

    return paths;
}

QStringList Environment::systemLibraryPaths(CompilerFamily family, const QString &compilerPath)
{
    QStringList paths;

    if (family == CompilerFamily::Tcc) {
        return resolveTccLibraryPaths();
    }

    if (family == CompilerFamily::Msvc) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString envLib = env.value(QStringLiteral("LIB"));
        if (!envLib.isEmpty()) {
            const QStringList list = envLib.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString &p : list) {
                paths << QDir::cleanPath(p.trimmed());
            }
            return paths;
        }

#ifdef Q_OS_WIN
        QString vsInstallPath;
        QString programFilesX86 = env.value(QStringLiteral("ProgramFiles(x86)"), QStringLiteral("C:/Program Files (x86)"));
        QString programFiles = env.value(QStringLiteral("ProgramFiles"), QStringLiteral("C:/Program Files"));
        QString vswherePath = QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/Installer/vswhere.exe"));
        
        if (QFile::exists(vswherePath)) {
            QProcess proc;
            proc.start(vswherePath, {QStringLiteral("-latest"), QStringLiteral("-property"), QStringLiteral("installationPath")});
            if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
                vsInstallPath = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            }
        }

        if (vsInstallPath.isEmpty()) {
            QStringList candidateRoots = {
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/Community")),
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/Professional")),
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/Enterprise")),
                QDir(programFiles).filePath(QStringLiteral("Microsoft Visual Studio/2022/BuildTools")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/Community")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/Professional")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/Enterprise")),
                QDir(programFilesX86).filePath(QStringLiteral("Microsoft Visual Studio/2019/BuildTools"))
            };
            for (const QString &cand : candidateRoots) {
                if (QDir(cand).exists()) {
                    vsInstallPath = cand;
                    break;
                }
            }
        }

        if (!vsInstallPath.isEmpty()) {
            QDir msvcBase(QDir(vsInstallPath).filePath(QStringLiteral("VC/Tools/MSVC")));
            if (msvcBase.exists()) {
                const QStringList versions = msvcBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
                if (!versions.isEmpty()) {
                    QString latestMsvcLib = msvcBase.filePath(QDir(versions.first()).filePath(QStringLiteral("lib/x64")));
                    if (QDir(latestMsvcLib).exists()) {
                        paths << QDir::cleanPath(latestMsvcLib);
                    }
                }
            }
        }

        QDir sdkBase(QStringLiteral("C:/Program Files (x86)/Windows Kits/10/Lib"));
        if (sdkBase.exists()) {
            const QStringList versions = sdkBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
            if (!versions.isEmpty()) {
                QString sdkVer = versions.first();
                QStringList subfolders = {QStringLiteral("ucrt/x64"), QStringLiteral("um/x64")};
                for (const QString &sub : subfolders) {
                    QString path = sdkBase.filePath(QDir(sdkVer).filePath(sub));
                    if (QDir(path).exists()) {
                        paths << QDir::cleanPath(path);
                    }
                }
            }
        }
#endif
        return paths;
    }

    QString compiler = compilerPath;
    if (compiler.isEmpty()) {
        compiler = findExternalCompiler();
    }
    if (compiler.isEmpty()) {
        return paths;
    }

    QProcess proc;
    proc.start(compiler, {QStringLiteral("-print-search-dirs")});
    if (proc.waitForFinished(3000)) {
        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const QStringList lines = output.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("libraries: ="))) {
                QString listStr = trimmed.mid(12);
#ifdef Q_OS_WIN
                QStringList rawPaths = listStr.split(QLatin1Char(';'), Qt::SkipEmptyParts);
#else
                QStringList rawPaths = listStr.split(QLatin1Char(':'), Qt::SkipEmptyParts);
#endif
                for (const QString &raw : rawPaths) {
                    QString cleaned = QDir::cleanPath(raw.trimmed());
                    if (QDir(cleaned).exists()) {
                        paths << cleaned;
                    }
                }
                break;
            }
        }
    }

    return paths;
}

}
