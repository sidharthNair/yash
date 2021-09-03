#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

char *special_identifiers[6] = {NULL, ">", "<", "2>", "|", "&"};

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

int main(void)
{
    while (1)
    {
        printf("# ");
        char input[2048];
        fgets(input, 2048, stdin);          // get input from user
        input[strcspn(input, "\n")] = '\0'; // remove trailing newline
        char *argv[64];
        int length = tokenize(input, argv);
        int count = 0;
        int special[64] = {0};
        int pipe_flag = 0;
        int pipe_index = 0;
        for (int i = 0; i < length; i++)
        {
            for (int j = 1; j < 6; j++)
            {
                if (!strcmp(argv[i], special_identifiers[j]))
                {
                    count++;
                    special[i] = j;
                    if (j == 4)
                        pipe_flag = 1;
                        pipe_index = i;
                        argv[i] = '\0';
                    break;
                }
            }
            //printf("%d", special[i]);
        }
        //printf("\n%d\n", count);
        int pipefd[2];
        for (int i = 0; i < length; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n%d %d\n", pipe_flag, pipe_index);
        if (pipe_flag)
        {
            pipe(pipefd);
            int ch1 = fork();
            if (ch1 == 0) {
                close(pipefd[0]);
                dup2(pipefd[1],1);
                printf("left: %s\n" , *argv);
                execvp(*argv, argv);
            }
            int ch2 = fork();
            if (ch2 == 0) {
                close(pipefd[1]);
                dup2(pipefd[0],0);
                printf("right: %s\n" , *(argv + (pipe_index+1)));
                execvp(*(argv + (pipe_index+1)), (argv + (pipe_index+1)));
            }
            else {
                wait((int *)NULL);
            }
        }
        else
        {
            int pid = fork();
            if (pid == 0)
            {
                execvp(*argv, argv);
            }
            else
            {
                signal(SIGINT, SIG_IGN);
                wait((int *)NULL);
                signal(SIGINT, SIG_DFL);
            }
        }
    }
    return 0;
}