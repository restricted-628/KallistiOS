OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_member_entry)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x101ff000;
    .page0 : { *(.text.page0) } :code
    . = 0x10200000;
    .page1 : { *(.text.page1) } :code
    . = 0x10201000;
    .page2 : { *(.text.page2) } :code
    . = 0x10202000;
    .page3 : { *(.text.page3) } :code
    . = 0x10203000;
    .page4 : { *(.text.page4) } :code
    . = 0x10204000;
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) } :data
    /DISCARD/ : { *(.comment .note* .eh_frame*) }
    ASSERT(SIZEOF(.page0) <= 4096 && SIZEOF(.page1) <= 4096 &&
           SIZEOF(.page2) <= 4096 && SIZEOF(.page3) <= 4096 &&
           SIZEOF(.page4) <= 4096, "text page overflow")
    ASSERT(SIZEOF(.data) == 4 && SIZEOF(.bss) == 4, "fixture data layout changed")

}
