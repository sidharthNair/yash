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

int tokenize(char *input, char **tokens);
void redirect(char **argv);
void execute_reg(char **argv, job_t *j);
void execute_pipe(char **argv, job_t *j);
void start_job(char *input);
void update_status(job_t *j, int blocking);
void update_jobs();
void fg();
void bg();
void jobs(int done_flag);
void cleanup();

// FOR DEBUGGING PURPOSES, prints jobs list
void print_linked_list() {
    job_t *current = shell_job;
    printf("\n\n");
    while (current) {
        printf("%d - %s %d\n", current->job_num, current->jstr, current->status);
        current = current->next;
    }
    printf("null\n\n");
}

// tokenizes string by spaces, returns length of tokens array
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

// searches for redirect identifiers and performs the redirection accordingly
void redirect(char **argv) {
    char *token = *argv;
    int in_counter, out_counter, err_counter = 0;
    while (token) {
        if (!strcmp(token, "<")) {
            in_counter++;
            *(argv++) = NULL;
            if (!*argv) {
                exit(1);
            }
            int new_stdin = open(*(argv++), O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            if (new_stdin == -1) {
                exit(1);
            }
            dup2(new_stdin, 0);
            close(new_stdin);
        } else if (!strcmp(token, ">")) {
            out_counter++;
            *(argv++) = NULL;
            if (!*argv) {
                exit(1);
            }
            int new_stdout = open(*(argv++), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            dup2(new_stdout, 1);
            close(new_stdout);
        } else if (!strcmp(token, "2>")) {
            err_counter++;
            *(argv++) = NULL;
            if (!*argv) {
                exit(1);
            }
            int new_stderr = open(*(argv++), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            dup2(new_stderr, 2);
            close(new_stderr);
        } else {
            argv++;
        }
        token = *argv;
    }
    if (in_counter > 1 || out_counter > 1 || err_counter > 1) {
        exit(1);
    }
}

// executes command without pipe
void execute_reg(char **argv, job_t *j) {
    int pid = fork();
    if (pid == 0) {
        redirect(argv);
        execvp(*argv, argv);
        exit(1);
    } else {
        wait((int *)NULL);
    }
}

// executes command with pipe
void execute_pipe(char **argv, job_t *j) {
    int pipefd[2];
    pipe(pipefd);
    int child1 = fork();
    if (child1 == 0) {
        dup2(pipefd[1], 1);
        close(pipefd[0]);
        redirect(argv);
        execvp(*argv, argv);
        exit(1);
    }
    int child2 = fork();
    if (child2 == 0) {
        dup2(pipefd[0], 0);
        close(pipefd[1]);
        redirect(argv + (j->pipe + 1));
        execvp(*(argv + (j->pipe + 1)), (argv + (j->pipe + 1)));
        exit(1);
    } else {
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(child1, NULL, 0);
        waitpid(child2, NULL, 0);
    }
}

// creates and starts a job with given command
void start_job(char *input) {
    job_t *j = malloc(sizeof(job_t));
    j->jstr = strdup(input);
    j->next = NULL;
    j->pipe = 0;
    j->background = 0;
    char *argv[128];
    int length = tokenize(input, argv);
    if (!strcmp(argv[length - 1], "&")) {
        argv[length-- - 1] = NULL;
        j->background = 1;
    } else {
        argv[length] = NULL;
    }
    for (int i = 0; i < length; i++) {
        if (!strcmp(argv[i], "|")) {
            if (j->pipe || i == length - 1) {  // pipe error, invalid command
                free(j->jstr);
                free(j);
                return;
            }
            argv[i] = NULL;
            j->pipe = i;
        } else if (!strcmp(argv[i], "&")) {  // & error, invalid command
            free(j->jstr);
            free(j);
            return;
        }
    }
    job_t *current = shell_job;
    int job_num = 1;
    while (current->next) {
        current = current->next;
        if (!(current->status == TERMINATED && !current->background)) {
            job_num = current->job_num + 1;
        }
    }
    current->next = j;
    j->job_num = job_num;
    int pgid = fork();
    if (pgid == 0) {
        setpgid(0, 0);

        //default signal behavior
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

// updates status of jobs, blocking flag is used if the process using it is a foreground process
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

// updates statuses of all running jobs, removes terminated jobs from job list
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

// brings most recently stopped or backgrounded process to the foreground
void fg() {
    job_t *j = shell_job->next;
    job_t *recent_fg_job = NULL;
    while (j) {
        if (j->status == STOPPED || (j->background && j->status != TERMINATED)) {
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

// starts up most recently stopped process in the background
void bg() {
    job_t *j = shell_job->next;
    job_t *recent_bg_job = NULL;
    job_t *recent_fg_job = NULL;
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
        printf("[%d]%c  %s\n", recent_bg_job->job_num, recent_bg_job == recent_fg_job ? '+' : '-', recent_bg_job->jstr);
    } else {
        printf("[%d]%c  %s &\n", recent_bg_job->job_num, recent_bg_job == recent_fg_job ? '+' : '-', recent_bg_job->jstr);
    }
    recent_bg_job->status = RUNNING;
    recent_bg_job->background = 1;
    kill(-recent_bg_job->pgid, SIGCONT);
}

// prints job list, done_flag tells us whether we should print only terminated background processes or only unterminated processes
void jobs(int done_flag) {
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
                printf("[%d]%c  Done\t\t%s\n", j->job_num, j == recent_fg_job ? '+' : '-', j->jstr);
        } else {
            if (j->status != TERMINATED)
                printf("[%d]%c  %s\t\t%s\n", j->job_num, j == recent_fg_job ? '+' : '-', j->status == RUNNING ? "Running" : "Stopped", j->jstr);
        }
        j = j->next;
    }
}

// frees all memory
void cleanup() {
    job_t *current = shell_job;
    while (current) {
        if (current->status != TERMINATED) {
            kill(-current->pgid, SIGQUIT);
        }
        job_t *next = current->next;
        free(current->jstr);
        free(current);
        current = next;
    }
}

int main(void) {
    // shell should ignore all signals
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
    shell_job->jstr = strdup("shell");
    while (1) {
        write(1, "# ", 2);
        char input[2048];
        if (fgets(input, 2048, stdin) == NULL) {
            cleanup();
            exit(0);
        }
        update_jobs();
        jobs(1);
        input[strcspn(input, "\n")] = '\0';
        if (!strcmp(input, "jobs")) {
            jobs(0);
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