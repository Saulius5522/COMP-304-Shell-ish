Saulius Mickevicius
Repository: https://github.com/Saulius5522/COMP-304-Shell-ish

Shell-ish is a Unix style shell written in C language.

It features:
Part 1:
forking and executing the programs as its own children processe
Shell supports background execution of programs. An ampersand (&) at the
end of the command line indicates that the shell should return the command line prompt
immediately after launching that program.
This shell uses execv() system call (instead of execvp()) to execute common Linux programs
(e.g. ls, mkdir, cp, mv, date, gcc, and many others) and user programs by the child
process. 

Part 2:

Implementation of I/O redirection - for I/O redirection if the redirection character is >, the output file is created if it does not
exist and truncated if it does. For the redirection symbol >> the output file is created
if it does not exist and appended otherwise. The < character means that input is read
from a file.

Implemented piping, but the implementation only covers 2 instances of pipes - longer chains, which were supposed to be implemented recursively, were not implemented.

Part 3:
Implemented cut: an application similar to
UNIX’s cut command. An example of how to use it would be: cat /etc/passwd | cut -d ":" -f1,6.
By default, input lines are assumed to be separated by a TAB character. The
program supports the option -d, --delimiter, which, when followed by a single
character, specifies the delimiter to be used instead of TAB. It also supports the option
-f, --fields, which accepts a comma-separated list of field indices (e.g., 1,3,10).

Implementation of chatroom:
Each user is represented by a
named pipe with their name, and each room is represented by a folder which would contain
the named pipes of the users who joined it. To send a message, a user will write to all named
pipes within the same room. For one to join the chat, one must type "chatroom <roomname> <username>".

Custom command cleanuptxt:
This command is used to traverse through the current folder and delete instances of empty text files that end in ".txt". To use it correctly,
you must choose a folder and run "cleanuptxt" in $shellish.
