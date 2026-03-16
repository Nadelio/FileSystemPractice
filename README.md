# FileSystem Practice

Full stop, this is just practice, this is not meant to have any kind of real world use, just a fun little toy I made in ~3 days.

## Compile From Source
I legit just used `clang main.c -Wall -g -o filesystem.exe` to compile this. So if you have clang installed, just run this.\
I don't know how I would compile this using `gcc`, but it is a single file, I am sure you can figure it out.\
The `-g` is just so that I get the `compile_commands.json` file for lsp support, ngl I don't even need it anymore, I just do it out of habit now.

## Startup Arguments
`-d` - enable debug messages\
`-t` - enable test cases (don't, there isn't anything special with them)

## CLI Arguments
If you want to know about all the available commands, just run the `help` command. It tells you. :P

## Concept
Okay, so every file has a max size of 256 bytes, every file name also has a max size of 256 bytes.\
The file system has the space for 10 files, these spaces have indices that can be referenced in various commands (`move`, `copy`, `info`).\
File contents and file names are both stored as null-terminated strings, so be aware of that.\
And that's it! That is the full concept!\
Super duper simple.
