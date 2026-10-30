/* Board support for the Arm MPS2 AN385 (Cortex-M3) as emulated by QEMU:
 * vector table, reset handler, CMSDK UART0 console and the handful of
 * newlib syscalls the demo needs. The exit status goes through semihosting,
 * so run QEMU with -semihosting-config enable=on.
 */
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define UART0_BASE  0x40004000u
#define UART0_DATA  (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART0_STATE (*(volatile uint32_t *)(UART0_BASE + 0x04))
#define UART0_CTRL  (*(volatile uint32_t *)(UART0_BASE + 0x08))
#define UART0_BAUD  (*(volatile uint32_t *)(UART0_BASE + 0x10))

#define SYS_EXIT        0x18
#define ADP_Stopped_ApplicationExit 0x20026
#define ADP_Stopped_RunTimeErrorUnknown 0x20023

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern char end, _heap_limit;

extern int main(int argc, char **argv);
extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

void Reset_Handler(void);
static void Fault_Handler(void);

__attribute__((section(".isr_vector"), used))
static void (*const vectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    Fault_Handler,      /* NMI */
    Fault_Handler,      /* HardFault */
    Fault_Handler,      /* MemManage */
    Fault_Handler,      /* BusFault */
    Fault_Handler,      /* UsageFault */
    0, 0, 0, 0,
    vPortSVCHandler,
    Fault_Handler,      /* DebugMon */
    0,
    xPortPendSVHandler,
    xPortSysTickHandler,
};

static int semihost(int op, void *arg)
{
    register int r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;

    __asm__ volatile ("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

static void uart_putc(char c)
{
    while (UART0_STATE & 1)  /* TX buffer full */
        ;
    UART0_DATA = (unsigned char)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

void Reset_Handler(void)
{
    static char *argv[] = { "app", 0 };
    uint32_t *src, *dst;

    for (src = &_sidata, dst = &_sdata; dst < &_edata;)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;

    UART0_BAUD = 16;
    UART0_CTRL = 1;  /* TX enable */

    exit(main(1, argv));
}

static void Fault_Handler(void)
{
    uart_puts("\r\nfault\r\n");
    _exit(1);
}

void vAssertCalled(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();
    printf("assertion failed: %s:%d\n", file, line);
    exit(1);
}

int _write(int fd, const char *buf, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\n')
            uart_putc('\r');
        uart_putc(buf[i]);
    }
    return len;
}

int _fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR;  /* Makes stdout line-buffered. */
    return 0;
}

int _isatty(int fd)
{
    return 1;
}

void *_sbrk(ptrdiff_t incr)
{
    static char *brk = &end;
    char *prev;

    if (brk + incr > &_heap_limit) {
        errno = ENOMEM;
        return (void *)-1;
    }
    prev = brk;
    brk += incr;
    return prev;
}

void _exit(int status)
{
    semihost(SYS_EXIT, (void *)(status == 0 ? ADP_Stopped_ApplicationExit
                                            : ADP_Stopped_RunTimeErrorUnknown));
    for (;;)
        ;
}
