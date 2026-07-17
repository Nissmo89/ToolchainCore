# ToolchainCore

ToolchainCore is the headless Qt C++ toolchain module for `nord_C`.

Current focus:

- compiler and archiver discovery
- path normalization for include and library search paths
- compile configuration and result types
- external compiler and archiver based disk builds
- optional internal TCC memory backend behind a build flag
- executable launch and output capture

LSP / diagnostics are intentionally out of scope here and live in a separate module.

## Build

```bash
cmake -S . -B build
cmake --build build
```

The module is designed to be embedded into `Nord_C`, not used as a standalone app.

To opt into the internal TCC backend, configure with `-DTOOLCHAINCORE_ENABLE_INTERNAL_TCC=ON`. The build still succeeds if TinyCC is not installed; the backend will just report itself unavailable at runtime.

## Documentation

- [API documentation](docs/doc.md)
