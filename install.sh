#! /bin/bash

set -e

mkdir -p build
cd build

# This marks where we install the software
PREFIX=/opt/local/z80-none-elf

if [ ! -f binutils-2.43/configure ]; then
  curl -C - -LO https://ftp.gnu.org/gnu/binutils/binutils-2.43.tar.lz
  tar xf binutils-2.43.tar.lz
fi

(cd binutils-2.43 \
    && ./configure --target=z80-none-elf --program-prefix=z80-none-elf- --prefix=$PREFIX \
    && make -j$(nproc) \
    && make install
)

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

