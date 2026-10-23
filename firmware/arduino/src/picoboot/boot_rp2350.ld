/* The Pico bootloader on the RP2350 (Pico 2 W, 4 MB): the boot ROM boots it from the start of flash through its
   image definition (.embedded_block, in the first 4 KB), all within the 24 KB in front of the state sector
   (lib/proptroller/src/proptroller/picoboot_map.h). Flash routines run from RAM (.time_critical). */
MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 0x6000
    RAM(rwx)  : ORIGIN = 0x20000000, LENGTH = 520k
}
ENTRY(picoboot_reset)
__stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        KEEP(*(.embedded_block))
        __embedded_block_end = .;
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    ASSERT(__embedded_block_end - ORIGIN(FLASH) <= 4096, "the image definition must be in the first 4 KB")

    .data : {
        __data_start = .;
        *(.time_critical*)
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > FLASH
    __data_load = LOADADDR(.data);
    __flash_end = LOADADDR(.data) + SIZEOF(.data);

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.binary_info*) }
}
