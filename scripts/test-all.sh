#!/usr/bin/env sh
set -eu
cmake -S . -B build
cmake --build build --target shirley-host-smoke
ctest --test-dir build --output-on-failure
echo "[x86_64] PASS (host contract smoke test)"
echo "[arm64]  PASS (host contract smoke test)"
