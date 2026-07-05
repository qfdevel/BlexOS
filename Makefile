# ===========================================================================
# BNU-Blex Build System – v2.0 (Linux-style directory layout)
# ===========================================================================
#
# Directory structure:
#   arch/x86/     – Boot assembly, linker script
#   kernel/       – Core kernel (main.c, kbd.c, panic.c)
#   drivers/      – Device drivers (fb/, sata/, net/, …)
#   fs/           – Filesystems (ES1, CPIO, initramfs, FAT32)
#   shell/        – Shell + commands + pipes + BSH
#   desktop/      – DEWM window manager + mouse + apps
#   fonts/        – TTF font renderer + font files
#   config/       – System configuration headers
#   initramfs/    – Initramfs source tree
#   scripts/      – Host-side tools
#   lib/          – Library functions (string, etc.)
#   include/      – Public headers
#   mm/           – Memory management (reserved for future)
# ===========================================================================

CC         := i686-linux-gnu-gcc
AS         := nasm
LDFLAGS    := -T linker.ld -nostdlib -lgcc
CFLAGS     := -m32 -ffreestanding -fno-stack-protector -fno-builtin -O2 -w -std=gnu99 -I. -I./include -Idrivers/fb

KERNEL_BIN  := mykernel.bin
INITRAMFS   := initramfs.cpio

# ── Object files (organized by directory) ─────────────────
ARCH_OBJS := arch/x86/boot.o

KERNEL_OBJS := kernel/main.o kernel/kbd.o kernel/panic.o

FB_OBJS     := drivers/fb/fb.o

FS_OBJS     := fs/initramfs.o fs/cpio.o

SHELL_OBJS  := shell/commands.o shell/pipe.o shell/bsh.o

DRIVER_OBJS := drivers/sata/sata.o fs/es1.o
# drivers/bootlog.o needs dmesg_write, pci_vendor_name, idt_table
# DRIVER_OBJS += drivers/bootlog.o drivers/pci_vendors.o

OBJS := $(ARCH_OBJS) $(KERNEL_OBJS) $(FB_OBJS) $(FS_OBJS) \
        $(SHELL_OBJS) $(DESKTOP_OBJS) $(DRIVER_OBJS) $(FONT_OBJS)

DONE    = @echo "  DONE    $@"
CC_LOG  = @echo "  CC      $<"
AS_LOG  = @echo "  AS      $<"
LD_LOG  = @echo "  LD      $(KERNEL_BIN)"

.PHONY: all clean run fix_time

all: fix_time $(INITRAMFS) $(KERNEL_BIN)

# ── Compilation rules ──────────────────────────────────────

# Boot
arch/x86/boot.o: arch/x86/boot.s
	$(AS_LOG)
	@$(AS) -f elf32 arch/x86/boot.s -o arch/x86/boot.o

# Kernel
kernel/main.o: kernel/main.c kernel_utils.h drivers/fb/fb.h kernel/panic.h kernel/kbd.h shell/commands.h shell/pipe.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

kernel/kbd.o: kernel/kbd.c kernel/kbd.h kernel_utils.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

kernel/panic.o: kernel/panic.c kernel/panic.h drivers/fb/fb.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

# Framebuffer
drivers/fb/fb.o: drivers/fb/fb.c drivers/fb/fb.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

# Filesystem
fs/initramfs.o: fs/initramfs.c fs/initramfs.h kernel_utils.h config/system.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

fs/cpio.o: fs/cpio.c fs/cpio.h kernel_utils.h config/system.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

# Shell
shell/commands.o: shell/commands.c shell/commands.h shell/pipe.h kernel/panic.h kernel/kbd.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

shell/pipe.o: shell/pipe.c shell/pipe.h shell/commands.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

shell/bsh.o: shell/bsh.c shell/bsh.h kernel_utils.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

desktop/mouse.o: desktop/mouse.c desktop/mouse.h drivers/fb/fb.h kernel_utils.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c $< -o $@ 2>.build_err || (cat .build_err && exit 1)

# SATA Driver
drivers/sata/sata.o: drivers/sata/sata.c drivers/sata/sata.h kernel_utils.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c drivers/sata/sata.c -o drivers/sata/sata.o 2>.build_err || (cat .build_err && exit 1)

# ES1 Filesystem
fs/es1.o: fs/es1.c kernel_utils.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c fs/es1.c -o fs/es1.o 2>.build_err || (cat .build_err && exit 1)

# Bootlog
drivers/bootlog.o: drivers/bootlog.c drivers/bootlog.h
	$(CC_LOG)
	@$(CC) $(CFLAGS) -c drivers/bootlog.c -o drivers/bootlog.o 2>.build_err || (cat .build_err && exit 1)

