@ECHO OFF

SET build_mode=%1
SHIFT

IF "%build_mode%"=="" (
  SET build_mode=Release
)

cmake .. -GNinja -DCMAKE_BUILD_TYPE=%build_mode% -DWITH_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=C:\Users\mhx\git\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_INSTALLED_DIR=C:\Users\mhx\git\@vcpkg-install-dwarfs
