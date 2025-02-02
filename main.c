#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdarg.h>
#include <ncurses.h>
#include <errno.h>

typedef enum {
  INFO,
  ERROR,
} Log_Level;

static void _log(const char* file_name, int line, Log_Level lvl , const char* fmt, ...) {
  
  va_list args;
  FILE* output = stdout;
  if (lvl == ERROR) {
    output = stderr;
    fprintf(output, "ERROR ");
  }

  va_start(args, fmt);
  fprintf(output, "%s:%d - ", file_name, line);
  vfprintf(output, fmt, args); 
  va_end(args);


}

FILE* f;

#define log(...) _log(__FILE__, __LINE__, __VA_ARGS__);

typedef struct {
  char* data;
  size_t size;
} String_View;

String_View sv_from_cstr(char* cstr, size_t size) {
  return (String_View) {
    .data = cstr,
    .size = size 
  };
}

String_View chop_by_delimiter(String_View* sv, const char delimiter) {
  
  String_View result = { .data="", .size = 0};

  if (sv->size == 0) 
    return result; 

  for (int i = 0; i < sv->size; i++) {
    if (sv->data[i] == delimiter) {

      result.data = sv->data;
      result.size = i;

      i++;
      sv->data += i;
      sv->size -= i;  
      return result;
    }
  }

  result.data = sv->data;
  result.size = sv->size;

  sv->size = 0;

  return result;
}

int sv_contains(String_View haystack, String_View needle) {
   if (haystack.size < needle.size) {
      return 0;
   }
   if (haystack.size == 0 || needle.size == 0) {
      return 0;
   }

   for (size_t i = 0; i < haystack.size; i++) {
     if (i + needle.size > haystack.size) {
       return 0;
     }

     //printf("Checking the part: %.*s against %.*s\n", haystack.size - i, haystack.data + i, needle.size,
      //   needle.data);

     if (memcmp(haystack.data + i, needle.data, needle.size) == 0) {
       return 1;
     }
   }
   return 0;
}

typedef struct {
  char* buffer;
  size_t size;
  size_t capacity;
} String_Buffer;

typedef struct {
  char** lines;
  size_t size;
} Autocomplete;

Autocomplete autocomplete = {0};

static void increase_string_buffer_capacity(String_Buffer* sb, size_t capacity_increase) {

  if (sb->buffer == NULL ) {
    sb->buffer = malloc(sizeof(char) * (sb->capacity + capacity_increase));
  } else {
    sb->buffer = realloc(sb->buffer, sizeof(char) * (sb->capacity + capacity_increase));
  }
  sb->capacity = sb->capacity + capacity_increase;
  
  assert(sb->buffer && "Something wrong happened to the String Buffer");
}

static void add_to_string_buffer(String_Buffer* sb, const char* string, size_t size) {
  
  assert(sb && "String_Buffer cannot be null");
  
  if (sb->size + size >= sb->capacity) {
    //log(INFO, "Increasing capacity\n");
    increase_string_buffer_capacity(sb, size);
  }
  
  memcpy(sb->buffer + sb->size, string, size);

  sb->size += size; 
}

void shift_arg(int* argc, char*** argv) {
  (*argc)--;
  (*argv)++;
}

static void run_subprocess(char** params) {
  int stdoutfd[2];

  pid_t pid = fork();
  if (pid == 0) {
    execvp(params[0], params);
    fprintf(stderr, "An errour occour when executing the program %s", params[0]);
    exit(1);
  } else {
    wait(NULL);
  }
}

String_Buffer get_subprocess_output(char** params) {
  int fd[2];
  String_Buffer buf = {0};
  pipe(fd); 
  char buffer[512];
  pid_t pid = fork();
  if (pid == 0) {
    dup2(fd[1], STDOUT_FILENO);
    dup2(fd[1], STDERR_FILENO);
    close(fd[0]);
    close(fd[1]);
    execvp(params[0], params);
    fprintf(stderr, "An errour occour when executing the program %s", params[0]);
    exit(1);
  } else {
    close(fd[1]);
    int n_bytes = 0;
    int pid_status = 0; 

   for (;;) {
      if (waitpid(pid, &pid_status, 0) < 0) {
        fprintf(stderr, "Could not wait for pid %d: %s", pid, strerror(errno));
        break;
      }

      if (WIFEXITED(pid_status)) {
        int exit_status = WEXITSTATUS(pid_status);

        if (exit_status != 0) {
          fprintf(stderr, "The pid %d exit with non-zero status: %d",pid, exit_status);
          break;
        }
      }

      if (WIFSIGNALED(pid_status)) {
        fprintf(stderr, "The pid %d was terminated by %s", pid, strsignal(WTERMSIG(pid_status)));
        break;
      }
    } 

    do {
      n_bytes = read(fd[0], buffer, 512);

      if (n_bytes > 0) {
        add_to_string_buffer(&buf, buffer,n_bytes);
      }
    } while (n_bytes >0);

    close(fd[0]);
  }

  return buf;
}

String_Buffer get_pods(char* env) {
  char* params[] = {"kubectl", "get","pods","-n",env, NULL};
  return get_subprocess_output(params);
}

#define PAGER "less"

void output_to_pager(char** params) {
  int fd[2];

  String_Buffer buf = {0};
  pipe(fd); 
  pid_t pid = fork();
  if (pid == 0) {
    dup2(fd[1], STDOUT_FILENO);
    close(fd[0]);
    close(fd[1]);
    execvp(params[0], params);
  } 
  
  pid_t pid2 = fork();

  if (pid2 == 0) {
    dup2(fd[0], STDIN_FILENO);
    close(fd[0]);
    close(fd[1]);
    char* array[] = {PAGER, NULL};
    execvp(array[0], array);
  }
  
  close(fd[0]);
  close(fd[1]);

  pid_t wpid;
  int status; 
  while ((wpid = wait(&status)) > 0)
  {
  }
}

