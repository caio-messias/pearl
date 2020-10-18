# Compiling and running

Compile with gcc and run as an executable. Here we are using the most common warning flags to preserve sanity while programming in C.

```
> gcc -Wall -Wextra -Werror -pedantic pearl.c -o pearl
> ./pearl
```

# Documentation

## What is a shell?
//todo

## Basic shell loop
The basic shell loop is as follows:

1. read an input line
2. parse the input into commands, arguments and redirections
3. execute the commands
4. goto 1

The reading and parsing can done by standard C functions such as `fgets`, `getline`, `strtok_r` and so on. To execute the commands we'll need some system calls, namely `execvp` and `fork`.

## Executing single commands

The `execvp(const char *file, const char *argv[])` sys call executes a given file with it's arguments. The first argument, `file`, can be either a filename or a filepath. If it's a filename, `execvp` searches the user path for a file matching it. For example, if we had `ls` as the file, `execvp` would search the PATH variable (by default it is at least `/bin:/usr/bin`) and the file `/bin/ls` would be matched and executed. If `file` is actually an filepath, such as `/usr/local/bin/ls`, then the file located at that full path would be executed, if available. The second argument, `argv`, is an array of strings containing the arguments to the command. By Unix convention, the filename (but not the path) is included as the first element of the `argv` array. As an example of how to call execvp, we have `execvp("ls", {"ls", "-l", "-a"})`

With `execvp` we can execute any file from our shell, but there is a problem: when `execvp` executes a file it replaces the entire code of the current process with the code of the given file, meaning that the shell loop would be replaced by the first command that the user types and we'll only be able to run one command ever. One way of fixing this is to create a new child process for each new command, and replace the code of that process with the command the user typed, while the parent process continue to run the shell loop. To do so we can use the `fork` syscall. This function creates a new child process running the same code as the father as returns the process id (`pid`) of the newly created process. When we call this function we'll have two processes running the same code, but we can know which is the child and which is the parent by looking at the returned `pid`. In the parent the `pid` will be always bigger than 0, while in the child it will be zero. 

The following is an example to illustrate how to use `execvp` alongside `fork` and is the logic used by our shell to run commands:

```C
int run_command(const char* argv[]) {
	pid_t pid = fork();
	if (pid == -1)
		return -1;
	
if (pid == 0) {
	// This is running in the child. 
	// execvp will replace this process code with argv and run it.
	execvp(argv[0], argv);
	
	// Since execvp should replace this process code, anything below this line 
	// should not run, unless there was an error with execvp;
	printf("failed running the command: %s", argv[0]);
	return -1;
}

// This is the parent. It will wait for it's child to finish executing 
// and return to the shell loop, to read another command.
wait(pid);
return 0;
```

## Input and output redirection
Now that we have a basic shell that is capable of running single commands and their arguments, it is useful to add input and output redirection. 

Input redirection means that instead of reading from the standard input (`stdin`, ie. the terminal), a command can read from a file. This can be done with the `<` character. For example: `sort < myfile` will sort the contents of `myfile` and print it in the console screen. 

Output redirection is similar and uses the `>` character. Instead of writing to the standard output (`stdout`, ie. also the terminal), a program can write to a file. For example: `ls > myfile` will write it's output into `myfile`. If `myfile` doesn't exist it will create it, and if it does exist the command will overwrite any content that was previously there. There is a second way of output redirection, called appending, that uses the `>>` character. This form differs from the usual truncate operation (with `>`) in that if the file already exists it then writes at the end of it, rather than overwriting. No content is lost this way. Input and output redirection and also be used together: `sort < myfile > mysortedfile`.

To implement redirection we first need to know about file descriptors (`fd`) and permissions. A file descriptor is a positive integer that identifies an open file in the system and is done by processes. It is the return value of the `open(const char *pathname, int flags, mode_t mode)` syscall. To write to a file, we use this `fd` in the write sys call: `write(fd, str, sizeof(str))`. Considering we need to open a file to write to it, it makes sense that a write functions uses an `fd` rather than a filename.

