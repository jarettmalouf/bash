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
#include <math.h>
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

int get_exit_code(int s1, int s2) {
  if (s2 != 0) return s2;
  if (s1 != 0) return s1;
  return 0;
}

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

void print_stack() {
  for (int i = top; i >= 0; i--) {
    fprintf(stdout, "%s", dir_stack[i]);
    if (i > 0) fprintf(stdout, " ");
  }
  fprintf(stdout,"\n");
}

int handle_dir_change(const CMD *cmd) {
  int status = 0;

  // pushd
  if (strcmp(cmd->argv[0], "pushd") == 0) {
    if (cmd->argc != 2) return STATUS(1);
    if (top == -1) dir_push("/");
    char *cwd = (char*) malloc(sizeof(char) * 100);
    status = chdir(cmd->argv[1]);
    getcwd(cwd, 100);
    dir_push(cwd);
    print_stack();
  } 

  // popd
  else if (strcmp(cmd->argv[0], "popd") == 0) {
    if (top == -1) return STATUS(1);
    char *popped_dir = dir_pop();
    fprintf(stdout, "%s\n", popped_dir);
    status = chdir(dir_peek());
  } 
  
  // cd
  else if (strcmp(cmd->argv[0], "cd") == 0) {
    if (cmd->argc == 1) {
      char *home = getenv("HOME");
      if (home == NULL) {
        return STATUS(1);
      }
      status = chdir(home);
    } else if (cmd->argc == 2) {
      char *path = cmd->argv[1];
      status = chdir(path);
    }
  }

  return STATUS(status);
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

  switch (cmd->fromType) {
    case RED_IN_HERE:
      handle_redirect_here_input(cmd); break;
    case RED_IN:
      handle_redirect_input(cmd); break;
    default: break;
  }
}

bool is_builtin(const CMD *cmd) {
  return strcmp(cmd->argv[0], "cd") == 0 || strcmp(cmd->argv[0], "pushd") == 0 || strcmp(cmd->argv[0], "popd") == 0;
}

int process_simple(const CMD *cmd) {
  int status;
  pid_t pid = fork();

  if (pid == 0) {
    handle_var_definitions(cmd);
    handle_redirects(cmd);
    int exec_status = execvp(cmd->argv[0], cmd->argv);
    if (exec_status == -1) { 
      exit(1); 
      // perror(""); 
    }
  } 

  if (is_builtin(cmd)) status = handle_dir_change(cmd);
  waitpid(pid, &status, 0);
  
  return STATUS(status);
}

void close_fd_pair(int fd_pair[2]) {
  close(fd_pair[0]);
  close(fd_pair[1]);
}

int process_pipe(const CMD *cmd) {
  if (cmd == NULL) return 0;
  int fd_pair[2];
  int status_l, status_r;
  pipe(fd_pair);
  
  pid_t left_pid = fork();
  if (left_pid == 0) {
    dup2(fd_pair[1], STDOUT_FILENO);
    close_fd_pair(fd_pair);
    exit(process(cmd->left));
  }

  pid_t right_pid = fork();
  if (right_pid == 0) {
    dup2(fd_pair[0], STDIN_FILENO);
    close_fd_pair(fd_pair);
    exit(process(cmd->right));
  }

  close_fd_pair(fd_pair);

  waitpid(left_pid, &status_l, 0);
  waitpid(right_pid, &status_r, 0);

  return STATUS(get_exit_code(status_l, status_r));
}

void print_info(const CMD *cmd) {
  if (cmd == NULL) return;
  fprintf(stderr, "Here is the command struct:\n");
  fprintf(stderr, "type: %d\n", cmd->type);
  fprintf(stderr, "argc: %d\n", cmd->argc);
  fprintf(stderr, "nLocal: %d\n", cmd->nLocal);
  fprintf(stderr, "fromType: %d\n", cmd->fromType);
  print_info(cmd->left);
  print_info(cmd->right);
}

int process_sep_end(const CMD *cmd) {
  int status_l, status_r;

  pid_t left_pid = fork();
  if (left_pid == 0) {
    status_l = process(cmd->left);
    exit(status_l);
  }
  waitpid(left_pid, &status_l, 0);

  pid_t right_pid = fork();
  if (right_pid == 0) {
    status_r = process(cmd->right);
    exit(status_r);
  }
  waitpid(right_pid, &status_r, 0);

  return STATUS(get_exit_code(status_l, status_r));
}

int process_subcmd(const CMD *cmd) {
  int status;
  pid_t pid = fork();

  if (pid == 0) {
    handle_var_definitions(cmd);
    handle_redirects(cmd);
    // handle_dir_change(cmd);
    exit(process(cmd->left));
  }

  waitpid(pid, &status, 0);
  
  return STATUS(status);
}

int process_sep_bg(const CMD *cmd) {
  int status_r = 0;

  pid_t left_pid = fork();
  if (left_pid == 0) {
    exit(process(cmd->left));
  }

  pid_t right_pid = fork();
  if (right_pid == 0) {
    status_r = process(cmd->right);
    fprintf(stderr, "Backgrounded: %d\n", getpid());
    exit(status_r);
  }

  waitpid(right_pid, &status_r, 0);
  return STATUS(status_r);
}

void reap_zombies() {
  int child_status;
  pid_t pid;
  while (1) {
    pid = waitpid(-1, &child_status, WNOHANG);
    if (pid <= 0) break;
    fprintf(stderr, "Completed: %d (%d)\n", pid, child_status);
  }
}

int process_sep_and(const CMD *cmd) {
  int status_l, status_r;
  
  pid_t left_pid = fork();
  if (left_pid == 0) {
    status_l = process(cmd->left);
    exit(status_l);
  }
  waitpid(left_pid, &status_l, 0);

  if (status_l != 0) {
    return STATUS(status_l);
  }

  pid_t right_pid = fork();
  if (right_pid == 0) {
    status_r = process(cmd->right);
        exit(status_r);

  }
  waitpid(right_pid, &status_r, 0);

  return STATUS(status_r);
}

int process_sep_or(const CMD *cmd) {
  int status_l, status_r;
  
  pid_t left_pid = fork();
  if (left_pid == 0) {
    status_l = process(cmd->left);
    exit(status_l);
  }
  waitpid(left_pid, &status_l, 0);

  if (status_l == 0) {
    return STATUS(status_l);
  }

  pid_t right_pid = fork();
  if (right_pid == 0) {
    status_r = process(cmd->right);
    exit(status_r);
  }
  waitpid(right_pid, &status_r, 0);

  return STATUS(status_r);
}

void set_last_status(int status) {
  char s_status[100];
  sprintf(s_status, "%d", status);
  setenv("?", s_status, 1);
}

int process(const CMD *cmd) {
  reap_zombies();
  int status = 0;

  if (cmd == NULL) return status;

  switch (cmd->type) {
    case SEP_BG:
      status = process_sep_bg(cmd); break;
    case SEP_END:
      status = process_sep_end(cmd); break;
    case PIPE:
      status = process_pipe(cmd); break;
    case SEP_AND:
      status = process_sep_and(cmd); break;
    case SEP_OR:
      status = process_sep_or(cmd); break;
    case SUBCMD:
      status = process_subcmd(cmd); break;
    default:
      status = process_simple(cmd);
  }

  set_last_status(status);
  return status;
}

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