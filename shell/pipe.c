/*
 * shell/pipe.c – Shell pipe implementation
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Simple pipe implementation for BlexOS shell.
 * Uses a fixed-size buffer (4096 bytes) to store
 * output from the first command, then feeds it as
 * input to the second command.
 *
 * The pipe character is "|". Example:
 *   help | cat
 *   ls /bin | cat
 */

#include "pipe.h"
#include "../kernel_utils.h"
#include "../shell/commands.h"

/* ══════════════════════════════════════════════════════
 *  GLOBALS
 * ══════════════════════════════════════════════════════ */

pipe_state_t pipe_state = { 0 };

/* ── String helpers ────────────────────────────────── */
static int p_strlen(const char *s) { int i=0; while(s[i]) i++; return i; }
static void p_strcpy(char *d, const char *s) {
    int i=0; while(s[i]) { d[i]=s[i]; i++; } d[i]='\0';
}
static int p_strncmp(const char *a, const char *b, int n) {
    for (int i=0; i<n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') break;
    }
    return 0;
}
static void p_strtrim(char *s) {
    int i = 0, j;
    /* Leading spaces */
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) {
        int k = 0;
        while (s[i+k]) { s[k] = s[i+k]; k++; }
        s[k] = '\0';
    }
    /* Trailing spaces */
    int len = p_strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}
static char* internal_strchr_p(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return 0;
}

/* ══════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════ */

void pipe_init(void) {
    pipe_state.active   = 0;
    pipe_state.length   = 0;
    pipe_state.read_pos = 0;
    pipe_state.buffer[0] = '\0';
    pipe_state.second_cmd[0] = '\0';
    pipe_state.second_args[0] = '\0';
}

/*
 * "ls | cat" → find the '|', split into cmd1+args1 and cmd2+args2
 */
int pipe_parse(const char *cmdline,
               char *cmd1, int cmd1_sz,
               char *args1, int args1_sz,
               char *cmd2, int cmd2_sz,
               char *args2, int args2_sz) {
    if (!cmdline) return 0;

    /* Find pipe symbol */
    char *pipe_pos = internal_strchr_p(cmdline, '|');
    if (!pipe_pos) return 0;

    /* Is it really a pipe? (not in a string or something) */
    /* Simple: just split at the first '|' */
    
    /* --- First part (before |) --- */
    int len1 = (int)(pipe_pos - cmdline);
    if (len1 > cmd1_sz - 1) len1 = cmd1_sz - 1;
    
    /* Extract first command name */
    int i;
    for (i = 0; i < len1 && cmdline[i] != ' ' && cmdline[i] != '\t'; i++) {
        if (i < cmd1_sz - 1) cmd1[i] = cmdline[i];
    }
    cmd1[i] = '\0';

    /* Extract first command args */
    int arg_start = i;
    while (arg_start < len1 && (cmdline[arg_start] == ' ' || cmdline[arg_start] == '\t'))
        arg_start++;
    
    int j = 0;
    for (i = arg_start; i < len1 && j < args1_sz - 1; i++) {
        args1[j++] = cmdline[i];
    }
    args1[j] = '\0';

    /* --- Second part (after |) --- */
    const char *after_pipe = pipe_pos + 1;
    while (*after_pipe == ' ' || *after_pipe == '\t') after_pipe++;

    /* Extract second command name */
    for (i = 0; after_pipe[i] && after_pipe[i] != ' ' && after_pipe[i] != '\t' && i < cmd2_sz - 1; i++) {
        cmd2[i] = after_pipe[i];
    }
    cmd2[i] = '\0';

    /* Extract second command args */
    arg_start = i;
    while (after_pipe[arg_start] == ' ' || after_pipe[arg_start] == '\t') arg_start++;
    
    j = 0;
    for (i = arg_start; after_pipe[i] && j < args2_sz - 1; i++) {
        args2[j++] = after_pipe[i];
    }
    args2[j] = '\0';

    return 1;
}

int pipe_start_capture(const char *cmdline) {
    char cmd1[32], args1[256], cmd2[64], args2[256];

    if (!pipe_parse(cmdline, cmd1, sizeof(cmd1), args1, sizeof(args1),
                    cmd2, sizeof(cmd2), args2, sizeof(args2))) {
        /* No pipe in this command */
        pipe_init();
        return 0;
    }

    /* Set up pipe state */
    pipe_state.active   = 1;
    pipe_state.length   = 0;
    pipe_state.read_pos = 0;

    /* Determine which command to execute first and second */
    /* Store second command info */
    p_strcpy(pipe_state.second_cmd, cmd2);
    p_strcpy(pipe_state.second_args, args2);

    return 1;
}

void pipe_write(const char *data, int len) {
    if (!pipe_state.active) return;

    int space = (int)sizeof(pipe_state.buffer) - pipe_state.length - 1;
    if (len > space) len = space;
    if (len <= 0) return;

    for (int i = 0; i < len; i++) {
        pipe_state.buffer[pipe_state.length++] = data[i];
    }
    pipe_state.buffer[pipe_state.length] = '\0';
}

void pipe_end_capture(void) {
    if (!pipe_state.active) return;

    pipe_state.active = 0;  /* Deactivate so output goes to screen again */

    /* Find and execute the second command */
    extern command_t command_table[];
    extern int command_count;
    extern int str_match(const char *s1, const char *s2);

    /* Feed pipe buffer as stdin (for now, just print pipe buffer content
     * and let the cat command show it by passing as argument) */

    /* Special case: if second command is "cat", just print the buffer */
    if (p_strncmp(pipe_state.second_cmd, "cat", 3) == 0) {
        pipe_state.buffer[pipe_state.length] = '\0';
        print_str(pipe_state.buffer);
        if (pipe_state.length == 0 || pipe_state.buffer[pipe_state.length-1] != '\n')
            putchar('\n');
    } else {
        /* Try to execute second command */
        int found = 0;
        for (int k = 0; k < command_count; k++) {
            if (str_match(pipe_state.second_cmd, command_table[k].name)) {
                command_table[k].func(pipe_state.second_args);
                found = 1;
                break;
            }
        }
        if (!found) {
            print_str_color("pipe: command not found: ", VGA_ATTR(COL_LRED, COL_BLACK));
            print_str(pipe_state.second_cmd);
            putchar('\n');
        }
    }

    pipe_init();  /* Reset for next use */
}

int pipe_has_data(void) {
    return (pipe_state.active && pipe_state.read_pos < pipe_state.length);
}

int pipe_read_char(void) {
    if (!pipe_has_data()) return -1;
    return (unsigned char)pipe_state.buffer[pipe_state.read_pos++];
}

int pipe_is_active(void) {
    return pipe_state.active;
}
