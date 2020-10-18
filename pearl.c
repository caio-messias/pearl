#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXLINE 1024
#define MAXARGS 64
#define MAXCMDS 64
#define MAXFNAME 500

char prompt[] = "pearl> ";

struct command {
    int argc;               /* number of arguments in this command */
    char *argv[MAXARGS];    /* contains the command name and args */
    int srcfd;              /* source file descriptor, initially STDIN_FILENO */
    char srcfile[MAXFNAME]; /* source file, used with < in a command */
    int dstfd;              /* destination fd, initially STDOUT_FILENO */
    char dstfile[MAXFNAME]; /* destination file, used with > or >> */
    bool append;            /* true if >> is present in the command */
};

void init_cmd(struct command *cmd)
{
    cmd->argc = 0;
    cmd->srcfd = STDIN_FILENO;
    cmd->dstfd = STDOUT_FILENO;
    strcpy(cmd->srcfile, "");
    strcpy(cmd->dstfile, "");
    cmd->append = false;
}

static int parse(char *cmdline, struct command cmds[]);
static int run_command(struct command *cmd, int closepipe);
static void run_commands(struct command cmds[], int ncmds);
static void redirect(struct command *cmd);
static void try_close(int fd, int fd2);

int main()
{
    char cmdline[MAXLINE];
    struct command cmds[MAXCMDS];

    // shell loop: read line -> parse args -> run commands
    while (1) {
        printf("%s", prompt);

        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
            perror("fgets error");
            exit(-1);
        }

        if (feof(stdin)) {
            exit(0);
        }

        cmdline[strlen(cmdline) - 1] = '\0';

        int nparsed = parse(cmdline, cmds);
        if (nparsed == -1) {
            printf("failed parsing command %s\n", cmdline);
            continue;
        }

        run_commands(cmds, nparsed);
    }
}

static int parse(char *cmdline, struct command cmds[])
{
    struct command *cmd = &cmds[0];
    init_cmd(cmd);
    char *saveptr = NULL;
    char *token = NULL;
    int ncmds = 0;

    for (cmd->argc = 0, token = strtok_r(cmdline, " ", &saveptr);
         cmd->argc < MAXARGS && token != NULL;
         token = strtok_r(NULL, " ", &saveptr)) {

        if (strcmp(token, "<") == 0) {
            token = strtok_r(NULL, " ", &saveptr);
            if (token == NULL)
                return -1;

            strcpy(cmd->srcfile, token);
            cmd->srcfd = -1;
            continue;
        }

        if (strcmp(token, ">") == 0) {
            token = strtok_r(NULL, " ", &saveptr);
            if (token == NULL)
                return -1;

            strcpy(cmd->dstfile, token);
            cmd->dstfd = -1;
            continue;
        }

        if (strcmp(token, ">>") == 0) {
            token = strtok_r(NULL, " ", &saveptr);
            if (token == NULL)
                return -1;

            strcpy(cmd->dstfile, token);
            cmd->dstfd = -1;
            cmd->append = true;
            continue;
        }

        if (strcmp(token, "|") == 0) {
            cmd->argv[cmd->argc] = NULL;
            cmd = &cmds[++ncmds];
            init_cmd(cmd);
            continue;
        }

        // got arg
        cmd->argv[cmd->argc++] = token;
    }

    // execvp requires the arguments to be NULL terminated.
    cmd->argv[cmd->argc] = NULL;
    return ncmds + 1;
}

static int run_command(struct command *cmd, int closepipe)
{
    if (cmd->argv[0] == NULL) {
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (closepipe != -1) {
            close(closepipe);
        }

        redirect(cmd);
        execvp(cmd->argv[0], cmd->argv);
    }

    return pid;
}

static void run_commands(struct command cmds[], int ncmds)
{
    pid_t pid;
    int in = STDIN_FILENO;
    int fd[2];

    for (int i = 0; i < ncmds - 1; ++i) {
        pipe(fd);
        cmds[i].srcfd = in;
        cmds[i].dstfd = fd[1];

        run_command(&cmds[i], fd[0]);
        try_close(fd[1], STDOUT_FILENO);
        try_close(in, STDIN_FILENO);
        in = fd[0]; /* the next command reads from here */
    }

    cmds[ncmds - 1].srcfd = in;
    cmds[ncmds - 1].dstfd = STDOUT_FILENO;
    pid = run_command(&cmds[ncmds - 1], -1);
    try_close(in, STDIN_FILENO);

    if (pid != 0) {
        waitpid(pid, NULL, 0);
    }
}

static void redirect(struct command *cmd)
{
    if (strcmp(cmd->srcfile, "") != 0) {
        cmd->srcfd = open(cmd->srcfile, O_RDONLY, 0);
    }

    dup2(cmd->srcfd, STDIN_FILENO);
    try_close(cmd->srcfd, STDIN_FILENO);

    if (strcmp(cmd->dstfile, "") != 0) {
        int flags = O_WRONLY | O_CREAT;
        if (cmd->append) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }

        int file_perm = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        cmd->dstfd = open(cmd->dstfile, flags, file_perm);
    }

    dup2(cmd->dstfd, STDOUT_FILENO);
    try_close(cmd->dstfd, STDOUT_FILENO);
}

static void try_close(int fd, int fd2)
{
    if (fd != fd2) {
        close(fd);
    }
}
