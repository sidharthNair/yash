#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

char* special_identifiers[6] = {NULL, ">", "<", "2>", "|", "&"};

int tokenize(char* input, char** tokens) {
    int length = 0;
    *tokens = strtok(input, " ");
    while (*tokens != NULL) {
        tokens++;
        *tokens = strtok(NULL, " ");
        length++;
    }
    return length;
}

int main(void) {
    while (1) {
        printf("# ");
        char input[2000];
        fgets(input, 2000, stdin); // get input from user
        input[strcspn(input, "\n")] = '\0'; // remove trailing newline
        char* argv[64];
        int length = tokenize(input, argv);
        int count = 0;
        int special[64] = {0};
        for (int i = 0; i < length; i++) {
            for (int j = 1; j < 6; j++) {
                if (!strcmp(argv[i], special_identifiers[j])) {
                    count++;
                    special[i] = j;
                    break;
                }
            }
            //printf("%d", special[i]);
        }
        //printf("\n%d\n", count);
        int pid = fork();
        if (pid == 0) {
            execvp(*argv, argv);
        }
        else {
            signal(SIGINT, SIG_IGN);
            wait((int *)NULL);
            signal(SIGINT, SIG_DFL);
        }
    }
    return 0;
}