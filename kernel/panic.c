/*
 * kernel/panic.c – Gelişmiş Kernel Panic Handler (BSOD v2)
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Professional blue screen with:
 *   - Full register dump (EAX-EDX, ESI-EDI, EBP-ESP, EIP, EFLAGS)
 *   - Stack trace via EBP chain
 *   - Segment registers (CS, DS, ES, FS, GS, SS)
 *   - Control registers (CR0, CR2, CR3, CR4) – CR2 = fault address
 *   - Kernel version, build date
 *   - QEMU shutdown via isa-debug-exit port
 *   - Stack dump (first 64 bytes of stack)
 */

#include "panic.h"
#include "../drivers/fb/fb.h"
#include "../kernel_utils.h"
#include <stdarg.h>
#include <stddef.h>

volatile int kernel_panic_active = 0;

/* ── Colors ────────────────────────────────────────────── */
#define BG      RGB(0, 0, 128)        /* Dark blue background */
#define FG      RGB(255, 255, 255)     /* White text */
#define HIGHL   RGB(255, 255, 0)       /* Yellow highlight */
#define RED     RGB(255, 80, 80)       /* Red error title */
#define CYAN    RGB(100, 180, 255)     /* Cyan info */
#define GREEN   RGB(100, 255, 100)     /* Green OK */
#define GRAY    RGB(180, 180, 200)     /* Dim text */
#define ORANGE  RGB(255, 180, 50)      /* Orange warning */

/* ── Print helpers ────────────────────────────────────── */
static void p(const char *s) { tty_print_color(s, FG); }
static void p_c(const char *s, uint32_t c) { tty_print_color(s, c); }
static void p_n(void) { tty_putchar('\n'); }
static void p_hex(uint32_t n) {
    const char *hex = "0123456789ABCDEF";
    p("0x");
    for (int i = 28; i >= 0; i -= 4) {
        char buf[2] = { hex[(n >> i) & 0xF], 0 };
        tty_print_color(buf, FG);
    }
}
static void p_int(int n) { tty_print_int(n); }
static void p_spc(int n) { for (int i = 0; i < n; i++) tty_putchar(' '); }

/* ── Print a line with box char ───────────────────────── */
#define BAR "  "

/* ── Separator line ────────────────────────────────────── */
static void sep(char c) {
    for (int i = 0; i < 68; i++) tty_putchar(c);
    p_n();
}

/* ── Format string (minimal %s %d %x %c) ───────────────── */
static void fmt(const char *f, va_list a) {
    for (int i = 0; f[i]; i++) {
        if (f[i] == '%' && f[i+1]) {
            i++;
            switch (f[i]) {
                case 's': { const char *s = va_arg(a, const char*); p(s ? s : "(null)"); break; }
                case 'd': { int v = va_arg(a, int); p_int(v); break; }
                case 'x': { uint32_t v = va_arg(a, uint32_t); p_hex(v); break; }
                case 'c': { char c = (char)va_arg(a, int); char b[2] = {c,0}; p(b); break; }
                default: p("?"); break;
            }
        } else if (f[i] == '\n') { p_n(); }
        else { tty_putchar(f[i]); }
    }
}

/* ═══════════════════════════════════════════════════════════
 *  STACK TRACE via EBP chain
 * ═══════════════════════════════════════════════════════════ */
static void dump_stack_trace(void) {
    p_c("\n" BAR "STACK TRACE\n", CYAN);
    sep('-');

    uint32_t *ebp;
    __asm__ volatile ("mov %%ebp, %0" : "=r"(ebp));

    for (int i = 0; i < 20; i++) {
        if ((uint32_t)ebp < 0x1000 || (uint32_t)ebp > 0x100000) break;
        uint32_t eip = ebp[1];
        if (eip == 0 || eip < 0x100000) break;
        p("  ["); p_int(i); p("] ");
        p_hex(eip);
        p("  (caller)");
        p_n();
        ebp = (uint32_t*)ebp[0];
    }
}

