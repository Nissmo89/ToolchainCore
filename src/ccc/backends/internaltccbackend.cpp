#include "ccc/backends/internaltccbackend.h"

#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
#include "libtcc.h"
#endif

#include "ccc/environment.h"
#include <QFileInfo>
#include <QDir>

namespace ccc {

#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
namespace {

struct TccErrorBuffer {
    QStringList messages;
};

void tccErrorCollector(void *opaque, const char *msg)
{
    if (!opaque || !msg) {
        return;
    }
    auto *buffer = static_cast<TccErrorBuffer *>(opaque);
    buffer->messages << QString::fromUtf8(msg);
}

void configureTccState(TCCState *s, const CompileConfig &config, TccErrorBuffer &errors)
{
    tcc_set_error_func(s, &errors, tccErrorCollector);
    tcc_set_options(s, "-nostdinc");

    // Add configured include paths
    for (const QString &path : config.includePaths) {
        tcc_add_include_path(s, path.toUtf8().constData());
    }

    // Add standard TCC include paths resolved from Environment
    const QStringList tccIncludes = Environment::resolveTccIncludePaths();
    for (const QString &path : tccIncludes) {
        tcc_add_include_path(s, path.toUtf8().constData());
    }

    // Add library paths
    for (const QString &path : config.libraryPaths) {
        tcc_add_library_path(s, path.toUtf8().constData());
    }

    // Add standard TCC library paths
    const QStringList tccLibs = Environment::resolveTccLibraryPaths();
    for (const QString &path : tccLibs) {
        tcc_add_library_path(s, path.toUtf8().constData());
    }

    if (!tccLibs.isEmpty()) {
        tcc_set_lib_path(s, tccLibs.first().toUtf8().constData());
    }

    // Add definitions and compiler flags
    QStringList options;
    for (const QString &def : config.definitions) {
        if (def.startsWith(QLatin1String("-D"))) {
            options << def;
        } else {
            options << (QStringLiteral("-D") + def);
        }
    }
    for (const QString &flag : config.compilerFlags) {
        options << flag;
    }
    if (!options.isEmpty()) {
        tcc_set_options(s, options.join(QLatin1Char(' ')).toUtf8().constData());
    }
}

} // namespace
#endif

QString InternalTccBackend::name() const
{
    return QStringLiteral("internal-tcc");
}

bool InternalTccBackend::isAvailable() const
{
#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
    return true;
#else
    return false;
#endif
}

CompilationResult InternalTccBackend::compile(const QString &sourcePath,
                                              const QString &outputPath,
                                              const CompileConfig &config) const
{
    CompilationResult result;
    result.outputPath = outputPath;

#ifndef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
    Q_UNUSED(sourcePath);
    Q_UNUSED(config);
    result.success = false;
    result.errorMessage = QStringLiteral("Internal TinyCC backend is disabled in this build.");
    return result;
#else
    if (sourcePath.isEmpty()) {
        result.errorMessage = QStringLiteral("Source path is empty.");
        return result;
    }

    TCCState *s = tcc_new();
    if (!s) {
        result.errorMessage = QStringLiteral("Failed to create TCC state.");
        return result;
    }

    TccErrorBuffer errors;
    configureTccState(s, config, errors);

    int tccOutputType = TCC_OUTPUT_EXE;
    if (config.outputType == OutputType::SharedLibrary) {
        tccOutputType = TCC_OUTPUT_DLL;
    } else if (config.outputType == OutputType::StaticLibrary) {
        tccOutputType = TCC_OUTPUT_OBJ; // TCC doesn't build .a directly from multiple files easily
    }
    tcc_set_output_type(s, tccOutputType);

    if (tcc_add_file(s, sourcePath.toLocal8Bit().constData()) < 0) {
        result.success = false;
        result.errorMessage = errors.messages.join(QLatin1Char('\n'));
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Failed to add source file.");
        }
        tcc_delete(s);
        return result;
    }

    // Add libraries to link
    for (const QString &lib : config.libraries) {
        tcc_add_library(s, lib.toUtf8().constData());
    }

    if (tcc_output_file(s, outputPath.toLocal8Bit().constData()) < 0) {
        result.success = false;
        result.errorMessage = errors.messages.join(QLatin1Char('\n'));
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Link failed.");
        }
        tcc_delete(s);
        return result;
    }

    tcc_delete(s);
    result.success = true;
    return result;
#endif
}

std::unique_ptr<MemoryModule> InternalTccBackend::compileToMemory(const QString &sourcePathOrCode,
                                                                  const CompileConfig &config,
                                                                  QString &errorMessage) const
{
#ifndef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
    Q_UNUSED(sourcePathOrCode);
    Q_UNUSED(config);
    errorMessage = QStringLiteral("Internal TinyCC backend is disabled in this build.");
    return nullptr;
#else
    if (sourcePathOrCode.isEmpty()) {
        errorMessage = QStringLiteral("Source path or code is empty.");
        return nullptr;
    }

    TCCState *s = tcc_new();
    if (!s) {
        errorMessage = QStringLiteral("Failed to create TCC state.");
        return nullptr;
    }

    TccErrorBuffer errors;
    configureTccState(s, config, errors);

    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    bool isFile = QFileInfo::exists(sourcePathOrCode);
    if (isFile) {
        if (tcc_add_file(s, sourcePathOrCode.toLocal8Bit().constData()) < 0) {
            errorMessage = errors.messages.join(QLatin1Char('\n'));
            if (errorMessage.isEmpty()) {
                errorMessage = QStringLiteral("Failed to add source file.");
            }
            tcc_delete(s);
            return nullptr;
        }
    } else {
        if (tcc_compile_string(s, sourcePathOrCode.toUtf8().constData()) < 0) {
            errorMessage = errors.messages.join(QLatin1Char('\n'));
            if (errorMessage.isEmpty()) {
                errorMessage = QStringLiteral("Failed to compile source string.");
            }
            tcc_delete(s);
            return nullptr;
        }
    }

    // Add libraries
    for (const QString &lib : config.libraries) {
        tcc_add_library(s, lib.toUtf8().constData());
    }

    if (tcc_relocate(s, TCC_RELOCATE_AUTO) < 0) {
        errorMessage = errors.messages.join(QLatin1Char('\n'));
        if (errorMessage.isEmpty()) {
            errorMessage = QStringLiteral("Failed to relocate memory compilation.");
        }
        tcc_delete(s);
        return nullptr;
    }

    return std::make_unique<TccMemoryModule>(s);
#endif
}

TccMemoryModule::TccMemoryModule(void *tccState)
    : m_tccState(tccState)
{
}

TccMemoryModule::~TccMemoryModule()
{
#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
    if (m_tccState) {
        tcc_delete(static_cast<TCCState *>(m_tccState));
    }
#endif
}

void* TccMemoryModule::getSymbol(const QString &name) const
{
#ifdef TOOLCHAINCORE_ENABLE_INTERNAL_TCC
    if (m_tccState) {
        return tcc_get_symbol(static_cast<TCCState *>(m_tccState), name.toUtf8().constData());
    }
#endif
    Q_UNUSED(name);
    return nullptr;
}

} // namespace ccc
