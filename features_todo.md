# Features to Implement (Full C/C++ Toolchain)

Below is the sequenced list of features to implement. We will build them one by one, compile/build them, and remove/mark them off as they are completed.

- [x] **Part 1: Compiler Family Discovery & Abstraction**
  * **Goal**: Detect if a discovered compiler is GNU-like (GCC/Clang), MSVC-like (`cl.exe`), or TinyCC. Create a system to format arguments based on this family.

- [x] **Part 2: Object File Output Type**
  * **Goal**: Add `OutputType::ObjectFile` and update `ExternalCompilerBackend` and `ccc::Engine` to support compiling source files directly to `.o` / `.obj` files without linking.

- [x] **Part 3: Linking Interface**
  * **Goal**: Add a `link` interface to `ccc::Engine` and the backends to allow linking multiple object files together into an executable, shared library, or static library.

- [x] **Part 4: Structured Optimization & Debug Flags**
  * **Goal**: Support high-level configurations in `CompileConfig` (like `OptimizationLevel` and `debugInfo`) so the user does not have to pass compiler-specific raw flag strings.

- [x] **Part 5: TinyCC JIT Symbol Resolution & Execution**
  * **Goal**: Enable callers to retrieve symbol addresses from a successfully JIT-compiled in-memory TCC execution context.
