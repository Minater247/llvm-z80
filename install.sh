#! /bin/bash

set -e

BINUTILS_FLAG=false
for arg in "$@"; do
  case $arg in
    --with-binutils) BINUTILS_FLAG=true;;
  esac
done

if ! command -v z80-none-elf-as >/dev/null 2>&1; then
  NEED_BINUTILS=true
else
  NEED_BINUTILS=false
fi

if $BINUTILS_FLAG; then
  NEED_BINUTILS=true
fi

mkdir -p build
cd build

PREFIX=${PREFIX:-/opt/local/z80-clang/}
BINUTILS_PREFIX=${BINUTILS_PREFIX:-/opt/local/z80-none-elf/}

if $NEED_BINUTILS; then
  if [ ! -f binutils-2.43/configure ]; then
    curl -C - -LO https://ftp.gnu.org/gnu/binutils/binutils-2.43.tar.lz
    tar xf binutils-2.43.tar.lz
  fi

  (cd binutils-2.43 \
      && ./configure --target=z80-none-elf --program-prefix=z80-none-elf- --prefix=$BINUTILS_PREFIX \
      && make -j$(nproc) \
      && make install
  )
else
  echo "Skipping binutils build"
fi

if [ ! -f build.ninja ]; then
  cmake -G Ninja -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_PROJECTS="clang" \
                        -DCMAKE_INSTALL_PREFIX=$PREFIX \
                        -DCMAKE_BUILD_TYPE=Release \
                        -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=Z80 \
                        -DLLVM_TARGETS_TO_BUILD= \
                        -DLLVM_DEFAULT_TARGET_TRIPLE=z80-none-elf \
                        ../llvm
fi

ninja install

