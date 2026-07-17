#pragma once

#include "ccc/backends/compilerbackend.h"

namespace ccc {

class CCC_EXPORT ExternalCompilerBackend final : public CompilerBackend {
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] CompilationResult compile(const QString &sourcePath,
                                            const QString &outputPath,
                                            const CompileConfig &config) const override;
    [[nodiscard]] CompilationResult link(const QStringList &objectPaths,
                                         const QString &outputPath,
                                         const CompileConfig &config) const override;

private:
    [[nodiscard]] static CompilationResult runProcess(const QString &program,
                                                      const QStringList &arguments,
                                                      const QString &workingDirectory,
                                                      bool captureOutput);
    [[nodiscard]] static QStringList normalizePathArgs(const QStringList &paths,
                                                       const QString &workingDirectory,
                                                       const QString &prefix);
    [[nodiscard]] static QStringList normalizeDefinitions(const QStringList &definitions,
                                                          const QString &prefix);
    [[nodiscard]] static QStringList normalizeLibrariesGnu(const QStringList &libraries);
    [[nodiscard]] static QStringList normalizeLibrariesMsvc(const QStringList &libraries);
    [[nodiscard]] static QString resolvedCompiler(Language language, const QString &preferredCompiler);
    [[nodiscard]] static QString resolvedArchiver(const QString &preferredArchiver);
};

}

