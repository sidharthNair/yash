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
    int background;
    pid_t pgid;
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
            int new_stdin = open(*(argv++), O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            dup2(new_stdin, 0);
            close(new_stdin);
        }
        else if (!strcmp(token, ">"))
        {
            *(argv++) = NULL;
            int new_stdout = open(*(argv++), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
            dup2(new_stdout, 1);
            close(new_stdout);
        }
        else if (!strcmp(token, "2>"))
        {
            *(argv++) = NULL;
            int new_stderr = open(*(argv++), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
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
        wait((int *)NULL);
    }
}

void execute_pipe(char **argv, job_t *j)
{
    int pipefd[2];
    pipe(pipefd);
    int child1 = fork();
    if (child1 == 0)
    {
        dup2(pipefd[1], 1);
        close(pipefd[0]);
        redirect(argv);
        execvp(*argv, argv);
    }
    int child2 = fork();
    if (child2 == 0)
    {
        dup2(pipefd[0], 0);
        close(pipefd[1]);
        redirect(argv + (j->pipe + 1));
        execvp(*(argv + (j->pipe + 1)), (argv + (j->pipe + 1)));
    }
    else
    {
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(child1, NULL, 0);
        waitpid(child2, NULL, 0);
    }
}

void update_status(job_t *j, int blocking)
{
    int status;
    waitpid(j->pgid, &status, blocking ? WUNTRACED : WUNTRACED | WNOHANG);
    if (WIFEXITED(status) || (WIFSIGNALED(status) && !WIFSTOPPED(status)))
    {
        printf("%d\n", WEXITSTATUS(status));
        j->status = TERMINATED;
    }
    else if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP)
    {
        j->status = STOPPED;
    }
}

void start_job(char *input)
{
    job_t *j = malloc(sizeof(job_t));
    job_t *next = shell_job;
    while (next->next)
    {
        next = next->next;
    }
    next->next = j;
    j->jstr = strdup(input);
    char *argv[64];
    int length = tokenize(input, argv);
    if (!strcmp(argv[length - 1], "&"))
    {
        argv[length-- - 1] = NULL;
        j->background = 1;
    }
    else
    {
        argv[length] = NULL;
    }
    for (int i = 0; i < length; i++)
    {
        if (!strcmp(argv[i], "|"))
        {
            argv[i] = NULL;
            j->pipe = i;
        }
    }
    int pgid = fork();
    if (pgid == 0)
    {
        setpgid(0, 0);

        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        if (j->pipe)
        {
            execute_pipe(argv, j);
        }
        else
        {
            execute_reg(argv, j);
        }
        exit(0);
    }
    else
    {
        j->pgid = pgid;
        j->status = RUNNING;
        if (!j->background)
        {
            tcsetpgrp(STDIN_FILENO, pgid);
            update_status(j, 1);
            tcsetpgrp(STDIN_FILENO, shell_job->pgid);
        }
    }
}

void update_jobs()
{
    job_t *current = shell_job->next;
    while (current != NULL)
    {
        if (current->status == RUNNING)
        {
            update_status(current, 0);
        }
        current = current->next;
    }
}

void fg()
{
    job_t *current = shell_job->next;
    job_t *recent_fg_job;
    while (current != NULL) {
        if (current->status == STOPPED || current->background) {
            recent_fg_job = current;
        }
        current = current->next;
    }
    if (recent_fg_job == NULL) return;
    kill(-recent_fg_job->pgid, SIGCONT);
    tcsetpgrp(STDIN_FILENO, recent_fg_job->pgid);
    update_status(recent_fg_job, 1);
    tcsetpgrp(STDIN_FILENO, shell_job->pgid);
}

void bg()
{
    job_t *current = shell_job->next;
    job_t *recent_bg_job;
    while (current != NULL) {
        if (current->status == STOPPED) {
            recent_bg_job = current;
        }
        current = current->next;
    }
    if (recent_bg_job == NULL) return;
    kill(-recent_bg_job->pgid, SIGCONT);
}

void print_jobs()
{
    job_t *current = shell_job->next;
    int i = 1;
    while (current)
    {
        char *status;
        switch (current->status)
        {
        case RUNNING:
            status = "Running";
            break;
        case STOPPED:
            status = "Stopped";
            break;
        case TERMINATED:
            status = "Done";
        }
        printf("[%d] - %s\t%s\n", i++, status, current->jstr);
        current = current->next;
    }
}

int main(void)
{
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
    while (1)
    {
        write(1, "# ", 2);
        char input[2048];
        if (fgets(input, 2048, stdin) == NULL)
            exit(1);
        update_jobs();
        input[strcspn(input, "\n")] = '\0';
        if (!strcmp(input, "jobs"))
        {
            print_jobs();
        }
        else if (!strcmp(input, "fg"))
        {
            fg();
        }
        else if (!strcmp(input, "bg"))
        {
            bg();
        }
        else
        {
            start_job(input);
        }
    }
    return 0;
}