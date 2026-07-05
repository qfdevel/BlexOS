/*
 * kernel/main.c – BNU-Blex x86 kernel (main entry)
 * Copyright (C) 2026 Blex – BOSL License
 *
 * KLAVYE: ORİJİNAL kernel.c ile AYNI mantık.
 *   - Düz PS/2 poll, scancode set 1
 *   - kbd_us tablosu (Enter = 0x1C → '\n')
 *   - shift basılıysa kbd_us_shift (Shift+4=$ vb.)
 *   - caps_lock: harfleri büyüt
 *   - 0xE0 tracking YOK (originalde de yoktu, sorun çıkarıyordu)
 *   - extended/arrows YOK
 */

#include "../kernel_utils.h"
#include "../shell/commands.h"
#include "../drivers/fb/fb.h"
// #include "../fonts/ttf_render.h"
#include "../drivers/initramfs.h"
#include "../drivers/cpio.h"
#include "../config/system.h"
#include "../kernel/panic.h"
#include "../kernel/kbd.h"
#include "../kernel/tty.h"
#include "../shell/pipe.h"

extern int   str_match(const char*, const char*);
extern void  str_copy(char*, const char*);
extern command_t command_table[];
extern int    command_count;

/* Defined in shell/commands.c */
extern char   history[10][64];
extern int    history_count;
extern int    history_index;
extern char   cwd_path[256];

/* Defined in kernel_utils.h / es1.c / sata.c */
extern ata_drive_t ata_drives[ATA_MAX_DRIVES];
extern es1_superblock_t es1_sb;
extern es1_node_t es1_nodes[ES1_MAX_NODES];



char          current_user[16]    = "user";
char          system_hostname[16] = "Blex";
unsigned int  boot_ticks          = 0;

char shell_buffer[64];
int  buf_idx = 0;
int  cursor_x = 0, cursor_y = 0;
char current_color = 0x07;

void update_cursor(int x, int y) { (void)x;(void)y; }
void clear_screen(void) { tty_clear(); }
void putchar(char c) { tty_putchar(c); }
void print_str(const char* s) { tty_print(s); }
void print_str_color(const char* s, char a) {
    static const uint32_t cmap[16] = {
        [0xA]=COL_GREEN, [0xB]=COL_CYAN, [0xC]=COL_RED,
        [0xE]=COL_YELLOW, [0xF]=COL_WHITE
    };
    tty_print_color(s, cmap[a&0xF] ? cmap[a&0xF] : COL_TEXT_FG);
}
void print_int(int n) { tty_print_int(n); }

void bootlog_ok(const char* m)   { print_str_color("[  OK  ] ",0xA); print_str(m); putchar('\n'); }
void bootlog_info(const char* m) { print_str_color("[ BOOT ] ",0xB); print_str(m); putchar('\n'); }
void bootlog_warn(const char* m) { print_str_color("[ WARN ] ",0xE); print_str(m); putchar('\n'); }
static void boot_delay(void) { for(volatile int i=0;i<2000000;i++); }

typedef void (*brun_entry_t)(void);
void brun_exec(const char* fn) {
    es1_node_t *n = es1_find(fn);
    if (!n) { print_str_color("brun: not found\n",0xC); return; }
    brun_header_t *h = (brun_header_t*)n->inline_data;
    if (h->magic != BRUN_MAGIC) { print_str_color("brun: bad magic\n",0xC); return; }
    ((brun_entry_t)(n->inline_data+sizeof(brun_header_t)))();
    print_str_color("\nbrun: done.\n",0xA);
}

static void report_sata(void) {
    int a=0;
    for(int d=0;d<ATA_MAX_DRIVES;d++) {
        if(!ata_drives[d].present) continue;
        if(!a){bootlog_ok("ATA/SATA drives:");a=1;}
        print_str("         drive");putchar('0'+d);
        print_str(ata_drives[d].is_sata?" [SATA] ":" [ATA]  ");
        print_str(ata_drives[d].model);print_str(" – ");
        print_int((int)(ata_drives[d].sectors/2048));print_str(" MB\n");
    }
    if(!a) bootlog_warn("No ATA/SATA drives");
}
static void report_es1(void) {
    print_str_color("[  OK  ] ",0xA);
    print_str("ES1: ");print_int((int)es1_sb.used_nodes);
    print_str("/");print_int(ES1_MAX_NODES);
    print_str(" nodes, \"");print_str(es1_sb.label);print_str("\"\n");
}

void print_prompt(void) { tty_print_prompt(current_user, system_hostname); }

