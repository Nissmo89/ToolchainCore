#pragma once

#include "ccc/backends/compilerbackend.h"

namespace ccc {

class CCC_EXPORT InternalTccBackend final : public CompilerBackend {
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] CompilationResult compile(const QString &sourcePath,
                                            const QString &outputPath,
                                            const CompileConfig &config) const override;
    [[nodiscard]] CompilationResult link(const QStringList &objectPaths,
                                         const QString &outputPath,
                                         const CompileConfig &config) const override;
    [[nodiscard]] static void* getSymbolAddress(const std::shared_ptr<void> &context,
                                                const QString &symbolName);
};

}
