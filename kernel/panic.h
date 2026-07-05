/*
 * kernel/panic.h – Kernel panic interface
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Bir kernel panic durumunda mavi ekran (BSOD) gösterir,
 * hata bilgilerini (dosya, satır, mesaj, register state)
 * framebuffer'a basar ve sonsuz döngüye girer.
 */

#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <stdint.h>

/* ── Panic register state dump ────────────────────────── */
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
    uint32_t cr0, cr2, cr3, cr4;
} panic_regs_t;

/* ── Kernel panic (flag) ──────────────────────────────── */
extern volatile int kernel_panic_active;

/*
 * panic() – Mavi ekran göster ve dur.
 * 
 * Parameters:
 *   file    – Kaynak dosya adı (__FILE__)
 *   line    – Satır numarası      (__LINE__)
 *   fmt     – Sebep açıklaması (printf-stili)
 *   ...     – Format argümanları
 *
 * Örnek:  panic(__FILE__, __LINE__, "Out of memory: %d bytes", size);
 */
void panic(const char *file, int line, const char *fmt, ...);

/*
 * panic_regs() – Register dump'ı ile panic.
 * Yukarıdaki ile aynı, ama register state'i de basar.
 */
void panic_regs(const char *file, int line, panic_regs_t *regs,
                const char *fmt, ...);

/*
 * panic_assert() – Assert başarısız olursa panic'e düş.
 * Kullanım:  panic_assert(x > 0);
 * Genellikle: if (x > 0) {} else { panic_assert_fail(...); }
 */
#define panic_assert(cond)  do {                                \
    if (!(cond)) {                                              \
        panic(__FILE__, __LINE__, "Assertion failed: " #cond);  \
    }                                                           \
} while (0)

/*
 * panic_oops() – Daha hafif bir uyarı (OOPS).
 * Panic kadar sert değil, çalışmaya devam eder ama uyarır.
 */
void panic_oops(const char *file, int line, const char *fmt, ...);

#endif /* KERNEL_PANIC_H */
