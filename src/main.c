#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "shell.h"

ShellContext* context_create() {
    ShellContext *ctx = malloc(sizeof(ShellContext));
    if (ctx) {
        ctx->is_running = true;
        ctx->job_count = 0;
        ctx->next_job_id = 1;
    }
    return ctx;
}

void context_free(ShellContext *ctx) {
    if (!ctx) return;

    // Free all command strings stored in history
    for (int i = 0; i < ctx->history_count; i++) {
        if (ctx->history[i]) {
            free(ctx->history[i]);
        }
    }

    // Handle orphan background jobs 
    cleanup_orphans(ctx);

    // Finally, free the context struct itself
    free(ctx);
}

void cleanup_orphans(ShellContext *ctx) {
    for (int i = 0; i < ctx->job_count; i++) {
        // Send SIGTERM to tell the process to shut down
        kill(ctx->jobs[i].pid, SIGTERM);
        
        // Use waitpid to "collect" the process so it doesn't become a zombie
        waitpid(ctx->jobs[i].pid, NULL, 0);
    }
}

void add_to_history(ShellContext *ctx, const char *line) {
    if (!line || strlen(line) == 0) return;

    // If history is full, free the oldest command (index 0)
    if (ctx->history_count == MAX_HISTORY) {
        free(ctx->history[0]);
        for (int i = 1; i < MAX_HISTORY; i++) {
            ctx->history[i - 1] = ctx->history[i];
        }
        ctx->history_count--;
    }

    ctx->history[ctx->history_count++] = strdup(line);
}

int main() {
    ShellContext *ctx = context_create();
    if (!ctx) return 1;

    setup_signal_handlers();

    char line[1024];

    while (ctx->is_running) {
        // reap_background_jobs(ctx);
        printf("mysh> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) break;

        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue; 
        
        add_to_history(ctx, line);
        Command *cmd = parse_command(line); 

        if (cmd) {
            executor(ctx, cmd);
            free_command(cmd); // Clean up the struct after execution
        }
    }

    context_free(ctx);
    return 0;
}