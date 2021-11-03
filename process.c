#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <limits.h>
#include <linux/limits.h>
#include "/c/cs323/proj4/starter-code/parse.h"
#include "process.h"

#define STACK_SIZE 100

char *dir_stack[STACK_SIZE];         
int top = -1;  
bool is_dir_stack_empty() { return top == -1; }
char *dir_pop() { return dir_stack[top--]; }
void dir_push(char *path) { dir_stack[++top] = path; }
char *dir_peek() { return dir_stack[top]; }

// typedef struct cmd {
//   int type;             // Node type: SIMPLE, PIPE, SEP_AND, SEP_OR, SEP_END,
// 			//   SEP_BG, SUBCMD, or NONE (default)

//   int argc;             // Number of command-line arguments
//   char **argv;          // Null-terminated argument vector or NULL

//   int nLocal;           // Number of local variable assignments
//   char **locVar;        // Array of local variable names and the values to
//   char **locVal;        //   assign to them when the command executes

//   int fromType;         // Redirect stdin: NONE (default), RED_IN (<), or
// 			//   RED_IN_HERE (<<)
//   char *fromFile;       // File to redirect stdin, contents of here document,
// 			//   or NULL (default)

//   int toType;           // Redirect stdout: NONE (default), RED_OUT (>),
// 			//   RED_OUT_APP (>>)
//   char *toFile;         // File to redirect stdout or NULL (default)

//   int errType;          // Unused for this project.
//   char *errFile;        // Unused for this project.

//   struct cmd *left;     // Left subtree or NULL (default)
//   struct cmd *right;    // Right subtree or NULL (default)
// } CMD;

void handle_redirect_input(const CMD *cmd) {
  int new_input_fd = open(cmd->fromFile, O_RDONLY);
  dup2(new_input_fd, STDIN_FILENO);
  close(new_input_fd);
}

void handle_redirect_here_input(const CMD *cmd) {
  char template[] = "/tmp/Bash_heredoc_XXXXXX";
  int tmp_fd = mkstemp(template);
  write(tmp_fd, cmd->fromFile, strlen(cmd->fromFile));
  lseek(tmp_fd, 0, SEEK_SET);
  dup2(tmp_fd, STDIN_FILENO);
  close(tmp_fd);
}

void handle_redirect_output_append(const CMD *cmd) {
  int new_output_fd = open(cmd->toFile, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
  lseek(new_output_fd, 0, SEEK_END);
  dup2(new_output_fd, STDOUT_FILENO);
  close(new_output_fd);
}

void handle_redirect_output(const CMD *cmd) {
  int new_output_fd = open(cmd->toFile, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
  dup2(new_output_fd, STDOUT_FILENO);
  close(new_output_fd);
}

void handle_dir_change(const CMD *cmd) {
  char *command = cmd->argv[0];
  if (strcmp(command, "pushd") == 0) {
    char *path = cmd->argv[cmd->argc - 1];
    chdir(path);
    dir_push(path);
  } else if (strcmp(command, "popd") == 0) {
    dir_pop();
    chdir(dir_peek());
  } else if (strcmp(command, "cd") == 0) {
    char *path = cmd->argv[cmd->argc - 1];
    chdir(path);
  }
}

void handle_var_definitions(const CMD *cmd) {
  for (int i = 0; i < cmd->nLocal; i++) {
    setenv(cmd->locVar[i], cmd->locVal[i], 1);
  }
}

void handle_redirects(const CMD *cmd) {
  switch (cmd->toType) {
    case RED_OUT_APP:
      handle_redirect_output_append(cmd); break;
    case RED_OUT:
      handle_redirect_output(cmd); break;
    default: break;
  }

  if (cmd->fromFile != NULL) {
    switch (cmd->fromType) {
      case RED_IN_HERE:
        handle_redirect_here_input(cmd); break;
      case RED_IN:
        handle_redirect_input(cmd); break;
      default: break;
    }
  }
}

int process_simple(const CMD *cmd) {
  int status;
  pid_t pid = fork();

  if (pid == 0) {
    handle_var_definitions(cmd);
    handle_redirects(cmd);
    handle_dir_change(cmd);
    int exec_status = execvp(cmd->argv[0], cmd->argv);
    if (exec_status == -1) exit(0);
  }

  waitpid(pid, &status, 0);
  
  return STATUS(status);
}

int process_pipe(const CMD *cmd) {
  if (cmd == NULL) return 0;
  int fd_pair[2];
  int status;
  pipe(fd_pair);
  
  int left_pid = fork();
  if (left_pid == 0) {
    dup2(fd_pair[1], STDOUT_FILENO);
    status = process(cmd->left);
    close(fd_pair[0]);
    close(fd_pair[1]);
    exit(status);
  }
  waitpid(left_pid, &status, 0);

  int right_pid = fork();
  if (right_pid == 0) {
    dup2(fd_pair[0], STDIN_FILENO);
    status = process(cmd->right);
    close(fd_pair[0]);
    close(fd_pair[1]);
    exit(status);
  }
  waitpid(right_pid, &status, 0);

  close(fd_pair[0]);
  close(fd_pair[1]);

  return STATUS(status);
}

int process(const CMD *cmd) {
  int status = 0;
  if (cmd == NULL) return status;

  switch (cmd->type) {
    case PIPE:
      status = process_pipe(cmd);
    case SIMPLE:
    default:
      status = process_simple(cmd);
  }
  // status = status | process(cmd->left);
  // status = status | process(cmd->right);
  return status;
}
