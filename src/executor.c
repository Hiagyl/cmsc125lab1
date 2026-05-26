#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include "shell.h"

static ShellContext *global_ctx = NULL;

void sigchld_handler() {
    if (!global_ctx) return;
    
    int saved_errno = errno; // Preserve errno to keep the handler safe
    int status;
    
    // Asynchronously reap any background job that finished immediately
    for (int i = 0; i < global_ctx->job_count; i++) {
        // WNOHANG ensures we don't block if a job is still actively running
        pid_t pid = waitpid(global_ctx->jobs[i].pid, &status, WNOHANG);
        if (pid > 0) {
            printf("\n[%d] Finished background job: %s (PID: %d)\nmysh> ", 
                   global_ctx->jobs[i].job_id, 
                   global_ctx->jobs[i].command_name, 
                   global_ctx->jobs[i].pid);
            fflush(stdout);

            // Shift array down to maintain order
            for (int j = i; j < global_ctx->job_count - 1; j++) {
                global_ctx->jobs[j] = global_ctx->jobs[j + 1];
            }
            global_ctx->job_count--;
            i--; // Adjust index since we shifted the array
        }
    }
    errno = saved_errno;
}

void setup_signal_handlers() {
    struct sigaction sa_chld, sa_int, sa_trm;

    // Configure SIGCHLD handler to reap background processes instantly
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    sigaction(SIGCHLD, &sa_chld, NULL);

    // Configure SIGINT (Ctrl+C) to be IGNORED by the parent shell wrapper
    sa_int.sa_handler = SIG_IGN; 
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    // Ignore SIGTTOU and SIGTTIN so tcsetpgrp() doesn't stop the shell
    sa_trm.sa_handler = SIG_IGN;
    sigemptyset(&sa_trm.sa_mask);
    sa_trm.sa_flags = SA_RESTART;
    sigaction(SIGTTOU, &sa_trm, NULL);
    sigaction(SIGTTIN, &sa_trm, NULL);
}

int command_exists(char *cmd) {
    if (strchr(cmd, '/') != NULL) {
        return access(cmd, X_OK) == 0;
    }

    char *path = getenv("PATH");
    if (!path) return 0;

    char *path_copy = strdup(path);
    char *dir = strtok(path_copy, ":");
    char full_path[1024];

    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return 1; // Found it!
        }
        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return 0; // Not found anywhere
}

void apply_redirection(Command *cmd) {
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { perror("open input"); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (cmd->output_file) {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->output_file, flags, 0644);
        if (fd < 0) { perror("open output"); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

void executor(ShellContext *ctx, Command *cmd) {
    if (cmd->argv[0] == NULL) return;

    global_ctx = ctx;
    if (handle_builtins(ctx, cmd)) return;

    if (!command_exists(cmd->argv[0])) {
        fprintf(stderr, "mysh: command not found: %s\n", cmd->argv[0]);
        return; // EXIT EARLY - Do not fork, do not print "Started"
    }
    
    pid_t pid = fork();
    if (pid == 0) {

        setpgid(0, 0);
        // This ensures Ctrl+C actually kills the foreground application
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);

        apply_redirection(cmd);
        if (execvp(cmd->argv[0], cmd->argv) == -1) {
            if (errno == ENOENT) {
                fprintf(stderr, "mysh: command not found: %s\n", cmd->argv[0]);
            } else if (errno == EACCES) {
                fprintf(stderr, "mysh: permission denied: %s\n", cmd->argv[0]);
            } else {
                perror("mysh");
            }
            exit(127);
        }
        exit(EXIT_FAILURE);
    } 
    else if (pid > 0) {

        setpgid(pid, pid);

        if (cmd->background) {
            if (ctx->job_count < MAX_BG_JOBS) {
                BackgroundJob *j = &ctx->jobs[ctx->job_count];
                j->pid = pid;
                j->job_id = ctx->next_job_id++;
                strncpy(j->command_name, cmd->argv[0], 255);
               printf("[%d] Started background job: %s (PID: %d)\n", 
                        j->job_id, cmd->argv[0], pid);
                ctx->job_count++;
            }
        } else {
            tcsetpgrp(STDIN_FILENO, pid);
            waitpid(pid, NULL, 0);
            tcsetpgrp(STDIN_FILENO, getpgrp());
        }
    } else {
        perror("fork");
    }
}

void reap_background_jobs(ShellContext *ctx) {
    int status;
    for (int i = 0; i < ctx->job_count; i++) {
        pid_t pid = waitpid(ctx->jobs[i].pid, &status, WNOHANG);
        if (pid > 0) {
            printf("[%d] Finished background job: %s (PID: %d)\n", 
                    ctx->jobs[i].job_id, 
                    ctx->jobs[i].command_name, 
                    ctx->jobs[i].pid);
            for (int j = i; j < ctx->job_count - 1; j++) {
                ctx->jobs[j] = ctx->jobs[j + 1];
            }
            ctx->job_count--;
            i--;
        }
    }
}