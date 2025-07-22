TODO:
- Verify that intermediate instructions are handled reasonably during RSO pass
- Manual testing to ensure no functionality degredation from RSO pass
- Fixups for non-`+full` triples, since they appear to core dump semiregularly
- Update calling conventions for ez80 and non-`+full` z80 to also use registers, fix test cases to match