#! /bin/bash

set -e

mkdir -p build
cd build

# This marks where we install the software
PREFIX=/opt/local/z80-none-elf

curl -C - -LO https://ftp.gnu.org/gnu/binutils/binutils-2.43.tar.lz

(tar xf binutils-2.43.tar.lz;
cd binutils-2.43 \
    && ./configure --target=z80-none-elf --program-prefix=z80-none-elf- --prefix=$PREFIX \
    && make -j$(nproc) \
    && make install
)

#(cd $PREFIX/bin; for i in ez80-none-*; do zname=z80-$(echo $i|cut -f2- -d"-"); if [ ! -f $zname ]; then ln -s $i $zname; fi; done)

cmake -G Ninja -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_PROJECTS="clang" \
                      -DCMAKE_INSTALL_PREFIX=$PREFIX \
                      -DCMAKE_BUILD_TYPE=Release \
                      -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=Z80 \
                      -DLLVM_TARGETS_TO_BUILD= \
                      -DLLVM_DEFAULT_TARGET_TRIPLE=z80-none-elf \
                      ../llvm

ninja install

