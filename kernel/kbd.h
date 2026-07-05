/*
 * kernel/kbd.h – PS/2 Keyboard layout tables only
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Sadece klavye layout tabloları. Klavye okuma/işleme
 * doğrudan kernel/main.c içinde yapılır (orijinal
 * kernel.c'deki gibi).
 */

#ifndef KERNEL_KBD_H
#define KERNEL_KBD_H

/* US QWERTY layout table (scancode set 1) */
extern unsigned char kbd_us[128];
/* US QWERTY shift table */
extern unsigned char kbd_us_shift[128];

#endif /* KERNEL_KBD_H */