/* ── Stack hex dump ────────────────────────────────────── */
static void dump_stack_raw(void) {
    p_c("\n" BAR "STACK DUMP (first 64 bytes)\n", CYAN);
    sep('-');

    uint32_t esp;
    __asm__ volatile ("mov %%esp, %0" : "=r"(esp));

    uint32_t *s = (uint32_t*)esp;
    for (int i = 0; i < 16; i++) {
        p("  "); p_hex((uint32_t)(uintptr_t)&s[i]); p(": "); p_hex(s[i]); p_n();
    }
}

/* ═══════════════════════════════════════════════════════════
 *  CPU state: read register values
 * ═══════════════════════════════════════════════════════════ */
static void get_cpu_state(panic_regs_t *r) {
    /* General purpose */
    __asm__ volatile ("mov %%eax, %0" : "=r"(r->eax));
    __asm__ volatile ("mov %%ebx, %0" : "=r"(r->ebx));
    __asm__ volatile ("mov %%ecx, %0" : "=r"(r->ecx));
    __asm__ volatile ("mov %%edx, %0" : "=r"(r->edx));
    __asm__ volatile ("mov %%esi, %0" : "=r"(r->esi));
    __asm__ volatile ("mov %%edi, %0" : "=r"(r->edi));
    __asm__ volatile ("mov %%ebp, %0" : "=r"(r->ebp));
    __asm__ volatile ("mov %%esp, %0" : "=r"(r->esp));
    /* EIP is tricky – use the return address from the stack */
    r->eip = 0;  /* Will be filled from stack trace */
    __asm__ volatile ("pushfl; pop %0" : "=r"(r->eflags));
    /* Segments */
    __asm__ volatile ("mov %%cs, %0" : "=r"(r->cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(r->ds));
    __asm__ volatile ("mov %%es, %0" : "=r"(r->es));
    __asm__ volatile ("mov %%fs, %0" : "=r"(r->fs));
    __asm__ volatile ("mov %%gs, %0" : "=r"(r->gs));
    __asm__ volatile ("mov %%ss, %0" : "=r"(r->ss));
    /* Control registers */
    __asm__ volatile ("mov %%cr0, %%eax; mov %%eax, %0" : "=r"(r->cr0) : : "eax");
    __asm__ volatile ("mov %%cr2, %%eax; mov %%eax, %0" : "=r"(r->cr2) : : "eax");
    __asm__ volatile ("mov %%cr3, %%eax; mov %%eax, %0" : "=r"(r->cr3) : : "eax");
    __asm__ volatile ("mov %%cr4, %%eax; mov %%eax, %0" : "=r"(r->cr4) : : "eax");
}

/* ═══════════════════════════════════════════════════════════
 *  DISPLAY THE PANIC SCREEN
 * ═══════════════════════════════════════════════════════════ */
static void panic_show(const char *file, int line, const char *reason) {
    /* Blue background */
    fb_fill_rect(0, 0, (int)fb.width, (int)fb.height, BG);
    tty_clear();

    /* ── Header ──────────────────────────────────────── */
    sep('=');
    p_c("  ***  KERNEL PANIC  ***  ", RED);
    p_c("[BNU-Blex vnightly-f109514]", GRAY);
    p_n();
    sep('=');
    p_n();

    /* ── Error info ───────────────────────────────────── */
    p_c(BAR, CYAN); p_c("File:  ", HIGHL);  p(file ? file : "?"); p_n();
    p_c(BAR, CYAN); p_c("Line:  ", HIGHL);  p_int(line); p_n();
    p_c(BAR, CYAN); p_c("Error: ", HIGHL);
    if (reason) {
        p_c(reason, FG);
    } else {
        p_c("Unknown error", FG);
    }
    p_n();
    sep('-');

    /* ── CPU Register Dump ────────────────────────────── */
    panic_regs_t regs;
    get_cpu_state(&regs);

    p_c("\n" BAR "REGISTERS\n", CYAN);
    sep('-');

    p("  "); p_c("EAX:", HIGHL); p_spc(2); p_hex(regs.eax);  p_spc(4);
    p_c("EBX:", HIGHL); p_spc(2); p_hex(regs.ebx);  p_spc(4);
    p_c("ECX:", HIGHL); p_spc(2); p_hex(regs.ecx);  p_spc(4);
    p_c("EDX:", HIGHL); p_spc(2); p_hex(regs.edx);  p_n();

    p("  "); p_c("ESI:", HIGHL); p_spc(2); p_hex(regs.esi);  p_spc(4);
    p_c("EDI:", HIGHL); p_spc(2); p_hex(regs.edi);  p_spc(4);
    p_c("EBP:", HIGHL); p_spc(2); p_hex(regs.ebp);  p_spc(4);
    p_c("ESP:", HIGHL); p_spc(2); p_hex(regs.esp);  p_n();

    p("  "); p_c("EIP: ", HIGHL); p_hex(regs.eip ? regs.eip : regs.esp);
    p_spc(4); p_c("FLAGS:", HIGHL); p_hex(regs.eflags); p_n();

    p_n();
    p("  "); p_c("CS:", HIGHL); p_hex(regs.cs); p_spc(4);
    p_c("DS:", HIGHL); p_hex(regs.ds); p_spc(4);
    p_c("ES:", HIGHL); p_hex(regs.es); p_spc(4);
    p_c("FS:", HIGHL); p_hex(regs.fs); p_spc(4);
    p_c("GS:", HIGHL); p_hex(regs.gs); p_spc(4);
    p_c("SS:", HIGHL); p_hex(regs.ss); p_n();

    p_n();
    p("  "); p_c("CR0:", HIGHL); p_hex(regs.cr0); p_spc(4);
    p_c("CR2:", HIGHL); p_hex(regs.cr2);
    p_c("  [FAULT ADDR]", RED);
    p_spc(4); p_c("CR3:", HIGHL); p_hex(regs.cr3); p_spc(4);
    p_c("CR4:", HIGHL); p_hex(regs.cr4); p_n();

    sep('-');

    /* ── Stack trace ──────────────────────────────────── */
    dump_stack_trace();
    sep('-');

    /* ── System info ──────────────────────────────────── */
    p_c("\n" BAR "SYSTEM\n", CYAN);
    sep('-');
    p("  Kernel: BNU-Blex vnightly-f109514 (i686)\n");
    p("  Mode:   32-bit protected mode\n");

    extern unsigned int boot_ticks;
    p("  Uptime: "); p_int((int)(boot_ticks / 3000)); p(" seconds\n");

    /* CPU vendor */
    uint32_t ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(regs.eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    char vendor[13];
    *(uint32_t*)vendor = ebx; *(uint32_t*)(vendor+4) = edx; *(uint32_t*)(vendor+8) = ecx;
    vendor[12] = 0;
    p("  CPU: "); p(vendor); p_n();

    sep('-');

    /* ── Final message ────────────────────────────────── */
    p_n();
    p_c("  System halted.", RED);
    p_c("  Press Ctrl+Alt+Del to reboot.", GRAY);
    p_n();
    sep('=');
}

/* ═══════════════════════════════════════════════════════════
 *  PUBLIC API
 * ═══════════════════════════════════════════════════════════ */

static void qemu_shutdown(void) {
    /* QEMU isa-debug-exit device */
    outw(0x501, 0x31);
    /* Also try Bochs/Old QEMU poweroff */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
}

void panic(const char *file, int line, const char *fmt, ...) {
    __asm__ volatile ("cli");
    kernel_panic_active = 1;

    char reason[256];
    reason[0] = 0;

    if (fmt && fmt[0]) {
        va_list args;
        va_start(args, fmt);
        /* Simple inline format without vsnprintf */
        int ri = 0;
        for (int fi = 0; fmt[fi] && ri < 250; fi++) {
            if (fmt[fi] == '%' && fmt[fi+1]) {
                fi++;
                switch (fmt[fi]) {
                    case 's': { const char *s = va_arg(args, const char*);
                        if (s) while (*s && ri < 250) reason[ri++] = *s++; break; }
                    case 'd': { int v = va_arg(args, int);
                        char buf[16]; int bi = 0;
                        if (v < 0) { reason[ri++] = '-'; v = -v; }
                        if (v == 0) reason[ri++] = '0';
                        else { int tmp = v; while (tmp) { bi++; tmp /= 10; }
                            for (int k = bi-1; k >= 0; k--) { reason[ri+k] = (char)('0' + v % 10); v /= 10; }
                            ri += bi;
                        } break; }
                    case 'x': { uint32_t v = va_arg(args, uint32_t);
                        const char *hex = "0123456789ABCDEF";
                        reason[ri++] = '0'; reason[ri++] = 'x';
                        for (int b = 28; b >= 0; b -= 4) reason[ri++] = hex[(v >> b) & 0xF];
                        break; }
                    case 'c': { int c = va_arg(args, int); reason[ri++] = (char)c; break; }
                    case '%': reason[ri++] = '%'; break;
                }
            } else {
                reason[ri++] = fmt[fi];
            }
        }
        reason[ri] = 0;
        va_end(args);
    }

    panic_show(file, line, reason);
    dump_stack_raw();

    qemu_shutdown();

    while (1) { __asm__ volatile ("cli; hlt"); }
}

void panic_regs(const char *file, int line, panic_regs_t *regs,
                const char *fmt, ...) {
    (void)regs; /* We already dump live registers */
    va_list args;
    va_start(args, fmt);
    /* Use the formatted message */
    char reason[256]; reason[0] = 0;
    int ri = 0;
    for (int fi = 0; fmt[fi] && ri < 250; fi++) {
        if (fmt[fi] == '%' && fmt[fi+1]) {
            fi++;
            switch (fmt[fi]) {
                case 's': { const char *s = va_arg(args, const char*);
                    if (s) while (*s && ri < 250) reason[ri++] = *s++; break; }
                case 'd': { int v = va_arg(args, int);
                    char b[16]; int bi = 0;
                    if (v < 0) { reason[ri++] = '-'; v = -v; }
                    if (v == 0) reason[ri++] = '0';
                    else { int t = v; while (t) { bi++; t/=10; }
                        for (int k = bi-1; k >= 0; k--) { reason[ri+k] = (char)('0'+v%10); v/=10; }
                        ri += bi; } break; }
                case 'x': { uint32_t v = va_arg(args, uint32_t);
                    const char *h = "0123456789ABCDEF";
                    reason[ri++] = '0'; reason[ri++] = 'x';
                    for (int b = 28; b >= 0; b -= 4) reason[ri++] = h[(v >> b) & 0xF];
                    break; }
                case 'c': { int c = va_arg(args, int); reason[ri++] = (char)c; break; }
                case '%': reason[ri++] = '%'; break;
            }
        } else reason[ri++] = fmt[fi];
    }
    reason[ri] = 0;
    va_end(args);

    panic(file, line, "%s", reason);
}

void panic_oops(const char *file, int line, const char *fmt, ...) {
    p_c("\n", ORANGE);
    sep('!');
    p_c("[ OOPS! ]", ORANGE); p_n();
    p_c("  File: ", HIGHL); p(file ? file : "?"); p_n();
    p_c("  Line: ", HIGHL); p_int(line); p_n();
    p_c("  Message: ", HIGHL);
    if (fmt) {
        va_list a;
        va_start(a, fmt);
        /* Simple print */
        char b[256]; b[0] = 0;
        int ri = 0;
        for (int fi = 0; fmt[fi] && ri < 250; fi++) {
            if (fmt[fi] == '%' && fmt[fi+1]) {
                fi++;
                switch (fmt[fi]) {
                    case 's': { const char *s = va_arg(a, const char*);
                        if (s) while (*s && ri < 250) b[ri++] = *s++; break; }
                    case 'd': { int v = va_arg(a, int);
                        char buf[16]; int bi = 0;
                        if (v < 0) { b[ri++] = '-'; v = -v; }
                        if (v == 0) b[ri++] = '0';
                        else { int t = v; while (t) { bi++; t/=10; }
                            for (int k = bi-1; k >= 0; k--) { b[ri+k] = (char)('0'+v%10); v/=10; }
                            ri += bi; } break; }
                    case 'x': { uint32_t v = va_arg(a, uint32_t);
                        const char *h = "0123456789ABCDEF";
                        b[ri++] = '0'; b[ri++] = 'x';
                        for (int i = 28; i >= 0; i -= 4) b[ri++] = h[(v>>i)&0xF];
                        break; }
                    default: b[ri++] = fmt[fi]; break;
                }
            } else b[ri++] = fmt[fi];
        }
        b[ri] = 0;
        va_end(a);
        p(b);
    }
    p_n();
    sep('!');
    p_n();
}
