TODO:
- Look into Z80StaticStackPass. I don't see very many cases where push/pop wouldn't save several cycles - the README example would save 10T with normal stack ops.
- The copy fix in Z80InstructionSelector (commit after "Adjust the static stack pass to use ...") does what it needs to but it's a guard rather than a proper fix. Don't like it, but it does work.