/* The Pico bootloader on the RP2040 (Pico W, 2 MB): boot2 in the first 256 bytes (the boot ROM runs it; it sets
   up the flash and enters the vector table at 0x10000100), the bootloader after it, all within the 24 KB in front
   of the state sector (lib/proptroller/src/proptroller/picoboot_map.h). Flash routines run from RAM (.time_critical). */
MEMORY
{
    BOOT2(rx) : ORIGIN = 0x10000000, LENGTH = 0x100
    FLASH(rx) : ORIGIN = 0x10000100, LENGTH = 0x5F00
    RAM(rwx)  : ORIGIN = 0x20000000, LENGTH = 264k
}
ENTRY(picoboot_reset)
__stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .boot2 : { __boot2_start__ = .; KEEP(*(.boot2)) __boot2_end__ = .; } > BOOT2
    ASSERT(__boot2_end__ - __boot2_start__ == 256, "boot2 must be 256 bytes")

    .text : {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

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
