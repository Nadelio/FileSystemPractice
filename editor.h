#ifndef FILESYSTEM_EDITOR_H
#define FILESYSTEM_EDITOR_H

#include <stddef.h>

int enter_raw_mode(void);
int exit_raw_mode(void);

/*
 * Edits content in a temporary buffer. Ctrl+S copies saved changes back to
 * content and returns 1; Escape discards unsaved changes and returns 0.
 * Returns -1 if the terminal cannot be put into raw mode.
 */
int edit_text(char *content, size_t capacity);

#endif
