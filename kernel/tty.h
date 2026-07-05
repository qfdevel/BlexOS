/*
 * kernel/tty.h – Framebuffer TTY (full-screen terminal)
 * Copyright (C) 2026 Blex – BOSL License
 */

#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <stdint.h>

/* ── TTY layout ─────────────────────────────────────── */
#define FONT_W    8   /* character cell width  */
#define FONT_H   16   /* character cell height */
#define TTY_PAD_X 8   /* left/right margin px  */
#define TTY_PAD_Y 4   /* top margin px         */

/* ── TTY colours ──────────────────────────────────────── */
#define TTY_BG_DEFAULT    RGB(10,  14,  22)
#define TTY_FG_DEFAULT    RGB(205, 214, 244)
#define TTY_CURSOR_COL    RGB(137, 180, 250)

/* ── TTY public API ───────────────────────────────────── */
void tty_clear        (void);
void tty_putchar      (char c);
void tty_print        (const char* s);
void tty_print_color  (const char* s, uint32_t col);
void tty_print_int    (int n);
void tty_print_prompt (const char* user, const char* host);

/* ── WM compatibility shims (TTY mode: no real WM) ───── */
#define wm_putchar      tty_putchar
#define wm_print        tty_print
#define wm_print_color  tty_print_color
#define wm_print_int    tty_print_int
#define wm_print_prompt tty_print_prompt
#define wm_clear_body(id) tty_clear()
#define wm_draw_all()   /* nop */
#define wm_draw_taskbar() /* nop */
#define wm_draw_desktop() /* nop */

/*
static inline int wm_open_window(int x, int y, int w, int h,
                                  const char* t, uint32_t bg) {
    (void)x; (void)y; (void)w; (void)h; (void)t; (void)bg;
    return 0;
}
*/

extern int focused_win;

#endif /* KERNEL_TTY_H */
