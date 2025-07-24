`find llvm/test/CodeGen/Z80 -name "*.ll" -not -path "*/opt/*" | head -5 | xargs -I {} build/bin/llc -mtriple=z80 -O2 {} -o /dev/null`

`find llvm/test/CodeGen/Z80 -name "*.ll" -not -path "*/opt/*" | head -10 | xargs -I {} build/bin/llc -mtriple=z80 -O2 {} -o /dev/null`

`find llvm/test/CodeGen/Z80 -name "*.ll" -not -path "*/opt/*" -not -name "operations24.ll" | head -10 | xargs -I {} build/bin/llc -mtriple=z80 -O2 {} -o /dev/null`