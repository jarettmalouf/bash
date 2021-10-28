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

int process_simple(const CMD *cmd) {
  int status;
  char *command = cmd->argv[0];
  pid_t pid = fork();

  if (pid == 0) {
    // handle var definition
    for (int i = 0; i < cmd->nLocal; i++) {
      fprintf(stderr, "processing %s\n", cmd->locVar[i]);
      setenv(cmd->locVar[i], cmd->locVal[i], 1);
    }

    // handle redirects
    if (cmd->toFile != NULL) {
      int new_output_fd = open(cmd->toFile, O_RDWR);
      dup2(new_output_fd, STDOUT_FILENO);
      close(new_output_fd);
    }

    if (cmd->fromFile != NULL) {
      int new_input_fd = open(cmd->fromFile, O_RDONLY);
      dup2(new_input_fd, STDIN_FILENO);
      close(new_input_fd);
    }

    // handle specific cases for pushd, popd, cd
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

    execvp(command, cmd->argv);
  }

  waitpid(pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

  return status;
}

int process(const CMD *cmd) {
  int status = 0;
  if (cmd == NULL) return status;
  
  switch (cmd->type) {
    case SIMPLE:
    default:
      status = status | process_simple(cmd);
  }
  status = status | process(cmd->left);
  status = status | process(cmd->right);
  return status;
}
