#pragma once

#include "ccc/ccc_global.h"
#include "ccc/environment.h"
#include "ccc/types.h"

#include <QObject>

namespace ccc {

class CCC_EXPORT Engine final : public QObject {
public:
    explicit Engine(QObject *parent = nullptr);

    [[nodiscard]] CompilationResult compile(const QString &sourcePath,
                                            const QString &outputPath,
                                            const CompileConfig &config) const;

    [[nodiscard]] RunResult compileAndRun(const QString &sourcePath,
                                          const QString &outputPath,
                                          const CompileConfig &config,
                                          const QStringList &runArguments = QStringList()) const;

    [[nodiscard]] ExecutionResult runExecutable(const QString &executablePath,
                                                const QStringList &arguments = QStringList(),
                                                const QString &workingDirectory = QString()) const;

private:
    [[nodiscard]] static QString resolveWorkingDirectory(const QString &sourcePath,
                                                         const QString &configuredWorkingDirectory);
    [[nodiscard]] static QString resolveAbsolutePath(const QString &path,
                                                     const QString &workingDirectory);
};

}
