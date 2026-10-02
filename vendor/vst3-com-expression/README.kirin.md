# Legacy VST3 expression macro fix

Snapshot: `robbert-vdh/vst3-sys`, revision
`b3ff4d775940f5b476b9d1cca02a90e07e1922a2`, `com/` (MIT; LICENSE retained).

The only code delta removes the trailing semicolon from `vtable!`'s returned
expression. Rust 1.99 reports that upstream expansion in the consuming
PRE/POST crates. It must return the same vtable, not suppress the warning.
COM layout, function signatures, offsets, reference counting and class IDs
are unchanged. The upstream procedural macros remain pinned by Cargo.lock.

`tests/vtable_expression.rs` consumes the actual exported macro from a separate
crate with warnings denied and compares its layout and three function pointers
against the direct upstream trait call. The shipping JUCE shell does not use
this retired compatibility wrapper.
