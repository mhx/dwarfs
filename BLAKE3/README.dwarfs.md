# Vendored copy of the BLAKE3 C implementation

Upstream:  https://github.com/BLAKE3-team/BLAKE3
Version:   1.8.5
Tag:       1.8.5
Commit:    93a431c78a52d7ccf0f366f106467f5070e6075e
Retrieved: 2026-08-04

License texts are in `LICENSE_CC0`, `LICENSE_A2` and `LICENSE_A2LLVM`,
copied verbatim from the upstream repository root.

## Contents

`c/` is a verbatim copy of the `c/` subdirectory of the upstream release,
with the following files removed as they are not needed here:

  - `example.c`, `example_tbb.c`, `main.c`, `.git-blame-ignore-revs`
  - `test.py`, `blake3_c_rust_bindings/`, `.cargo/` , `.github/`
  - `blake3_tbb.cpp` (DwarFS does its own parallelism)
