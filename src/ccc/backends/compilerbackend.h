#pragma once

#include "ccc/ccc_global.h"
#include "ccc/types.h"

namespace ccc {

class CCC_EXPORT CompilerBackend {
public:
    virtual ~CompilerBackend() = default;

    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual bool isAvailable() const = 0;
    [[nodiscard]] virtual CompilationResult compile(const QString &sourcePath,
                                                    const QString &outputPath,
                                                    const CompileConfig &config) const = 0;
};

}

