#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <signal.h> 
const char *sysname = "shellish";
enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};
struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};


/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}



/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

int cut_command(struct command_t *command)
{
  char splitter = '\t';
  int requested_fields[100];
  int field_count = 0;
  char buffer[1000];

  int arg_count = command->arg_count;
  for(int i = 0; i < arg_count; i++)
  {
    if(command -> args[i] == NULL)
    {
      break;
    }
    if(strcmp(command->args[i], "-d") == 0 || strcmp(command->args[i], "--delimiter") == 0)
    {
      splitter = command->args[i + 1][0]; 
      i++;
    }
    else if(strcmp(command->args[i], "-f") == 0 || strcmp(command->args[i], "--fields") == 0)
    {
      char *field_string = strdup(command->args[i + 1]);
      char *field_by_index = strtok(field_string, ",");
      while(field_by_index != NULL)
      {
        int temp_index = atoi(field_by_index);
        requested_fields[field_count++] = temp_index;
        field_by_index = strtok(NULL, ",");
      }
      i++;
    }
  }
  while(fgets(buffer, sizeof(buffer), stdin) != NULL)
  {
    char *line_fields[100];
    int total_fields = 0;
    char splitters[3] = {splitter, '\n', '\0'};

    char *fields_by_index = strtok(buffer, splitters);

    while(fields_by_index != NULL)
    {
      line_fields[total_fields++] = fields_by_index;
      fields_by_index = strtok(NULL, splitters);
    }
    for(int j = 0; j < field_count; j++)
    {
      int temp_field = requested_fields[j];

      printf("%s", line_fields[temp_field - 1]);

      if(j < field_count - 1)
      {
        printf("%c", splitter);
      }
    }
    printf("\n");
  }
  return SUCCESS;
}

int IOredirections(struct command_t *command)
{
    //reads from file, input is read from file. It replaces fd(file descriptor) to read only from the chosen file
    if(command->redirects[0] != NULL) // <
    {
      int file = open(command->redirects[0], O_RDONLY, 0777); // taken example from https://www.youtube.com/watch?v=5fnVr-zH-SE

      if(file == -1)
      {
        return -2;
      }
      dup2(file, STDIN_FILENO);

      close(file);
    }
    //check if file exists, if not - truncate. It replaces fd(file descriptor) to check whether file exists and if it does - to overwrite the contents
    if(command->redirects[1] != NULL) // >
    {
      int file = open(command->redirects[1], O_WRONLY | O_TRUNC | O_CREAT, 0777); // taken example from https://www.youtube.com/watch?v=5fnVr-zH-SE

      if(file == -1)
      {
        return -2;
      }
      dup2(file, STDOUT_FILENO);

      close(file);
    }
    //check if file exists, if not - append. It replaces fd(file descriptor) to check whether file exists and if it does - to overwrite the contents
    if(command->redirects[2] != NULL) // >>
    {
      int file = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0777); // taken example from https://www.youtube.com/watch?v=5fnVr-zH-SE

      if(file == -1)
      {
        return -2;
      }
      dup2(file, STDOUT_FILENO);

      close(file);
    }
    return SUCCESS;
}

void chatroom(char *roomname, char *username)
{
  int fd;
  char room_path[300];
  char pipe[512];

  snprintf(room_path, sizeof(room_path), "/tmp/chatroom-%s", roomname);   
  snprintf(pipe, sizeof(pipe), "%s/%s", room_path, username);


  mkdir(room_path, 0777); // example taken from https://www.geeksforgeeks.org/linux-unix/create-directoryfolder-cc-program/
  mkfifo(pipe, 0666); // example taken from https://www.geeksforgeeks.org/cpp/named-pipe-fifo-example-c-program/

  pid_t pid = fork();
  if(pid == 0)
  {
    char str1[1000];
    while(1)
    {
      fd = open(pipe, O_RDONLY);
      if(read(fd, str1, 1000) > 0)
      {
        printf("%s", "\n");
        printf("%s", str1);
        printf("%s", "\n");
      }
      close(fd);
    }
  }
  else
  {
    char str2[1000];
    while(1)
    {
      printf("[%s] %s:", roomname, username);
      fgets(str2, 1000, stdin);

      DIR *directory = opendir(room_path); //example taken https://www.baeldung.com/linux/list-files-using-c
      struct dirent *dir;
      if(directory)
      {
        while((dir = readdir(directory)) != NULL) //checks if there is directory
        {
          if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 || strcmp(dir->d_name, username) == 0)
          {
            continue;
          }

          if(fork() == 0) //continues checking directories
          {
            char str3[1000];
            sprintf(str3, "%s/%s", room_path, dir->d_name); //creates directory path

            int fd_out = open(str3, O_WRONLY);
            if(fd_out != -1)
            {
              char result[2000];
              sprintf(result, "[%s] %s: %s", roomname, username, str2);
              write(fd_out, result, strlen(result) + 1); // writes the result
              close(fd_out);
            }
            exit(0);
          }
        }
        closedir(directory); // closes the directory
      }
    }
    kill(pid, SIGKILL); // kills the process
  }
}
//deletes text files, that are empty
void cleanuptxt()
{
  DIR *directory = opendir("."); // opens directory
  if(directory == NULL)
  {
    return;
  }
  struct dirent *oneinstance;
  struct stat file_stats;

  while(((oneinstance = readdir(directory)) != NULL)) //checks if directory exists
  {
    char *file_end = strrchr(oneinstance->d_name, '.');
    if(file_end != NULL && strcmp(file_end, ".txt") == 0) //checks if file is a text file
    {
      if(stat(oneinstance->d_name, &file_stats) == 0)
      {
        if(file_stats.st_size == 0)
        {
          printf("%s\n", oneinstance->d_name); // prints deleted file
          unlink(oneinstance->d_name); // deletes text file
        }
      }
    }
  }
  closedir(directory); // closes directory stream
}

