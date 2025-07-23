`llc -mtriple=z80 -O2 /home/minater247/llvm-z80/llvm/test/CodeGen/Z80/opt/repeated-store-opt-registers.ll -stop-before=z80-repeated-store-opt -o -`

`find llvm/test/CodeGen/Z80 -name "*.ll" -not -path "*/opt/*" | head -5 | xargs -I {} build/bin/llc -mtriple=z80 -O2 {} -o /dev/null`