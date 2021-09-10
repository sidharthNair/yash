#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

enum status
{
    RUNNING,
    STOPPED,
    TERMINATED
};

struct job
{
    char *jstr;
    int status;
    pid_t pgid;
    pid_t lpid;
    pid_t rpid;
    struct job *head;
    struct job *next;
    int pipe;
} typedef job_t;

job_t *shell_job;

int tokenize(char *input, char **tokens)
{
    int length = 0;
    *tokens = strtok(input, " ");
    while (*tokens != NULL)
    {
        tokens++;
        *tokens = strtok(NULL, " ");
        length++;
    }
    return length;
}

void redirect(char **argv)
{
    char *token = *argv;
    while (token != NULL)
    {
        if (!strcmp(token, "<"))
        {
            *(argv++) = NULL;
            int new_stdin = open(*(argv++), O_RDONLY | O_CREAT, S_IRWXU);
            dup2(new_stdin, 0);
            close(new_stdin);
        }
        else if (!strcmp(token, ">"))
        {
            *(argv++) = NULL;
            int new_stdout = open(*(argv++), O_WRONLY | O_CREAT, S_IRWXU);
            dup2(new_stdout, 1);
            close(new_stdout);
        }
        else if (!strcmp(token, "2>"))
        {
            *(argv++) = NULL;
            int new_stderr = open(*(argv++), O_WRONLY | O_CREAT, S_IRWXU);
            dup2(new_stderr, 2);
            close(new_stderr);
        }
        else
        {
            argv++;
        }
        token = *argv;
    }
}

void execute_reg(char **argv, job_t *j)
{
    int pid = fork();
    if (pid == 0)
    {
        redirect(argv);
        execvp(*argv, argv);
        exit(0);
    }
    else
    {
        j->lpid = pid;
        wait((int *)NULL);
        tcsetpgrp(shell_job->pgid, 1);
    }
}

void execute_pipe(char **argv, job_t *j)
{
    int pipefd[2];
    pipe(pipefd);
    int child1 = fork();
    if (child1 == 0)
    {
        redirect(argv);
        dup2(pipefd[1], 1);
        close(pipefd[0]);
        execvp(*argv, argv);
    }
    j->lpid = child1;
    int child2 = fork();
    if (child2 == 0)
    {
        redirect(argv + (j->pipe + 1));
        dup2(pipefd[0], 0);
        close(pipefd[1]);
        execvp(*(argv + (j->pipe + 1)), (argv + (j->pipe + 1)));
    }
    else
    {
        j->rpid = child2;
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(child1, NULL, 0);
        waitpid(child2, NULL, 0);
        j->status = TERMINATED;
        tcsetpgrp(shell_job->pgid, 1);
    }
}

void create_job(char *input)
{
    job_t *j = malloc(sizeof(job_t));
    job_t *next = shell_job;
    while (next->next != NULL)
    {
        next = next->next;
    }
    next->next = j;
    int pgid = fork();
    if (pgid == 0)
    {
        setpgid(0, 0);
        j->jstr = strdup(input);
        j->pgid = getpid();
        j->status = RUNNING;
        char *argv[64];
        int length = tokenize(input, argv);
        argv[length] = NULL;
        j->pipe = 0;
        for (int i = 0; i < length; i++)
        {
            if (!strcmp(argv[i], "|"))
            {
                argv[i] = NULL;
                j->pipe = i;
            }
        }
        if (j->pipe)
        {
            execute_pipe(argv, j);
        }
        else
        {
            execute_reg(argv, j);
        }
    }
    else
    {
        tcsetpgrp(pgid, 1);
    }
}

void print_jobs()
{
    job_t *current = shell_job->next;
    int i = 1;
    while (current != NULL)
    {
        printf("[%d] - %s\t%s\n", i++, current->status == RUNNING ? "Running" : "Stopped", current->jstr);
    }
}

int main(void)
{
    signal(SIGTTOU, SIG_IGN);
    pid_t s_pid = getpid();
    shell_job = malloc(sizeof(job_t));
    shell_job->status = RUNNING;
    shell_job->pgid = s_pid;
    while (1)
    {
        write(1, "# ", 2);
        char input[2048];
        fgets(input, 2048, stdin);
        input[strcspn(input, "\n")] = '\0';
        if (!strcmp(input, "jobs"))
        {
            print_jobs();
        }
        else
        {
            create_job(input);
        }
    }
    return 0;
}