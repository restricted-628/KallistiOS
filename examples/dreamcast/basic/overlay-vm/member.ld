/* Original fixed-P0 test image: five instruction pages and crossing data/BSS. */
OUTPUT_ARCH(sh)
ENTRY(_overlay_entry)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = SLOT_BASE;
    .text.page0 : { *(.text.page0) } :code
    . = SLOT_BASE + 4096;
    .text.page1 : { *(.text.page1) } :code
    . = SLOT_BASE + 8192;
    .text.page2 : { *(.text.page2) } :code
    . = SLOT_BASE + 12288;
    .text.page3 : { *(.text.page3) } :code
    . = SLOT_BASE + 16384;
    .text.page4 : { *(.text.page4) } :code
    . = SLOT_BASE + 20480;
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) }
}
