#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum status {
    RUNNING,
    STOPPED,
    TERMINATED
};

struct job {
    char *jstr;
    int status;
    int background;
    int job_num;
    pid_t pgid;
    struct job *next;
    int pipe;
} typedef job_t;

job_t *shell_job;

int tokenize(char *input, char **tokens) {
    int length = 0;
    *tokens = strtok(input, " ");
    while (*tokens) {
        tokens++;
        *tokens = strtok(NULL, " ");
        length++;
    }
    return length;
}

void redirect(char **argv) {
    char *token = *argv;
    while (token) {
        if (!strcmp(token, "<")) {
            *(argv++) = NULL;
            int new_stdin = open(*(argv++), O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            if (new_stdin == -1) {
                perror("Input file not found");
                exit(1);
            }
            dup2(new_stdin, 0);
            close(new_stdin);
        } else if (!strcmp(token, ">")) {
            *(argv++) = NULL;
            int new_stdout = open(*(argv++), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            dup2(new_stdout, 1);
            close(new_stdout);
        } else if (!strcmp(token, "2>")) {
            *(argv++) = NULL;
            int new_stderr = open(*(argv++), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            dup2(new_stderr, 2);
            close(new_stderr);
        } else {
            argv++;
        }
        token = *argv;
    }
}

void execute_reg(char **argv, job_t *j) {
    int pid = fork();
    if (pid == 0) {
        redirect(argv);
        execvp(*argv, argv);
        exit(0);
    } else {
        wait((int *)NULL);
    }
}

void execute_pipe(char **argv, job_t *j) {
    int pipefd[2];
    pipe(pipefd);
    int child1 = fork();
    if (child1 == 0) {
        dup2(pipefd[1], 1);
        close(pipefd[0]);
        redirect(argv);
        execvp(*argv, argv);
        exit(0);
    }
    int child2 = fork();
    if (child2 == 0) {
        dup2(pipefd[0], 0);
        close(pipefd[1]);
        redirect(argv + (j->pipe + 1));
        execvp(*(argv + (j->pipe + 1)), (argv + (j->pipe + 1)));
        exit(0);
    } else {
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(child1, NULL, 0);
        waitpid(child2, NULL, 0);
    }
}

void update_status(job_t *j, int blocking) {
    int status;
    if (waitpid(j->pgid, &status, blocking ? WUNTRACED : WUNTRACED | WNOHANG)) {
        if (WIFEXITED(status) || (WIFSIGNALED(status) && !WIFSTOPPED(status))) {
            j->status = TERMINATED;
        } else if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP) {
            j->status = STOPPED;
        }
    }
}
void print_linked_list();

void start_job(char *input) {
    job_t *j = malloc(sizeof(job_t));
    job_t *current = shell_job;
    int job_num = 1;
    while (current->next) {
        current = current->next;
        if (!(current->status == TERMINATED && !current->background))
            job_num = current->job_num + 1;
    }
    current->next = j;
    j->jstr = strdup(input);
    j->job_num = job_num;
    j->next = NULL;
    j->pipe = 0;
    j->background = 0;
    char *argv[64];
    int length = tokenize(input, argv);
    if (!strcmp(argv[length - 1], "&")) {
        argv[length-- - 1] = NULL;
        j->background = 1;
    } else {
        argv[length] = NULL;
    }
    for (int i = 0; i < length; i++) {
        if (!strcmp(argv[i], "|")) {
            argv[i] = NULL;
            j->pipe = i;
        }
    }
    int pgid = fork();
    if (pgid == 0) {
        setpgid(0, 0);

        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        if (j->pipe) {
            execute_pipe(argv, j);
        } else {
            execute_reg(argv, j);
        }
        exit(0);
    } else {
        j->pgid = pgid;
        j->status = RUNNING;
        if (!j->background) {
            tcsetpgrp(STDIN_FILENO, pgid);
            update_status(j, 1);
            tcsetpgrp(STDIN_FILENO, shell_job->pgid);
        }
    }
}

void print_linked_list() {
    job_t *current = shell_job;
    printf("\n\n");
    while (current) {
        printf("%d - %s %d\n", current->job_num, current->jstr, current->status);
        current = current->next;
    }
    printf("null\n\n");
}

void update_jobs() {
    job_t *previous = shell_job;
    job_t *j = shell_job->next;
    while (j) {
        if (j->status == TERMINATED) {
            previous->next = j->next;
            free(j->jstr);
            free(j);
        } else if (j->status == RUNNING) {
            update_status(j, 0);
            previous = previous->next;
        } else {
            previous = previous->next;
        }
        j = previous->next;
    }
}

void fg() {
    job_t *j = shell_job->next;
    job_t *recent_fg_job;
    while (j) {
        if (j->status == STOPPED || j->background) {
            recent_fg_job = j;
        }
        j = j->next;
    }
    if (recent_fg_job == NULL)
        return;
    if (recent_fg_job->jstr[strlen(recent_fg_job->jstr) - 1] == '&') {
        char *job_string = strdup(recent_fg_job->jstr);
        job_string[strlen(job_string) - 1] = '\0';
        printf("%s\n", job_string);
        free(job_string);
    } else {
        printf("%s\n", recent_fg_job->jstr);
    }
    recent_fg_job->status = RUNNING;
    recent_fg_job->background = 0;
    kill(-recent_fg_job->pgid, SIGCONT);
    tcsetpgrp(STDIN_FILENO, recent_fg_job->pgid);
    update_status(recent_fg_job, 1);
    tcsetpgrp(STDIN_FILENO, shell_job->pgid);
}

void bg() {
    job_t *j = shell_job->next;
    job_t *recent_bg_job;
    job_t *recent_fg_job;
    while (j) {
        if (j->status == STOPPED || j->background) {
            recent_fg_job = j;
        }
        if (j->status == STOPPED) {
            recent_bg_job = j;
        }
        j = j->next;
    }
    if (recent_bg_job == NULL)
        return;
    if (recent_bg_job->jstr[strlen(recent_bg_job->jstr) - 1] == '&') {
        printf("[%d] %c %s\n", recent_bg_job->job_num, recent_bg_job == recent_fg_job ? '+' : '-', recent_bg_job->jstr);
    } else {
        printf("[%d] %c %s &\n", recent_bg_job->job_num, recent_bg_job == recent_fg_job ? '+' : '-', recent_bg_job->jstr);
    }
    recent_bg_job->status = RUNNING;
    recent_bg_job->background = 1;
    kill(-recent_bg_job->pgid, SIGCONT);
}

void print_jobs(int done_flag) {
    job_t *j = shell_job->next;
    job_t *recent_fg_job;
    while (j) {
        if (j->status == STOPPED || j->background) {
            recent_fg_job = j;
        }
        j = j->next;
    }
    j = shell_job->next;
    int i = 1;
    while (j) {
        if (done_flag) {
            if (j->status == TERMINATED && j->background)
                printf("[%d] %c Done\t%s\n", j->job_num, j == recent_fg_job ? '+' : '-', j->jstr);
        } else {
            if (j->status != TERMINATED)
                printf("[%d] %c %s\t%s\n", j->job_num, j == recent_fg_job ? '+' : '-', j->status == RUNNING ? "Running" : "Stopped", j->jstr);
        }
        j = j->next;
    }
}

int main(void) {
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    pid_t s_pid = getpid();
    setpgid(0, 0);
    shell_job = malloc(sizeof(job_t));
    shell_job->status = RUNNING;
    shell_job->pgid = s_pid;
    shell_job->jstr = "shell";
    while (1) {
        write(1, "# ", 2);
        char input[2048];
        if (fgets(input, 2048, stdin) == NULL)
            exit(1);
        update_jobs();
        print_jobs(1);
        input[strcspn(input, "\n")] = '\0';
        if (!strcmp(input, "jobs")) {
            print_jobs(0);
        } else if (!strcmp(input, "fg")) {
            fg();
        } else if (!strcmp(input, "bg")) {
            bg();
        } else {
            start_job(input);
        }
    }
    return 0;
}