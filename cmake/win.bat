REM
REM Copyright (c) Marcus Holland-Moritz
REM
REM This file is part of dwarfs.
REM
REM Permission is hereby granted, free of charge, to any person obtaining a copy
REM of this software and associated documentation files (the “Software”), to deal
REM in the Software without restriction, including without limitation the rights
REM to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
REM copies of the Software, and to permit persons to whom the Software is
REM furnished to do so, subject to the following conditions:
REM
REM The above copyright notice and this permission notice shall be included in
REM all copies or substantial portions of the Software.
REM
REM THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
REM IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
REM FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
REM AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
REM LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
REM OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
REM SOFTWARE.
REM
REM SPDX-License-Identifier: MIT
REM

@ECHO OFF

SET build_mode=%1
SHIFT

IF "%build_mode%"=="" (
  SET build_mode=Release
)

cmake .. -GNinja -DCMAKE_BUILD_TYPE=%build_mode% -DWITH_UNIVERSAL_BINARY=ON -DWITH_TESTS=ON -DWITH_BENCHMARKS=ON -DWITH_PXATTR=ON -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%\git\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_INSTALLED_DIR=%USERPROFILE%\git\@vcpkg-install-dwarfs %*
