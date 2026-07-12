#pragma once

#include "ccc/backends/compilerbackend.h"
#include <memory>

namespace ccc {

class CCC_EXPORT InternalTccBackend final : public CompilerBackend {
public:
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] CompilationResult compile(const QString &sourcePath,
                                             const QString &outputPath,
                                             const CompileConfig &config) const override;
    [[nodiscard]] std::unique_ptr<MemoryModule> compileToMemory(const QString &sourcePathOrCode,
                                                                const CompileConfig &config,
                                                                QString &errorMessage) const;
};

class TccMemoryModule final : public MemoryModule {
public:
    explicit TccMemoryModule(void *tccState);
    ~TccMemoryModule() override;
    [[nodiscard]] void* getSymbol(const QString &name) const override;
private:
    void *m_tccState = nullptr;
};

}
