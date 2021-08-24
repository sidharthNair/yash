#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

void tokenize(char* input, char** tokens) {
    *tokens = strtok(input, " ");
    while (*tokens != NULL) {
        tokens++;
        *tokens = strtok(NULL, " ");
    }
}

int main(void) {
    while (1) {
        printf("# ");
        char input[2000];
        fgets(input, 2000, stdin); // get input from user
        input[strcspn(input, "\n")] = '\0'; // remove trailing newline
        char** argv;
        tokenize(input, argv);
        int pid = fork();
        if (pid == 0) {
            execvp(*argv, argv);
        }
        else {
            wait(NULL);
        }
    }
    return 0;
}