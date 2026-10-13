# FileSystem Practice

Full stop, this is just practice, this is not meant to have any kind of real world use, just a fun little toy I made in ~3 days.

## Compile From Source
I legit just used `clang main.c editor.c -Wall -g -o filesystem.exe` to compile this. If you have clang installed, just run this.\
If you have gcc, use `gcc main.c editor.c -Wall -g -o filesystem.exe`.\
The `-g` is just so that I get the `compile_commands.json` file for lsp support, ngl I don't even need it anymore, I just do it out of habit now.

## Startup Arguments
`-d` - enable debug messages\
`-t` - enable test cases (don't, there isn't anything special with them)

## CLI Arguments
If you want to know about all the available commands, just run the `help` command. It tells you. :P

## Built-in Text Editor
The `edit <file>` command opens a file in the built-in text editor.
Arrow keys to move the cursor.\
`<C+s>` to save, and `<ESC>` to discard unsaved changes.\
`<C+a>` to select everything, `<C+c>` copies the selection, `<C+x>` cuts it, and `<C+v>` pastes the editor's copied text.\
Typing, backspace, or delete replaces the selection.

## Concept
Okay, so every file has a max size of 256 bytes, every file name also has a max size of 256 bytes.\
The file system has the space for 10 files, these spaces have indices that can be referenced in various commands (`move`, `copy`, `info`).\
File contents and file names are both stored as null-terminated strings, so be aware of that.\
Each file also stores up to five tags and its extension metadata.\
And that's it! That is the full concept!\
Super duper simple.
