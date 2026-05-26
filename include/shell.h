#ifndef SHELL_H
#define SHELL_H

#include <stdbool.h>
#include <sys/types.h>

#define MAX_ARGS 100
#define MAX_BG_JOBS 100
#define MAX_HISTORY 10




/* --- 1. Data Structures --- */

typedef struct {
    char *argv[MAX_ARGS];   // Command and arguments
    char *input_file;       // Filename for <
    char *output_file;      // Filename for > or >>
    int append;             // 0 for >, 1 for >>
    int background;         // 1 if & is present
} Command;

typedef struct {
    pid_t pid;
    int job_id;
    char command_name[256];
} BackgroundJob;


typedef struct {
    bool is_running;
    BackgroundJob jobs[MAX_BG_JOBS];
    int job_count;
    int next_job_id;
    
    char *history[MAX_HISTORY];
    int history_count;
} ShellContext;


/* --- 2. Core Functions --- */

ShellContext* context_create();
void context_free(ShellContext *ctx);
void cleanup_orphans(ShellContext *ctx);
void shell_loop(ShellContext *ctx);

/* --- 3. Parsing (parser.c) --- */

Command* parse_command(char *line);
void free_command(Command *cmd);

/* --- 4. Execution (executor.c) --- */

void executor(ShellContext *ctx, Command *cmd);
void reap_background_jobs(ShellContext *ctx);

/* --- 5. Built-ins (builtins.c) --- */

int handle_builtins(ShellContext *ctx, Command *cmd);
void builtin_cd(Command *cmd);
void builtin_pwd();
void builtin_help();
void builtin_exit(ShellContext *ctx);

/* --- 6. Command History --- */

void add_to_history(ShellContext *ctx, const char *line);
void builtin_history(ShellContext *ctx);

/* --- 7. Async Background Jobs --- */
void setup_signal_handlers();
#endif