# TODO
- simple text editor
  - paste (`<C+v>`), copy (`<C+v>`), and cut (`<C+x>`) shortcuts
  - arrow key navigation
  - `<ESC>` key for exiting editor
  - `<C+s>` for saving file changes
- tag system
  - `tag <file> <tag>` command
    - adds a tag that can be searched in the `search` command by prefixing the tag name with a `#`
      - Ex: `search #notes` returns all files with the `notes` tag
  - Add section to file metadata for tags as an array of strings
- `@` search syntax
  - search by file extension
  - Add section to file metadata for file extension (defaults to `.none`)
  - Ex: `search @txt` or `search @text` returns all the files with the `text` file extension 
  - Add command for making new file extensions `associate <extension> <extension name>`
  - 2 included file extensions, `.none`/`none` and `.txt`/`text` extensions