But there is a problem: we could use the open/read functions to read from an specific file, but we also want to retain the option of read from `stdin` and we don't want to change the source code of any program. To implement a redirection we can use the `dup2(int fd, int fd2)` sycall. This function makes `fd2` point to the same file as `fd`. If the were to read from `fd2`, we would end up reading from `fd` instead. Since most of programs expect to read from `stdin`, we can trick them to read from a file we want by calling: `dup2(fd, stdin)`. 

Here is an example of how to to this: reading from a file if a filename was given, or else reading from `stdin` as usual.
```C
int in = STDIN_FILENO
if (strcmp(filename, "") != 0 {
	in = open(filename)
}

dup2(in, STDIN_FILENO);
```

To write to a file we'll need some extra checking because truncating (`>`) and appending (`>>`) need different file flag options. Before opening a file in the system we must first say how we plan to use it. Will we read from it or write to it? If writing to it, should we truncate or append? What to do if the file does not exist? For each of these options there is a different flag we can set to use in both `open` and `write` syscalls.

* O_RDONLY to open a file in read mode
* O_WRONLY to open a file in write mode
* O_CREATE to create the file if it doesn't exist
* O_TRUNC or O_APPEND to trucante output or append it at the end of the file, respectively

Each of these options are represented by a single bit in the `flags` variable, and can be combined by using a bitwise OR operation. For example, to open a file in write mode, appending to it and creating it if it doesn't exist we could use:

```C
int flags = O_WRONLY | O_CREATE | O_APPEND;
```

Beside flags, we must also set the permissions correctly to properly use a file. There are three types of permissions: to read, to write and to execute. There can be given to 3 entities: the user who created the file (also called the owner of the file), a group and others. Since in our shell we are only ever reading and writing and not executing, we can give read and write permissions to the owner of the file and read permission to the rest.

```C
int file_perm = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
```

A permission is written in the form of `S_Ixyyy`, where `x` is the permission (`R` for reading, `W` for writing and `X` for executing) and `yyy` is the entity (`USR` for user, `GRP` for group and `OTH` for others).

## Piping
Piping is a form of interprocess communication. This lies at the heart of the Unix philosophy: creating small and composable programs, where the output of one program is the input of another. A pipe is composed of two sides: one for reading and one for writing. To create a pipe we use the `int pipe(pfd[2])` syscall. This function returns an array with two `fd`s, representing the two sides of the pipe, where `pfd[0]` is the reading end and `pfd[1]` is the writing end. When all writing ends are closed, readers will interpret this as a end-of-file. And if all reading ends are closed, a write will cause a fatal error.

There is a problem, though: just having access to a file descriptor does not make a process able to write to it. The `fd` must be valid in that process and this validation is done internally by the operating system. We would either have to open the file in that process for it to become valid or the process would have to inherit the file descriptor, via a `fork` syscall, as a child process inherit all open files from its parent. To connect a parent and a child process, then, we must always create the pipe before forking.

We can then connect two processes by using the same technique used when redirecting input:

```C
char s[100];
int pfd[2];
pipe(pfd[2]);
pid_t pid = fork();

if (pid == 0) { /* Running in the child process */
	// The child should not write to its parents, 
	// as pipes are supposed to be unidirectional and go from left to right.
	close(pfd[1]);

	// Read from the pipe.
	read(pfd[0], s, sizeof(s));
}

// Running in the parent process:
close(pfd[0])
write(pfd[1], "hello", 6)

// Wait for the child to finish reading.
waitpid(pid, NULL, 0)
```

# References:

- ["Write a Shell in C" by Stephen Brennan](https://brennan.io/2015/01/16/write-a-shell-in-c/) (for the basic shell structure and loop)
- [“OMG building a shell in 10 minutes" by Stefanie Schirmer](https://www.youtube.com/watch?v=k6TTj4C0LF0) (for the basic shell structure and loop)
- Advanced Unix Programming second edition by Marc J. Rochkind (for input and output redirection, file modes and permissions, piping)
