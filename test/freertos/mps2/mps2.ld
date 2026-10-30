/* Memory map of the MPS2 AN385 board as emulated by QEMU:
 * 4 MiB ZBT SSRAM1 at 0x00000000 holds the code, 4 MiB SSRAM2/3 at
 * 0x20000000 holds data, the newlib heap and the main stack.
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);
_main_stack_size = 16K;  /* Used before the scheduler starts and by ISRs. */

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
  } > FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx*)
  } > FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
  } > RAM

  /* The newlib heap (_sbrk) grows from here up to the main stack. */
  end = .;
  _end = .;
  _heap_limit = _estack - _main_stack_size;

  ASSERT(_ebss < _heap_limit, "RAM overflow")
}