void process_command(void) {
    shell_buffer[buf_idx]=0; putchar('\n');
    if(!buf_idx) { print_prompt(); return; }
    /* history */
    if(history_count<10) str_copy(history[history_count++],shell_buffer);
    else { for(int i=0;i<9;i++)str_copy(history[i],history[i+1]);str_copy(history[9],shell_buffer); }
    history_index=history_count;
    /* pipe check */
    char *p=shell_buffer; int hp=0;
    while(*p){if(*p=='|'){hp=1;break;}p++;}
    if(hp&&p>shell_buffer){
        char c1[32],a1[256],c2[64],a2[256];
        if(pipe_parse(shell_buffer,c1,32,a1,256,c2,64,a2,256)){
            pipe_state.active=1;pipe_state.length=0;
            int f=0;
            for(int k=0;k<command_count;k++){if(str_match(c1,command_table[k].name)){f=1;command_table[k].func(a1);break;}}
            if(!f){print_str_color("not found: ",0xC);print_str(c1);putchar('\n');pipe_init();}
            else{
                pipe_state.active=0;int f2=0;
                for(int k=0;k<command_count;k++){
                    if(str_match(c2,command_table[k].name)){f2=1;
                        if(str_match(c2,"cat")){pipe_state.buffer[pipe_state.length]=0;print_str(pipe_state.buffer);if(!pipe_state.length||pipe_state.buffer[pipe_state.length-1]!='\n')putchar('\n');}
                        else command_table[k].func(a2);break;
                    }
                }
                if(!f2)print_str_color("pipe: not found\n",0xC);
                pipe_init();
            }
        } else print_str_color("bad pipe\n",0xC);
    } else {
        char n[32];int i=0;
        while(shell_buffer[i]!=' '&&shell_buffer[i]&&i<31){n[i]=shell_buffer[i];i++;}n[i]=0;
        const char* ar=(shell_buffer[i]==' ')?&shell_buffer[i+1]:"";
        int f=0;
        for(int k=0;k<command_count;k++){if(str_match(n,command_table[k].name)){command_table[k].func(ar);f=1;break;}}
        if(!f){print_str_color("not found: ",0xC);print_str(n);putchar('\n');}
    }
    print_prompt();buf_idx=0;
}

/* ═══════════════════════════════════════════════════════
 *  KERNEL MAIN
 *  Klavye: ORİJİNAL kernel.c ile AYNI.
 *  Sadece shift komboları (kbd_us_shift) EKLENDİ.
 * ═══════════════════════════════════════════════════════ */

void kernel_main(uint32_t mb2_magic, uint64_t mb2_info) {
    fb_init(mb2_magic, (void*)(uintptr_t)mb2_info);
    if(!fb.addr) __asm__ volatile("cli;hlt");

    es1_init();
    initramfs_setup(mb2_magic, (void*)(uintptr_t)mb2_info);

#if 0
    int mouse_ok = mouse_init();
    if(mouse_ok) bootlog_ok("PS/2 mouse");
    else bootlog_warn("PS/2 mouse not found");
#endif

    pipe_init();

    bootlog_info("BNU-Blex starting..."); boot_delay();
    bootlog_ok("Framebuffer TTY (1920x1080)");
    bootlog_info("ES1..."); report_es1();
    bootlog_info("ATA/SATA..."); ata_init(); report_sata();
    bootlog_info("PS/2 keyboard..."); boot_delay();
    bootlog_ok("Keyboard ready");
    bootlog_info("Shell..."); boot_delay();

    tty_print_color("\n  BNU-Blex x86 vnightly-f109514  (c) 2026 Blex\n\n", COL_CYAN);
    tty_print_color("  Type 'help' for commands.\n\n", COL_TEXT_FG);
    print_prompt();

    /* ──────────────────────────────────────────────────────
     * ORİJİNAL KLAVYE DÖNGÜSÜ
     *
     * Aynen kernel.c'deki gibi:
     *   1. Port 0x64'ten veri var mı kontrol
     *   2. Port 0x60'tan scancode oku
     *   3. Modifier tracking (0x1D, 0x2A/0x36, 0x38, 0x3A)
     *   4. is_release skip
     *   5. kbd_us[key] → karakter
     *   6. Enter → process_command()
     *
     * FARK: shift_pressed ise kbd_us_shift kullan
     *       (Shift+4=$ , Shift+8=* , Shift+'=" , vb.)
     *       caps_lock ise kbd_us'taki harfleri büyüt.
     *
     * NOT: 0xE0 tracking YOK. Arrows çalışmaz ama
     *      regular tuşlar TERTEMİZ çalışır.
     * ────────────────────────────────────────────────────── */
    int shift_pressed = 0;
    int caps_lock     = 0;

    while (1) {
        boot_ticks++;

/* DEWM mouse polling */
#if 0
        if (dwm_is_active() && mouse_ok) {
            static int mpc = 0;
            if (++mpc >= 10) { mpc=0;
                if (mouse_has_data()) { 
                    mouse_poll(); 
                    // dwm_handle_mouse(...) satırı zaten içerideydi
                }
            }
        }
#endif

        /* ══════════════════════════════════════════════
         * KLAVYE – ORİJİNAL kernel.c İLE AYNI
         * ══════════════════════════════════════════════ */
        if (inb(0x64) & 1) {
            unsigned char sc  = inb(0x60);
            unsigned char key = sc & 0x7F;
            int is_release    = (sc & 0x80);

            /* ── Modifier tracking ──────────────────── */
            if (key == 0x1D) { /* Ctrl */ continue; }      /* ignore ctrl for now */
            if (key == 0x2A || key == 0x36) { shift_pressed = !is_release; continue; }
            if (key == 0x38) { /* Alt */ continue; }       /* ignore alt for now */
            if (key == 0x3A && !is_release) { caps_lock = !caps_lock; continue; }

            /* Release: skip */
            if (is_release) continue;

            /* ── Translate ──────────────────────────── */
            unsigned char c;
            if (shift_pressed) {
                c = kbd_us_shift[key];   /* $, *, {, }, :, ", |, vb. */
            } else {
                c = kbd_us[key];          /* normal layout */
                if (caps_lock && c >= 'a' && c <= 'z') {
                    c -= 32;              /* Caps Lock → büyük harf */
                }
            }

            /* ── Action ─────────────────────────────── */
            if (c == '\n') {
                process_command();
            } else if (c == '\b') {
                if (buf_idx > 0) { buf_idx--; putchar('\b'); putchar(' '); putchar('\b'); }
            } else if (c != 0 && buf_idx < 63) {
                shell_buffer[buf_idx++] = c;
                putchar(c);
            }
        }

        for (volatile int i = 0; i < 1000; i++);
    }
}