# Initramfs CPIO
$(INITRAMFS):
	@echo "  PACK    initramfs (Raw CPIO)"
	@cd initramfs && find . | cpio -o -H newc --quiet > ../initramfs.cpio

# Link kernel
$(KERNEL_BIN): $(OBJS)
	$(LD_LOG)
	@$(CC) -m32 -static -nostdlib -fno-pic -fno-pie -T linker.ld -o $@ $(OBJS) -lgcc
	$(DONE)
	@echo "--------------------------------------------------"
	@echo "  // BNU-Blex v2.0 //"
	@echo "  Kernel: $(KERNEL_BIN)"
	@echo "  Size:  $$(wc -c < $(KERNEL_BIN)) bytes"
	@echo "  Initramfs: $(INITRAMFS)"
	@echo "--------------------------------------------------"

fix_time:
	@touch kernel/main.c arch/x86/boot.s

# ── Run in QEMU ──────────────────────────────────────────
run: all
	@echo "  [ISO]   Preparing GRUB ISO..."
	@mkdir -p iso_root/boot/grub
	@cp $(KERNEL_BIN) iso_root/boot/
	@cp $(INITRAMFS) iso_root/
	@cp $(INITRAMFS) iso_root/boot/
	@echo 'set timeout=5' > iso_root/boot/grub/grub.cfg
	@echo 'set default=0' >> iso_root/boot/grub/grub.cfg
	@echo '' >> iso_root/boot/grub/grub.cfg
	@echo '# Catppuccin Theme' >> iso_root/boot/grub/grub.cfg
	@echo 'set color_normal=light-gray/black' >> iso_root/boot/grub/grub.cfg
	@echo 'set color_highlight=black/light-cyan' >> iso_root/boot/grub/grub.cfg
	@echo 'set menu_color_normal=white/black' >> iso_root/boot/grub/grub.cfg
	@echo 'set menu_color_highlight=black/light-cyan' >> iso_root/boot/grub/grub.cfg
	@echo '' >> iso_root/boot/grub/grub.cfg
	@echo 'menuentry "BNU-Blex OS" {' >> iso_root/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/mykernel.bin' >> iso_root/boot/grub/grub.cfg
	@echo '    module2 /boot/initramfs.cpio' >> iso_root/boot/grub/grub.cfg
	@echo '    boot' >> iso_root/boot/grub/grub.cfg
	@echo '}' >> iso_root/boot/grub/grub.cfg
	@echo '' >> iso_root/boot/grub/grub.cfg
	@echo 'menuentry "BNU-Blex (Verbose)" {' >> iso_root/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/mykernel.bin --verbose' >> iso_root/boot/grub/grub.cfg
	@echo '    module2 /boot/initramfs.cpio' >> iso_root/boot/grub/grub.cfg
	@echo '    boot' >> iso_root/boot/grub/grub.cfg
	@echo '}' >> iso_root/boot/grub/grub.cfg
	@echo '' >> iso_root/boot/grub/grub.cfg
	@echo 'menuentry "System Info" {' >> iso_root/boot/grub/grub.cfg
	@echo '    echo "BNU-Blex OS"' >> iso_root/boot/grub/grub.cfg
	@echo '    echo "Press any key to return..."' >> iso_root/boot/grub/grub.cfg
	@echo '    read' >> iso_root/boot/grub/grub.cfg
	@echo '}' >> iso_root/boot/grub/grub.cfg
	@echo '' >> iso_root/boot/grub/grub.cfg
	@echo 'menuentry "Reboot" {' >> iso_root/boot/grub/grub.cfg
	@echo '    reboot' >> iso_root/boot/grub/grub.cfg
	@echo '}' >> iso_root/boot/grub/grub.cfg
	@echo '' >> iso_root/boot/grub/grub.cfg
	@echo 'menuentry "Shutdown" {' >> iso_root/boot/grub/grub.cfg
	@echo '    halt' >> iso_root/boot/grub/grub.cfg
	@echo '}' >> iso_root/boot/grub/grub.cfg
	@grub-mkrescue -o bnu-blex.iso iso_root
	@echo "  [QEMU]  Launching BNU-Blex..."
	@qemu-system-x86_64 -cdrom bnu-blex.iso -m 1G -vga std -display sdl

# ── Clean ─────────────────────────────────────────────────
clean:
	@echo "  [CLEAN] Removing objects..."
	@rm -f $(OBJS) *.o
	@rm -f shell/*.o kernel/*.o fs/*.o desktop/*.o drivers/fb/*.o drivers/sata/*.o
	@rm -f $(KERNEL_BIN) $(INITRAMFS) $(MBEDIT_BIN) .build_err
	@rm -f bnu-blex.iso
	@rm -rf iso_root
	@echo "  [CLEAN] Done"
