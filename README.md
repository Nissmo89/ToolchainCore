# ToolchainCore

ToolchainCore is the headless Qt C++ toolchain module for `nord_C`.

Current focus:

- compiler and archiver discovery
- path normalization for include and library search paths
- compile configuration and result types
- external compiler and archiver based disk builds
- executable launch and output capture

LSP / diagnostics are intentionally out of scope here and live in a separate module.

## Build

```bash
cmake -S . -B build
cmake --build build
```

The module is designed to be embedded into `Nord_C`, not used as a standalone app.

## Documentation

- [API documentation](docs/doc.md)
