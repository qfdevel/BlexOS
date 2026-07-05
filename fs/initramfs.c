

































/*
 * config/initramfs.c – Initramfs boot-time setup
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Orchestrates:
 * 1. Multiboot2 module detection
 * 2. CPIO archive extraction into ES1
 */

#include "../kernel_utils.h"
#include "initramfs.h"
#include "cpio.h"
#include "../config/system.h"

/* ══════════════════════════════════════════════════════
 * INITRAMFS MODULE ADDRESSES
 * (filled in by detect_initramfs, used by cpio)
 * ══════════════════════════════════════════════════════ */
uint32_t initramfs_mod_start = 0;
uint32_t initramfs_mod_end   = 0;

/* ══════════════════════════════════════════════════════
 * MULTIBOOT2 TAG WALKER
 * ══════════════════════════════════════════════════════ */

#define MB2_MAGIC_EXPECTED 0x36D76289u

typedef struct { uint32_t type; uint32_t size; } mb2_tag_t;
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char     cmdline[];
} mb2_module_tag_t;

static void detect_initramfs(uint32_t mb2_magic, void* mb2_info) {
    if (mb2_magic != MB2_MAGIC_EXPECTED) {
        print_str("[initramfs] no Multiboot2 module\n");
        return;
    }
    uint8_t *p     = (uint8_t*)mb2_info + 8;
    uint32_t total = *(uint32_t*)mb2_info;
    uint8_t *end   = (uint8_t*)mb2_info + total;

    int found = 0;
    while (p < end) {
        mb2_tag_t *tag = (mb2_tag_t*)p;
        if (tag->type == 0) break;
        if (tag->type == 3) {
            mb2_module_tag_t *mt = (mb2_module_tag_t*)p;
            initramfs_mod_start = mt->mod_start;
            initramfs_mod_end   = mt->mod_end;
            print_str("[initramfs] module mapped\n");
            found++;
        }
        uint32_t aligned = (tag->size + 7) & ~7u;
        p += aligned;
    }
    if (!found) print_str("[initramfs] no module\n");
}

/* ══════════════════════════════════════════════════════
 * PUBLIC: initramfs_setup
 * ══════════════════════════════════════════════════════ */

void initramfs_setup(uint32_t mb2_magic, void *mb2_info) {
    /* 1. Locate the initramfs module from Multiboot2 tags */
    detect_initramfs(mb2_magic, mb2_info);

    /* 2. Extract every file from the CPIO archive into ES1 */
    if (initramfs_mod_start != 0) {
        const char *archive = (const char*)(uintptr_t)initramfs_mod_start;
        int n = cpio_extract_all(archive);
        if (n > 0) {
            print_str("[initramfs] extracted\n");
        } else if (n == 0) {
            print_str("[initramfs] empty archive\n");
        } else {
            print_str("[initramfs] bad CPIO\n");
        }
    } else {
        print_str("[initramfs] no data\n");
    }
}
