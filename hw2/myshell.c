#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int prepare(void) {
    signal(SIGCHLD, SIG_IGN); /* Eran's trick - avoid zombies */
    signal(SIGINT, SIG_IGN); /* ignore SIGINT always, except specific places in the code */
    return 0;
}

int regular_command(char** cmd) {
    pid_t pid = fork();
    if (pid < 0) { /* check fork() */
        perror("error - fork() failed");
        return 0;
    }
    if (pid == 0) { /* child process */
        signal(SIGINT, SIG_DFL); /* in case of a regular command a child process should terminate upon SIGINT */
        if (execvp(cmd[0], cmd) == -1) { /* check execvp() */
            perror("error - execvp() failed");
            exit(1);
        }
    }
    /* parent process - should wait for child process to end */
    else if ( waitpid(pid, NULL, 0)==-1 && errno!=ECHILD && errno!=EINTR ) {  /* ECHILD, EINTR errors are OK */
        perror("error - waitpid() failed");
        return 0; 
    }
    return 1;
}

int background_command(char** cmd, int last_arg_index) {
    pid_t pid = fork();
    if (pid < 0) { /* check fork() */
        perror("error - fork() failed");
        return 0;
    }
    if (pid == 0) { /* child process */
        cmd[last_arg_index - 1] = NULL; /* should't pass the '&' argument to execvp() */
        if (execvp(cmd[0], cmd) == -1) { /* check execvp() */
            perror("error - execvp() failed");
            exit(1);
        }
    } /* parent process shouldn't wait for child process to finish */
    return 1; 
}

int pipe_command(char** full_cmd, int pipe_symbol_index) {
    pid_t pid_write, pid_read;
    char** second_cmd = full_cmd + pipe_symbol_index + 1; /* pointer to the second command */
    int pipe_fd[2]; /* file descriptors array */
    full_cmd[pipe_symbol_index] = NULL; /* replace "|" with NULL for execvp() usage */
    if (pipe(pipe_fd) == -1 ) { /* check pipe() */
        perror("error - pipe() failed");
        return 0;
    }
    pid_write = fork();
    if (pid_write < 0) { /* check fork() */
        perror("error - fork() failed");
        return 0;
    }
    if (pid_write == 0) { /* writing child process - will execute the first command and output it to the pipe */
        signal(SIGINT, SIG_DFL); /* in case of a pipe command a child process should terminate upon SIGINT */
        /* copy write end of the pipe to the STDOUT entry (which is entry #1) */
        if (dup2(pipe_fd[1], 1) == -1) { /* check dup2() */
            perror("error - dup2() failed");
            exit(1);
        } 
        close(pipe_fd[1]); /* now that STDOUT is directed to the write end of the pipe there's no need for this pipe entrance */
        close(pipe_fd[0]); /* the writing child won't read from the pipe */
        /* now writing to STDOUT (performing execvp()) will be written to the pipe */
        if (execvp(full_cmd[0], full_cmd) == -1) { /* check execvp() */
            perror("error - execvp() failed");
            exit(1);
        }
    }
    pid_read = fork();
    if (pid_read < 0) { /* check fork() */
        perror("error - fork() failed");
        return 0;
    }
    if (pid_read == 0) { /* reading child process - will execute the second command and receive input from the pipe */
        signal(SIGINT, SIG_DFL); /* in case of a pipe command a child process should terminate upon SIGINT */
        /* copy read end of the pipe to the STDIN entry (which is entry #0) */
        if (dup2(pipe_fd[0], 0) == -1) { /* check dup2() */
            perror("error - dup2() failed");
            exit(1);
        }
        close(pipe_fd[0]); /* now that STDIN is directed to the read end of the pipe there's no need for this pipe exit */
        close(pipe_fd[1]); /* the reading child won't write to the pipe */
        /* now instead of reading from STDIN the reading child process will read from the pipe */ 
        if (execvp(second_cmd[0], second_cmd) == -1) { /* check execvp() */
            perror("error - execvp() failed");
            exit(1);
        }
    }
    close(pipe_fd[0]); /* the parent process also can write to the pipe so we need to close the write end of the pipe for him too */
    close(pipe_fd[1]); /* not as important to close the read end but it's better to do so */
    /* should wait for both child processes */
    if ( waitpid(pid_write, NULL, 0)==-1 && errno!=ECHILD && errno!=EINTR ) {  /* ECHILD, EINTR errors are OK */
        perror("error - waitpid() failed");
        return 0; 
    }
    if ( waitpid(pid_read, NULL, 0)==-1 && errno!=ECHILD && errno!=EINTR ) {  /* ECHILD, EINTR errors are OK */
        perror("error - waitpid() failed");
        return 0; 
    }
    return 1;
}

int redirection_command(char** cmd, int last_arg_index) {
    int fd_output_file; 
    pid_t pid = fork();
    cmd[last_arg_index - 2] = NULL; /* replace ">" with NULL for execvp() usage */
    if (pid < 0) { /* check fork() */
        perror("error - fork() failed");
        return 0;
    }
    if (pid == 0) { /* child process */
    	signal(SIGINT, SIG_DFL);
        fd_output_file = open(cmd[last_arg_index - 1], O_WRONLY | O_CREAT | O_TRUNC , 0777); /* open (or create if doesn't exist) the relevant file */
        if (fd_output_file == -1) { /* check open() */
            perror("error - open() failed");
            exit(1); 
        }
        /* copy file descriptor of the target file to STDOUT(#1) */
        if (dup2(fd_output_file, 1) == -1) { /* check dup2() */
            perror("error - dup2() failed");
            exit(1);
        }
        close(fd_output_file); /* after dup2 we don't need this to be open anymore */
        /* now the output of the first command is directed towards the opened file */
        if (execvp(cmd[0], cmd) == -1) { /* check execvp() */
            perror("error - execvp() failed");
            exit(1);
        }      
    }
    /* parent process should wait for child */
    else if ( waitpid(pid, NULL, 0)==-1 && errno!=ECHILD && errno!=EINTR ) {  /* ECHILD, EINTR errors are OK */
        perror("error - waitpid() failed");
        return 0; 
    }
    return 1;
}

/* The main function: checks what type of command was given and calls the relevant function to execute the command */
int process_arglist(int count, char** arglist) {
    int i = 0;
    if (count > 1){ /* background command can't be too "short" */
    	if (strcmp(arglist[count - 1], "&") == 0) { /* background command */
        	return background_command(arglist, count);
    	}
    }
    if (count > 2) { /* redirection command can't be too "short" */
    	if (strcmp(arglist[count - 2], ">") == 0) { /* redirection command */
        	return redirection_command(arglist, count);
    	}
    }
    for (i = 0; i < count; i++) { /* check if it's a pipe command */
        if (strcmp(arglist[i], "|") == 0) {
            return pipe_command(arglist, i);
        }
    }
    /* if the command is not from the 3 types above it's a regular command */
    return regular_command(arglist);
}

int finalize(void) {
    return 0;
}
