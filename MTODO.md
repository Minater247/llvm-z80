TODO:
- Fixups for non-`+full` triples, since they appear to core dump semiregularly
    - `memory.ll` is failing, as it tries to copy a 24-bit register into a 16-bit one
- Update calling conventions for ez80 and non-`+full` z80 to also use registers, fix test cases to match