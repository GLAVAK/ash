//
// Created by glavak on 12.02.17.
//

#include <stdlib.h>
#include <zconf.h>
#include <string.h>
#include <stdio.h>
#include <wait.h>
#include <fcntl.h>

#include "execute.h"
#include "shell.h"

int foreground_process(struct job * job);

int execute_command(char ** args,
                    char in_background,
                    char * infile, char * outfile, char * appfile,
                    int in_pipe, int out_pipe,
                    int in_pipe_other_end, int out_pipe_other_end,
                    int job_id)
{
    // Find free process slot in job
    int pid_index = 0;
    while (jobs[job_id].pids[pid_index] > 0)
    {
        pid_index++;
    }
    int pgid_to_set = 0;
    if (pid_index > 0) pgid_to_set = jobs[job_id].pids[0];

    // Fork process:
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("ash: forking");
        return 1;
    }

    // Here we have two processes executing this line of code
    if (pid == 0)
    {
        // Child process
        if (infile != NULL)
        {
            int fr = open(infile, O_RDONLY);
            dup2(fr, STDIN_FILENO);
        }
        if (outfile != NULL)
        {
            int access_mode =
                    S_IRUSR | S_IRGRP | S_IROTH |
                    S_IWUSR | S_IWGRP;
            int fw = open(outfile, O_WRONLY | O_CREAT, access_mode);
            dup2(fw, STDOUT_FILENO);
        }
        if (appfile != NULL)
        {
            int fw = open(appfile, O_WRONLY | O_APPEND);
            dup2(fw, STDOUT_FILENO);
        }

        if (in_pipe >= 0)
        {
            dup2(in_pipe, STDIN_FILENO);
            close(in_pipe);
            close(in_pipe_other_end);
        }
        if (out_pipe >= 0)
        {
            dup2(out_pipe, STDOUT_FILENO);
            close(out_pipe);
            close(out_pipe_other_end);
        }

        pid = getpid();
        setpgid(pid, pgid_to_set);

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        execvp(args[0], args);

        // If we're here, execvp failed
        perror("asd: executing child process");
        exit(1);
    }
    else
    {
        // Parent process
        setpgid(pid, pgid_to_set);

        if (in_pipe >= 0)
        {
            close(in_pipe);
            close(in_pipe_other_end);
        }

        jobs[job_id].pids[pid_index] = pid;
        jobs[job_id].pids[pid_index + 1] = -1;
        if (in_background)
        {
            printf("[%d] %d\n", job_id, pid);
            return 0;
        }
        else if (out_pipe < 0)
        {
            // For last process in pipe
            return foreground_process(&jobs[job_id]);
        }
    }
}

void print_jobs()
{
    for (int i = 0; i < MAXJOBS; ++i)
    {
        if (jobs[i].pids[0] <= 0) continue;

        int status;
        int ret = waitpid(jobs[i].pids[0], &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (ret == 0)
        {
            // State haven't changed
            print_job_status(&jobs[i]);
        }
        else if (ret != jobs[i].pids[0])
        {
            perror("waitpid");
        }
        else
        {
            jobs[i].status = status;
            print_job_status(&jobs[i]);
        }
    }
}

void background_process(struct job * job)
{
    kill_job(job, SIGCONT);
}

int foreground_process(struct job * job)
{
    if (WIFSTOPPED(job->status))
    {
        kill_job(job, SIGCONT);
    }

    fg_job = job;
    tcsetpgrp(shell_terminal, getpgid(job->pids[0]));
    tcsetattr(shell_terminal, TCSADRAIN, &job->tmodes);

    int pid_index = 0;
    while (job->pids[pid_index] > 0)
    {
        waitpid(job->pids[pid_index], &job->status, WUNTRACED);
        pid_index++;
    }

    fg_job = NULL;

    // Put the shell back in the foreground
    tcsetpgrp(shell_terminal, getpgrp());

    // Restore the shell’s terminal modes
    tcgetattr(shell_terminal, &job->tmodes);
    tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);

    print_job_status(job);

    if (WIFEXITED(job->status))
    {
        return WEXITSTATUS(job->status);
    }
    else
    {
        return 0;
    }
}

int
execute_command_or_builtin(char ** args,
                           char in_background,
                           char * infile, char * outfile, char * appfile,
                           int in_pipe, int out_pipe,
                           int in_pipe_other_end, int out_pipe_other_end,
                           int job_id)
{
    if (strcmp(args[0], "cd") == 0)
    {
        return chdir(args[1]);
    }
    else if (strcmp(args[0], "exit") == 0)
    {
        exit(0);
    }
    else if (strcmp(args[0], "jobs") == 0)
    {
        print_jobs();
        return 0;
    }
    else if (strcmp(args[0], "fg") == 0)
    {
        int job_argument;
        if (args[1] == NULL)
        {
            job_argument = 0;
        }
        else
        {
            sscanf(args[1], "%d", &job_argument);
        }
        foreground_process(&jobs[job_argument]);
        return 0;
    }
    else if (strcmp(args[0], "bg") == 0)
    {
        int job_argument;
        if (args[1] == NULL)
        {
            job_argument = 0;
        }
        else
        {
            sscanf(args[1], "%d", &job_argument);
        }
        background_process(&jobs[job_argument]);
        return 0;
    }
    else
    {
        return execute_command(args, in_background,
                               infile, outfile, appfile,
                               in_pipe, out_pipe,
                               in_pipe_other_end, out_pipe_other_end,
                               job_id);
    }
}