void print_status(WINDOW* window, char* env) {
  
  wclear(window);
  mvwprintw(window, 0, 0, "Env: %s", env[0] == 0 ? "NONE" : env);
  wrefresh(window);
}


void start_window() {
  
  f = fopen("dev/tty","r+");
  SCREEN *screen = newterm(NULL, f, f);

  set_term(screen);
  cbreak();
  noecho();

  if(has_colors()) {
    start_color();
    use_default_colors();
  }
  
  int row, col;

  getmaxyx(stdscr, row, col);

  WINDOW* log_window = newwin(row - 2, col, 0, 0);  
  
  char buffer[1024];
  size_t buf_size = 0;
  size_t lines = 0;
  WINDOW* status_window = newwin(1, col, row - 2, 0);
  WINDOW* command_window = newwin(1, col, row - 1, 0);
  int ch = 'a'; 
  int quit = 0;
  
  char env[1024]; 
  char pod_id[1024];
  
  keypad(command_window, TRUE);
  print_status(status_window, env);
  while (!quit) {
    ch = wgetch(command_window);
    if (ch == 10) {
      if (buf_size == 0) continue;
      if (strstr(buffer, ":e ") != 0) {
        wclear(log_window);
        String_View sv = sv_from_cstr(buffer, buf_size);
        String_View chop_string = chop_by_delimiter(&sv, ' ');
        mvwprintw(log_window, 0, 0, "Changing enviroment to `%.*s`", sv.size, sv.data);
        memset(env, 0, 1024);
        memcpy(env, sv.data, sv.size);
        wrefresh(log_window);
      } else if (env[0] == 0) {
        wclear(log_window);
        mvwprintw(log_window, 0, 0, "No env set, please use `:e environment` to set one");
        wrefresh(log_window);
      } else if (strncmp(":gp", buffer, buf_size) == 0) {
        wclear(log_window);	
        lines = 0;
        mvwprintw(log_window,lines++, 0, "Getting info from KBCTL");
        wrefresh(log_window);
        String_Buffer sb = get_pods(env);
        String_View sv = sv_from_cstr(sb.buffer, sb.size);
        String_View chop_string = chop_by_delimiter(&sv, '\n');
        int line_number = 0;
        do {
          line_number++;
          chop_string = chop_by_delimiter(&sv, '\n');
          mvwprintw(log_window,lines++, 0, "%.*s", chop_string.size, chop_string.data);
        } while (sv.size > 0);
        
        if (autocomplete.lines != NULL) {
          for (int i = 0; i < autocomplete.size; i++) {
            free(autocomplete.lines[i]);
          }
        }

        autocomplete.lines = malloc(sizeof(char*) * line_number);
        autocomplete.size = line_number;

        if (line_number > 0) {
          sv = sv_from_cstr(sb.buffer, sb.size);
          for (int i = 0; i < line_number; i++) {
            chop_string = chop_by_delimiter(&sv, '\n');
            chop_string = chop_by_delimiter(&sv, ' ');
            autocomplete.lines[i] = malloc(sizeof(char) * chop_string.size + 1);
            memcpy(autocomplete.lines[i], chop_string.data, chop_string.size);
            autocomplete.lines[i][chop_string.size + 1] = '\0';
          }
        }

        wrefresh(log_window);

      } else if (strstr(buffer, ":l ") != 0) {
        wclear(log_window);
        String_View sv = sv_from_cstr(buffer, buf_size);
        String_View chop_string = chop_by_delimiter(&sv, ' ');
        mvwprintw(log_window, 0, 0, "Getting logs from pod `%.*s`", sv.size, sv.data);
        memset(pod_id, 0, 1024);
        memcpy(pod_id, sv.data, sv.size);
        wrefresh(log_window);
        endwin();
        char* subcommand[] = {"kubectl", "logs", pod_id, "-n", env, NULL};
        output_to_pager(subcommand);
      }

      print_status(status_window, env);
      memset(buffer, 0, buf_size);
      buf_size = 0;
      wclear(command_window);
    } else if (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127) {
      if (buf_size == 0) continue;

      mvwprintw(command_window, 0, buf_size - 1, " ");
      buffer[buf_size--] = 0;
      mvwprintw(command_window, 0, 0, "%.*s",buf_size, buffer);
      wrefresh(command_window);
    } else if (ch == 9) { // TAB pressed
        for (int i = buf_size; i > 0; i--) {
          if (buffer[i] == ' ') {
            i++;
            String_View needle = sv_from_cstr(buffer + i, buf_size - i);
            for (int j = 0; j < autocomplete.size; j++) {  
              String_View haysack = sv_from_cstr(autocomplete.lines[j], strlen(autocomplete.lines[j])); 
              
              if (sv_contains(haysack, needle)) {
                memset(buffer + i , 0, buf_size - i);
                buf_size = buf_size - (buf_size - i);
                memcpy(buffer + buf_size, haysack.data, haysack.size);
                buf_size += haysack.size;
                mvwprintw(command_window, 0, 0, "%.*s",buf_size, buffer);
                wmove(command_window, buf_size, 0);
                wrefresh(command_window);
                break;
              }             
            }
            break;
          }
        }
    } else {
      buffer[buf_size] = (char) ch;
      buf_size++;
      mvwprintw(command_window, 0, 0, "%.*s",buf_size, buffer);
    }
  }
}


int main(int argc, char** argv) {

  start_window();
  return 0;
}
