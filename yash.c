#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

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

void execute(char **argv)
{
    int pid = fork();
    if (pid == 0)
    {
        execvp(*argv, argv);
        exit(0);
    }
    else
    {
        wait((int *)NULL);
    }
}

void execute_pipe(char **argv, int pipe_index)
{
    int pipefd[2];
    pipe(pipefd);
    int child1 = fork();
    if (child1 == 0)
    {
        dup2(pipefd[1], 1);
        close(pipefd[0]);
        execvp(*argv, argv);
    }
    int child2 = fork();
    if (child2 == 0)
    {
        dup2(pipefd[0], 0);
        close(pipefd[1]);
        execvp(*(argv + (pipe_index + 1)), (argv + (pipe_index + 1)));
    }
    else
    {
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(child1, NULL, 0);
        waitpid(child2, NULL, 0);
    }
}

int main(void)
{
    pid_t s_pid = getpid();
    while (1)
    {
        write(1, "# ", 2);
        char input[2048];
        char *argv[64];
        fgets(input, 2048, stdin);
        input[strcspn(input, "\n")] = '\0'; 
        int length = tokenize(input, argv);
        argv[length] = NULL;
        int pipe_index = 0;
        for (int i = 0; i < length; i++)
        {
            if (!strcmp(argv[i], "|")) {
                argv[i] = NULL;
                pipe_index = i;
            }
        }
        if (pipe_index) {
            execute_pipe(argv, pipe_index);
        }
        else {
            execute(argv);
        }
    }
    return 0;
}