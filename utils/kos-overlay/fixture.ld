/* Link-only fixture, not a complete application linker script. */
OUTPUT_ARCH(sh)
ENTRY(_overlay_entry)
PHDRS {
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = SLOT_BASE;
    .text : { *(.text .text.*) *(.rodata .rodata.*) } :code
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) }
}
