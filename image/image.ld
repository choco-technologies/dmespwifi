/*
 * The image of the Espressif Wi-Fi libraries: linked at fixed addresses. It lives in the flash from the offset 0x020000 (the free 896 kB
 * between the store of the selector and the bank A: nothing else of dmod-boot is there), which the selector maps at the identity (the flash
 * at 0x3C000000 for the data bus, at 0x42000000 for the instruction bus): the code is executed in place at 0x42020000, the read-only data
 * is read at 0x3C020000 (the same bytes: the instruction bus cannot read bytes), the initial .data follows it, and .data/.bss are in the
 * RAM that dmod-boot reserves for the image at the end of its RAM (0x3fcc4000, 16 kB).
 */
IMAGE_CODE      = 0x42000000;
IMAGE_DATA      = 0x3C000000;
CODE_TO_DATA    = IMAGE_CODE - IMAGE_DATA;

MEMORY
{
    rom (rx)  : ORIGIN = 0x42020000, LENGTH = 0x0E0000
    ram (rwx) : ORIGIN = 0x3fcc4000, LENGTH = 0x4000
}

ENTRY(dmwifi_image_header)

SECTIONS
{
    .text : ALIGN(4)
    {
        _image_start = .;
        KEEP(*(.header))
        *(.literal .literal.* .text .text.* .iram1 .iram1.* .wifi*iram*)
        . = ALIGN(16);
    } > rom AT > rom

    _rodata_lma = .;
    .rodata (_rodata_lma - CODE_TO_DATA) : AT(_rodata_lma)
    {
        *(.rodata .rodata.* .rodata_wlog_*)
        . = ALIGN(16);
    }

    _data_load_lma = ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 16);
    .data : AT(_data_load_lma)
    {
        . = ALIGN(4);
        _data_start = .;
        *(.data .data.* .dram1 .dram1.*)
        . = ALIGN(4);
        _data_end = .;
    } > ram

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    _data_load_d = _data_load_lma - CODE_TO_DATA;
    _image_end = _data_load_lma + SIZEOF(.data);
    _image_size = _image_end - _image_start;
    _data_size = _data_end - _data_start;
    _bss_size = _bss_end - _bss_start;
    _ram_end = _bss_end;

    /DISCARD/ : { *(.xtensa.info) *(.comment) *(.xt.*) *(.note*) *(.eh_frame*) }
}
