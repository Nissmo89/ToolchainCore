#pragma once

#include "ccc/ccc_global.h"
#include "ccc/types.h"

#include <QString>
#include <QStringList>

namespace ccc {

class CCC_EXPORT Environment final {
public:
    [[nodiscard]] static QString executableSuffix();
    [[nodiscard]] static QString executablePath(const QString &basePath);
    [[nodiscard]] static Language languageFromSourceFile(const QString &sourcePath);
    [[nodiscard]] static QStringList compilerCandidates(Language language);
    [[nodiscard]] static CompilerFamily detectCompilerFamily(const QString &compilerPath);
    [[nodiscard]] static QString findExternalCompiler(Language language = Language::Auto,
                                                      const QString &preferred = QString());
    [[nodiscard]] static QString findArchiver(const QString &preferred = QString());
    [[nodiscard]] static QStringList systemIncludePaths(Language language = Language::Auto,
                                                        CompilerFamily family = CompilerFamily::Gnu,
                                                        const QString &compilerPath = QString());
    [[nodiscard]] static QStringList systemLibraryPaths(CompilerFamily family = CompilerFamily::Gnu,
                                                        const QString &compilerPath = QString());
    [[nodiscard]] static QStringList resolveTccIncludePaths();
    [[nodiscard]] static QStringList resolveTccLibraryPaths();
};

}