void exec_from_path(char *temp_path, char *path, struct command_t *command, bool isNext)
{
  while (temp_path != NULL) 
  {
    strcpy(path, temp_path); // copies part of already parsed path to full path
    strcat(path, "/"); // appends / char
    if(isNext)
    {
    strcat(path, command->next->name); // appends command name to full path
    execv(path, command->next->args); // executes path with arguments
    }
    else
    {
    strcat(path, command->name); // appends command name to full path
    execv(path, command->args); // executes path with arguments
    }
    temp_path = strtok(NULL, ":"); //start from where you finished
  }
}

int process_command(struct command_t *command) 
{

  //printf("%s \n", command->args[count - 1]);
  //printf("%d \n", count);

  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
  int count = command->arg_count;
  
  if(count > 0 && (command->args[count - 1] != NULL) && strcmp(command->args[count - 1], "&") == 0) // checks if command has & at the end
  {
    command->background = 1; // changes background status to 1
    command->args[count - 1] = NULL; // changes the background
    command->arg_count = command->arg_count - 1; // lowers arg count by 1
  }

  //cleanuptxt command
  if(strcmp(command->name, "cleanuptxt") == 0)
  {
    cleanuptxt();
    return SUCCESS;
  }

  int fd[2];
  char* env_path = getenv("PATH"); // gets enviroment path

  if(command->next != NULL)
  {
    //prepares or creates the pipe
    pid_t pid1;
    pipe(fd);

    //forking
    pid1 = fork();

    if (pid1 == 0) // child
    {
      /// This shows how to do exec with environ (but is not available on MacOs)
      // extern char** environ; // environment variables
      // execvpe(command->name, command->args, environ); // exec+args+path+environ

      /// This shows how to do exec with auto-path resolve
      // add a NULL argument to the end of args, and the name to the beginning
      // as required by exec

      // TODO: do your own exec with path resolving using execv()
      // do so by replacing the execvp call below


      //execvp(command->name, command->args); // exec+args+path


      IOredirections(command);

      //write to pipe - changes file descriptor to write to buffer
      dup2(fd[1], STDOUT_FILENO);
      close(fd[1]);
      close(fd[0]);

      //cut command

      if(strcmp(command->name, "cut") == 0)
      {
        cut_command(command);
        exit(0);
      }

      //chatroom command

      if(strcmp(command->name, "chatroom") == 0)
      {
        if(!command->arg_count < 2)
        {
          chatroom(command->args[1], command->args[2]);
        }
      }

      env_path = getenv("PATH"); // gets enviroment path
      char* temp_path = strtok(env_path, ":"); //divides by :
      char path[2000];
      exec_from_path(temp_path, path, command, false);

      printf("-%s: %s: command not found\n", sysname, command->name);
      exit(127); 
    }

    //second forking
    pid_t pid2;
    pid2 = fork();
    if(pid2 == 0)
    {

      IOredirections(command->next);
      dup2(fd[0], STDIN_FILENO);
      close(fd[1]);
      close(fd[0]);

      //cut command

      if(strcmp(command->next->name, "cut") == 0)
      {
        cut_command(command);
        exit(0);
      }

      //chatroom command

      if(strcmp(command->name, "chatroom") == 0)
      {
        if(!command->arg_count < 2)
        {
          chatroom(command->args[1], command->args[2]);
        }
      }
      char* temp_path2 = strtok(env_path, ":"); //divides by :
      char path2[2000];
      exec_from_path(temp_path2, path2, command, true);

      printf("-%s: %s: command not found\n", sysname, command->name);
      exit(127); 
    }
    close(fd[0]);
    close(fd[1]);
    if(command->background == 0) // check if command is in foreground
    {
      waitpid(pid1, NULL, 0); // blocks the parent process until child process is finished,
      waitpid(pid2, NULL, 0);
      // waitpid recommended by Gemini AI, it was used to understand when to use waitpid() and when to use wait().
    }
    return SUCCESS;
  }
  else 
  {
    pid_t pid = fork();
    if(pid == 0)
    {
      IOredirections(command);
      char* temp_path2 = strtok(env_path, ":"); //divides by :
      char path2[2000];

      //cut command

      if(strcmp(command->name, "cut") == 0)
      {
        cut_command(command);
        exit(0);
      }

      //chatroom command

      if(strcmp(command->name, "chatroom") == 0)
      {
        if(!command->arg_count < 2)
        {
          chatroom(command->args[1], command->args[2]);
        }
      }
      exec_from_path(temp_path2, path2, command, false);
      
      printf("-%s: %s: command not found\n", sysname, command->name);
      exit(127); 
    }
    if(command->background == 0)
    {
      waitpid(pid, NULL, 0);
    }
    return SUCCESS; 
  }  
}
int main() 
{
  while (1) 
  {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}

