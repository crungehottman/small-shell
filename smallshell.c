/* Author:      Caleigh Runge-Hottman
 * Description: 
 *
 * A mini-version (i.e., fewer features) of the bash shell.
 * 
 * FEATURES:
 *    - The shell handles the following built-in commands:
 *         ls, cd, status, exit
 *    - The rest of the commands are passed into exec()
 *    - Comments (i.e., lines beginning with the '#' char) are supported
 *    - Allow for the redirection of stdin and stdout
 *    - Supports both foreground and background processes, controllable
 *      by the command line and by receiving signals
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>


////////////////////////////////////////////////////////////////////////
// SIGINT & SIGTSTP handling
////////////////////////////////////////////////////////////////////////
int fg_only_mode = 0; // keeps track of whether we're in foreground only mode

// catch ctrl-c
void catchSIGINT(int signo) {
   char* message = "Caught SIGINT\n";
   write(STDOUT_FILENO, message, 14);
}

// catch ctrl-z
void catchSIGTSTP (int signo) {
   char* enter_fg_mode = "Entering foreground-only mode (& is now ignored)\n: ";
   char* exit_fg_mode = "Exiting foreground-only mode\n: ";
   if (fg_only_mode == 0) { // if not already in fg only mode, switch
      fg_only_mode = 1;
      write(STDOUT_FILENO, enter_fg_mode, 49);
   }
   else { // if already in fg only mode, switch to non-fg only mode
      fg_only_mode = 0;
      write(STDOUT_FILENO, exit_fg_mode, 29);
   }
}

int main() {
   const int argsize = 20;

   // loops
   int i = 0;
   int j = 0;

   // flags
   int shell_exit = 0; // determines whether user input == exit
   int run_in_background = 0; // keeps track of whether its a bgr process

   // holding user input
   char *user_input = malloc(2048 * sizeof(char)); // can support 2048 chars
   char *input = malloc(2048 * sizeof(char)); // can support 2048 chars

   // for parsing user input into arg array
   int num_args = 0; // keep track of # of args
   char *token = malloc(argsize * sizeof(char)); // for strtok
   char *command = malloc(argsize * sizeof(char)); // store the command

   // hold input file and output file
   char *input_file = malloc(25 * sizeof(char));
   char *output_file = malloc(25 * sizeof(char));
   int input_fd; // input file descriptor...look @ slide 130
   int output_fd; // output file descriptor
   int result = 0; // store dup2 errors
   char *cwd = malloc(75 * sizeof(char)); // for getting cwd for debug
   char buff[76]; // for cwd for debug

   // processes and children
   pid_t spawnpid = getpid(); //holds process id
   int childExitMethod = -5;
   int exit_status = 0; // holds the exit status if one exists
   int term_signal = 0; // holds the term signal if one exists
   int fork_now = 0; // flag to avoid fork bombs
   // allocate space for 150(?) child process ids
   pid_t *children = malloc(150 * sizeof(pid_t));
   int num_procs = 0; // holds the total # processes for array indexing


   ///////////////////////////////////////////////////////////////////////////
   // signal handling
   ///////////////////////////////////////////////////////////////////////////
   // CTRL-C command will send SIGINT signal
   // Make sure SIGINT only terminates foreground command, if one is running. 
   // Because of re-entrancy, do not use printf() in signal handling functions
   ///////////////////////////////////////////////////////////////////////////
   struct sigaction SIGINT_action;
   struct sigaction SIGTSTP_action;

   SIGINT_action.sa_handler = catchSIGINT;
   sigfillset(&SIGINT_action.sa_mask);
   SIGINT_action.sa_flags = SA_RESTART;

   SIGTSTP_action.sa_handler = catchSIGTSTP;
   sigfillset(&SIGTSTP_action.sa_mask);
   SIGTSTP_action.sa_flags = SA_RESTART;

   sigaction(SIGINT, &SIGINT_action, NULL);
   sigaction(SIGTSTP, &SIGTSTP_action, NULL);

   ////////////////////////////////////////////////////////////////////////////
   //                             shell loop
   ////////////////////////////////////////////////////////////////////////////
   // loops until exit command found
   //////////////////////////////////////////////////////////////////////////// 
   do {
      //*********reset these vars for safety****************
      fflush(stdout);
      num_args = 0;
      run_in_background = 0;
      fork_now = 0;
      memset(command, '\0', sizeof(command));
      memset(user_input, '\0', sizeof(input));
      memset(input_file, '\0', sizeof(input_file));
      memset(output_file, '\0', sizeof(output_file));
      char *args[513];
      for (i = 0; i < 513; i++) { // 512 args + 1 command
         args[i] = malloc(20 * sizeof(char));
         memset(args[i], '\0', sizeof(args[i]));
      }

      /////////////////////////////////////////////////////////////////////////
      // The Prompt
      /////////////////////////////////////////////////////////////////////////
      // Syntax of command line:
      //    command [arg1 arg2 ...] [< input_file] [> output_file] [&]
      // where the items in brackets [] are optional. 
      // 
      // Uses a colon (:) as a prompt for each command line.
      //
      // Handling blank lines and comments:
      //  - When we receive a blank line or a line beginning with the 
      //  '#' character, do nothing, and re-prompt.
      /////////////////////////////////////////////////////////////////////////
      printf(": ");
      fflush(stdout); // flush buffers every time for safety/sanity!
      char *r;
      r = fgets(user_input, 2049, stdin); // read at most 2048 chars


      /////////////////////////////////////////////////////////////////////////
      // PARSE USER INPUT
      /////////////////////////////////////////////////////////////////////////
      // Format: 
      //    command [arg1 arg2...] [< input_file] {> output_file] [&]
      // store command into 'command' var
      // store args into arg array
      // store input file into 'input_file' var
      // store output file into 'output_file' var
      // flag the & with 'run_in_background' var
      // store the total # of args in num_args
      ////////////////////////////////////////////////////////////////////////
      if (user_input[0] == '\n') { // check for blank line
         token = NULL; // if blank, no need to tokenize
      }
      else { // a-ok to tokenize...get command or comment
         token = strtok(user_input, " \n"); // get command or comment
         if (token[0] == '#') { // check whether line is a comment
            token = NULL; // if line is a comment, skip the following loop.
         }
         else { // if not a comment, it's a command
            strcpy(command, token);
         }
      }

      // tokenize the rest of the command line input...
      while (token != NULL) {
         if (strcmp(token, "<") == 0) { // is there an input file?
            // [< input file] : we know the next "arg" is input file
            token = strtok(NULL, " \n"); // get the input file
            strcpy(input_file, token); // store input_file
         }
         else if (strcmp(token, ">") == 0) { // is there an output file?
            // [ > output file] : we know the next "arg" is the output file
            token = strtok(NULL, " \n"); // get the output file
            strcpy(output_file, token); // store output_file
         }
         else if (strcmp(token, "&") == 0) {// should this be run in the bgr?
            // don't run it in the bgr if the command is echo or if fg only mode
            if (fg_only_mode == 0 && (strcmp(command, "echo") != 0)) {
               run_in_background = 1; // set flag only if conditions met
            }
         }
         else { // everything else is considered an argument!
           // if it were the first time thru the loop, the 1st token (command)
           // would go into the args array, but the command isn't an arg.
           // still need to keep the cmnd in here for execv() though
            strcpy(args[num_args], token);      
            num_args++;
         }
         token = strtok(NULL, " \n"); // get next token
      }
      // last arg must be NULL for execvp()!!!!!!!
      args[num_args] = NULL;

      // replace any instance of $$ with pid
      char *tmpstr = malloc(20*sizeof(char));
      for (i = 0; i < num_args; i++) {
         if (strstr(args[i], "$$")!=NULL) { // found substring
            // if arg is simply $$, not appended to anything
            if (strcmp(args[i], "$$") == 0) {
               sprintf(args[i], "%d", getpid());
            }
            // if its appended to something else
            else { // note: this only works if $$ is appended to the end
               tmpstr = strtok(args[i], "$$");
               sprintf(args[i], "%s%d", tmpstr, getpid());
            }
         }
      }


      /////////////////////////////////////////////////////////////////////////
      // exit command (built-in)
      /////////////////////////////////////////////////////////////////////////
      // When this command is run, shell kills any other processes or jobs that 
      // the shell started before it terminates itself.
      //
      // Note: If the user tries to run a built in command in the background 
      // with &, ignore it, and run it in the foreground.
      /////////////////////////////////////////////////////////////////////////
      if (strcmp(command, "exit") == 0) {
         // kill off any processes or commands before exiting...
         // loop thru background processes & raise sigkill on each process:
         for (i = 0; i < num_procs; i++) {
            if (children[i] > 0) {
               kill(SIGKILL, children[i]);
               // wait for the dieing process
               waitpid(children[i], &childExitMethod, 0);
            }
         }
         shell_exit = 1;
         exit(0);
      }

      /////////////////////////////////////////////////////////////////////////
      // cd command (built-in)
      /////////////////////////////////////////////////////////////////////////
      // changes directories
      //
      // By itself (cd), it changes the directory specified in the HOME env.
      // variable (NOT the location where smallsh was executed from, unless 
      // smallsh is located in the HOME dir).
      // 
      // It can take 1 arg: the path of the directory to change to. 
      //
      // Note: Supports exact and relative paths.
      //////////////////////////////////////////////////////////////////////////
      else if (strcmp(command, "cd") == 0) {
         if (num_args == 1) { // an arg was not specified
            // change location to dir specified in the HOME environment variable
            chdir((getenv("HOME")));
         }
         else if (num_args == 2){ // 1 arg was specified
            // change location to arg[1]
            chdir(args[1]);
         }
         fflush(stdout);
      }

      /////////////////////////////////////////////////////////////////////////
      // status command (built-in)
      /////////////////////////////////////////////////////////////////////////
      // prints out either:
      //   the exit status 
      //        OR
      //   the termianting signal of the last *foreground* process.
      /////////////////////////////////////////////////////////////////////////
      else if (strcmp(command, "status") == 0) {
         if (WIFEXITED(childExitMethod) != 0) {
            // terminated normally, get exit status
            exit_status = WEXITSTATUS(childExitMethod);
            printf("exit value %d\n", exit_status);
            fflush(stdout);
         } // if here, not exited normally
         else if (WIFSIGNALED(childExitMethod) != 0) {
           // the process was terminated by a signal
           term_signal = WTERMSIG(childExitMethod);
           printf("terminated by signal %d\n", term_signal);
           fflush(stdout);
         } 
      }

      /////////////////////////////////////////////////////////////////////////
      // non-built in commands
      /////////////////////////////////////////////////////////////////////////
      // passed on to a member of the exec() family of functions
      /////////////////////////////////////////////////////////////////////////
      else {
         if (run_in_background == 0) { // if run in foreground...
	    spawnpid = fork();
            fork_now++;
            if (fork_now >= 50) { // handle forkbomb errors
               printf("over 50 forks...aborting...\n");
               abort();
            }
            switch(spawnpid) {
               case -1: 
                  // ERROR
                  perror("fork error\n");
                  exit(1);
                  break;
               case 0:
               // in child... 
                  // redirect stdin if we have input file
                  if (input_file[0] != '\0') { // an input file is specified
                     input_fd = open(input_file, O_RDONLY);
                     if (input_fd == -1) { perror("open()"); exit(1); }
                     // call dup2() to change in_fd to point where stdin points
                     result = dup2(input_fd, 0); // stdin == 0
                     if (result == -1) { perror("dup2"); exit(2); }
                  }
                  // redirect stdout if we have output file
                  if (output_file[0] != '\0') { // an output file is specified
                     output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                     if (output_fd == -1) { perror("open()"); exit(1); }
                     // call dup2() to change out_fd to point where stdout points
                     result = dup2(output_fd, 1); // stdout == 1
                     if (result == -1) { perror("dup2"); exit(2); }
                  }
                  // call execvp
                  if (execvp(args[0], args) < 0) {
                     // if it returns, there was an error
                     perror("incorrect command");
                     exit(1);
                  }
                  break;
               default:
               // in parent...
                  waitpid(spawnpid, &childExitMethod, 0);
                  // if child killed by signal, print out signal #
                  if (WIFEXITED(childExitMethod) != 0) {
                     // terminated normally, get exit status
                     exit_status = WEXITSTATUS(childExitMethod);
                  } // if here, not exited normally
                  else if (WIFSIGNALED(childExitMethod) != 0) {
                     // the process was terminated by a signal
                     term_signal = WTERMSIG(childExitMethod);
                     if (term_signal != 11) {
                        printf("terminated by signal %d\n", term_signal);
                     }
                  }
                  break;
            }
         }
         else { // run in background...
            spawnpid = fork();
            fork_now++;
            if (fork_now >= 50) { // handle forkbomb errors
               printf("error: forked over 50 processes. aborting...\n");
               abort();
            }
            switch(spawnpid) {
               case -1: 
                  perror("fork error\n");
                  exit(1);
                  break;
               case 0: // in child... 
                  // redirect stdin if we have input file
                  if (input_file[0] != '\0') { // an input file is specified
                     input_fd = open(input_file, O_RDONLY);
                     if (input_fd == -1) { perror("open()"); exit(1); }
                     // call dup2() to change in_fd to point where stdin points
                     result = dup2(input_fd, 0); // stdin == 0
                     if (result == -1) { perror("dup2"); exit(2); } // slide 126
                  }
                  else { // no specified input file
                     // if not specified, bgr commands redirect to /dev/null/
                     input_fd = open("/dev/null", O_RDONLY);
                     // call dup2() to change devnull to pt where stdin points
                     result = dup2(input_fd, 0); // stdin == 0
                  }
                  // redirect stdout if we have output file
                  if (output_file[0] != '\0') { // an output file is specified
                     output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                     if (output_fd == -1) { perror("open()"); exit(1); }
                     // call dup2() to change out_fd to point where stdout points
                     result = dup2(output_fd, 1); // stdout == 1
                     if (result == -1) { perror("dup2"); exit(2); } // slide 126
                  }
                  else { // no specified output file
                     output_fd = open("/dev/null", O_WRONLY | O_CREAT, 0744);
                     // call dup2() to change devnull to pt where stdout points
                     result = dup2(output_fd, 1); // stdout == 1
                  }
                  if(execvp(args[0], args) < 0) { // call execvp
                     // if it returns,there was an error
                     perror("incorrect command");
                     exit(1);
                  }
                  break;
               default: // in background parent...
                  // shell will print pid of bgr process when it begins
                  printf("background pid is %d\n", spawnpid);
                  children[num_procs] = spawnpid;
                  num_procs++;
                  // shell will not wait for background commands to complete
                  if (WIFEXITED(childExitMethod != 0)) {
                     exit_status = WEXITSTATUS(childExitMethod);
                  } // if here, not exited normally
                  else if (WIFSIGNALED(childExitMethod) != 0) {
                  // the process was terminated by a signal
                     term_signal = WTERMSIG(childExitMethod);
                     printf("terminated by signal %d\n", term_signal);
                  }
                  break;
            }
         }
      }

      // loop over arr of pids and waitpid with nohang to clean up zombies...
      int temp = 0;
      for (i = 0; i < num_procs; i++) {
         if (children[i] != -5) { // if child is not the flag-set reset child...
            temp = waitpid(children[i], &childExitMethod, WNOHANG);
         }
         // if the process was completed, display pid & exit value/sig if exited
         if (temp == children[i]) {
            printf("process %d completed\n", children[i]);
            if (WIFEXITED(childExitMethod != 0)) {
               exit_status = WEXITSTATUS(childExitMethod);
               printf("exit value %d\n", exit_status);
            } // if exited by signal instead
            else if (WIFSIGNALED(childExitMethod) != 0) {
               term_signal = WTERMSIG(childExitMethod);
               printf("term sig was %d\n", term_signal);
            }
            children[i] = -5; // flag for resetting finished child
         }
      }

   } while (!shell_exit);
   return 0;
}
