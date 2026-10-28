#!/bin/sh
# Windows build via MSYS2 (UCRT64) + emsdk, both under C:\dev\tools.
# Run with build-windows.cmd; see CLAUDE.md "Building on Windows".
set -e
export ProgramFiles='C:\Program Files' ProgramData='C:\ProgramData'
export EMSDK=C:/dev/tools/emsdk
export EMSDK_NODE=C:/dev/tools/emsdk/node/24.19.0_64bit/node.exe
export EMSDK_PYTHON=C:/dev/tools/emsdk/python/3.13.3_64bit/python.exe
# shims/ holds sh launchers for emcc, emcmake etc. (emsdk only ships .bat on Windows).
# PATH entries must be /c/... style: the colon in C:/... splits the entry, and
# then cmd.exe (used by CMake's ExternalProject steps) can't find the .bat files.
export PATH="$PWD/node_modules/.bin:/c/dev/tools/shims:/c/dev/tools/emsdk/upstream/emscripten:/c/dev/tools/emsdk:$PATH"
export CMAKE_GENERATOR=Ninja CMAKE_POLICY_VERSION_MINIMUM=3.5
[ -d node_modules ] || npm ci
# npm run build would go through cmd.exe, which can't run build-wasm.sh
./build-wasm.sh
node build.js
