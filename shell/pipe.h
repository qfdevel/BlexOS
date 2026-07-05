/*
 * shell/pipe.h – Shell pipe support for BlexOS
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Implements the "|" (pipe) operator for the shell.
 * Allows chaining commands like:  ls | cat
 *
 * How it works in a freestanding kernel:
 *   - Output redirection: First command's output is captured
 *     in a temporary buffer / ES1 file instead of going to TTY.
 *   - Second command reads from that buffer as input.
 *
 * Limitation: This is a simplified pipe implementation.
 * Commands must be in the format: cmd1 | cmd2
 * No arbitrary chaining (yet).
 */

#ifndef SHELL_PIPE_H
#define SHELL_PIPE_H

/* ── Pipe state ────────────────────────────────────────── */
typedef struct {
    char  buffer[4096];       /* Pipe buffer (captured output) */
    int   length;             /* Bytes written */
    int   read_pos;           /* Read cursor position */
    int   active;             /* 1 = pipe is active */
    char  second_cmd[64];     /* Command after the pipe */
    char  second_args[256];   /* Arguments for second command */
} pipe_state_t;

extern pipe_state_t pipe_state;

/*
 * pipe_init() – Pipe sistemini sıfırla.
 */
void pipe_init(void);

/*
 * pipe_start_capture() – Pipe'a yakalamaya başla.
 * Sonraki tüm print_str/putchar çıktıları pipe buffer'a gider.
 * Returns 1 if pipe was set up, 0 if no pipe in command.
 */
int pipe_start_capture(const char *cmdline);

/*
 * pipe_write() – Pipe buffer'a veri yaz.
 * (print_str/putchar pipe modundayken bunu çağırır)
 */
void pipe_write(const char *data, int len);

/*
 * pipe_end_capture() – Yakalamayı bitir.
 * İkinci komutu çalıştır ve pipe buffer'ı input olarak ver.
 */
void pipe_end_capture(void);

/*
 * pipe_has_data() – Pipe buffer'da okunacak veri var mı?
 */
int pipe_has_data(void);

/*
 * pipe_read_char() – Pipe buffer'dan bir karakter oku.
 * Returns -1 if no more data.
 */
int pipe_read_char(void);

/*
 * pipe_is_active() – Pipe aktif mi?
 */
int pipe_is_active(void);

/*
 * pipe_parse() – Komut satırını pipe'a göre ayır.
 * "ls | cat" → cmd1="ls", cmd2="cat"
 * Returns 1 if pipe found, 0 otherwise.
 */
int pipe_parse(const char *cmdline,
               char *cmd1, int cmd1_sz,
               char *args1, int args1_sz,
               char *cmd2, int cmd2_sz,
               char *args2, int args2_sz);

#endif /* SHELL_PIPE_H */
