extern unsigned _estack;
extern unsigned _sidata, _sdata, _edata, _sbss, _ebss;
int main(void);

static void reset_handler(void)
{
    unsigned *src = &_sidata, *dst = &_sdata;

#if defined(__ARM_FP) && __ARM_FP
    /* CPACR: grant full access to CP10 and CP11. Without this the first VFP
       instruction takes a UsageFault, which on this vector table is a hang. */
    *(volatile unsigned *)0xE000ED88u |= 0xFu << 20;
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
#endif

    while (dst < &_edata) { *dst++ = *src++; }
    for (dst = &_sbss; dst < &_ebss; ++dst) { *dst = 0u; }
    main();
    for (;;) { }
}

static void hang(void) { for (;;) { } }

__attribute__((section(".vectors"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void))&_estack,
    reset_handler,
    hang, hang, hang, hang, hang, hang, hang, hang,
    hang, hang, hang, hang, hang, hang
};

/* The two compiler-runtime symbols the library's own dependency check excuses.
   A real firmware gets them from its libc; this harness has none. */
void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);

void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;

    while (n--) { *p++ = *q++; }
    return d;
}

void *memset(void *d, int c, unsigned long n)
{
    unsigned char *p = (unsigned char *)d;

    while (n--) { *p++ = (unsigned char)c; }
    return d;
}
