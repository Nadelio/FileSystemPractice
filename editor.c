#include "editor.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

static DWORD original_console_mode;
static HANDLE console_input;
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static struct termios original_terminal_mode;
#endif

#define TAB_WIDTH 2

enum EditorKey {
	KEY_NONE,
	KEY_ERROR,
	KEY_CHARACTER,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_UP,
	KEY_DOWN,
	KEY_HOME,
	KEY_END,
	KEY_DELETE,
	KEY_ESCAPE
};

typedef struct {
	enum EditorKey type;
	int character;
	bool shift;
} EditorInput;

typedef struct {
	char *text;
	size_t length;
	size_t capacity;
	size_t cursor;
	size_t anchor; // other end of the selection, only important while selecting
	bool selecting;
	char *clipboard;
	size_t row_offset;
	size_t col_offset;
} Editor;

typedef struct {
	char *data;
	size_t length;
	size_t capacity;
} OutputBuffer;

int enter_raw_mode(void) {
#ifdef _WIN32
	console_input = GetStdHandle(STD_INPUT_HANDLE);
	if(console_input == INVALID_HANDLE_VALUE ||
			!GetConsoleMode(console_input, &original_console_mode)) {
		errno = EIO;
		return errno;
	}

	DWORD raw_mode = original_console_mode &
			~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT);
	raw_mode |= ENABLE_WINDOW_INPUT;
	if(!SetConsoleMode(console_input, raw_mode)) {
		errno = EIO;
		return errno;
	}
#else
	if(tcgetattr(STDIN_FILENO, &original_terminal_mode) == -1) {
		return errno;
	}

	struct termios raw_mode = original_terminal_mode;
	raw_mode.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG);
	raw_mode.c_iflag &= (tcflag_t) ~(ICRNL | IXON);
	raw_mode.c_cc[VMIN] = 1;
	raw_mode.c_cc[VTIME] = 0;
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode) == -1) {
		return errno;
	}
#endif
	return 0;
}

int exit_raw_mode(void) {
#ifdef _WIN32
	if(!SetConsoleMode(console_input, original_console_mode)) {
		errno = EIO;
		return errno;
	}
#else
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_mode) == -1) {
		return errno;
	}
#endif
	return 0;
}

static EditorInput make_input(enum EditorKey type, int character, bool shift) {
	EditorInput input = { type, character, shift };
	return input;
}

static void get_terminal_size(size_t *rows, size_t *cols) {
	*rows = 24;
	*cols = 80;
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO info;
	if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
		*rows = (size_t)(info.srWindow.Bottom - info.srWindow.Top + 1);
		*cols = (size_t)(info.srWindow.Right - info.srWindow.Left + 1);
	}
#else
	struct winsize size;
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0 && size.ws_col > 0) {
		*rows = size.ws_row;
		*cols = size.ws_col;
	}
#endif
	if(*rows < 2) *rows = 2;
	if(*cols < 1) *cols = 1;
}

#ifdef _WIN32
static EditorInput read_editor_input(void) {
	// held key can arrive as one record with a repeat count.
	static EditorInput pending;
	static WORD pending_repeats = 0;
	if(pending_repeats > 0) {
		pending_repeats--;
		return pending;
	}

	for(;;) {
		INPUT_RECORD record;
		DWORD count = 0;
		if(!ReadConsoleInputW(console_input, &record, 1, &count) || count != 1) {
			return make_input(KEY_ERROR, 0, false);
		}
		if(record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
			return make_input(KEY_NONE, 0, false);
		}
		if(record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
			continue;
		}

		KEY_EVENT_RECORD *key = &record.Event.KeyEvent;
		bool shift = (key->dwControlKeyState & SHIFT_PRESSED) != 0;
		EditorInput input;
		switch(key->wVirtualKeyCode) {
			case VK_LEFT: input = make_input(KEY_LEFT, 0, shift); break;
			case VK_RIGHT: input = make_input(KEY_RIGHT, 0, shift); break;
			case VK_UP: input = make_input(KEY_UP, 0, shift); break;
			case VK_DOWN: input = make_input(KEY_DOWN, 0, shift); break;
			case VK_HOME: input = make_input(KEY_HOME, 0, shift); break;
			case VK_END: input = make_input(KEY_END, 0, shift); break;
			case VK_DELETE: input = make_input(KEY_DELETE, 0, false); break;
			case VK_ESCAPE: input = make_input(KEY_ESCAPE, 0, false); break;
			default:
				if(key->uChar.UnicodeChar == 0) {
					continue; // modifier keys and other non-alphanum keys
				}
				input = make_input(KEY_CHARACTER, key->uChar.UnicodeChar, false);
				break;
		}

		if(key->wRepeatCount > 1) {
			pending = input;
			pending_repeats = key->wRepeatCount - 1;
		}
		return input;
	}
}
#else
static bool read_byte(unsigned char *byte) {
	ssize_t count;
	do {
		count = read(STDIN_FILENO, byte, 1);
	} while(count == -1 && errno == EINTR);
	return count == 1;
}

static bool input_ready(long microseconds) {
	fd_set input_set;
	FD_ZERO(&input_set);
	FD_SET(STDIN_FILENO, &input_set);
	struct timeval timeout = { 0, microseconds };
	return select(STDIN_FILENO + 1, &input_set, NULL, NULL, &timeout) > 0;
}

static EditorInput read_editor_input(void) {
	unsigned char character;
	if(!read_byte(&character)) {
		return make_input(KEY_ERROR, 0, false);
	}
	if(character != 27) {
		return make_input(KEY_CHARACTER, character, false);
	}
	if(!input_ready(30000)) {
		return make_input(KEY_ESCAPE, 0, false);
	}

	// ansi escapes look like \1b [ <params> <final> or \1b O <final>
	unsigned char introducer;
	if(!read_byte(&introducer) || (introducer != '[' && introducer != 'O')) {
		return make_input(KEY_NONE, 0, false);
	}

	char params[16];
	size_t param_length = 0;
	unsigned char final;
	for(;;) {
		if(!read_byte(&final)) {
			return make_input(KEY_ERROR, 0, false);
		}
		if(final >= 0x40 && final <= 0x7E) {
			break;
		}
		if(param_length < sizeof(params) - 1) {
			params[param_length++] = (char)final;
		}
	}
	params[param_length] = '\0';

	int modifier = 1;
	char *separator = strchr(params, ';');
	if(separator != NULL) {
		modifier = atoi(separator + 1);
	}
	bool shift = modifier >= 2 && ((modifier - 1) & 1);

	switch(final) {
		case 'A': return make_input(KEY_UP, 0, shift);
		case 'B': return make_input(KEY_DOWN, 0, shift);
		case 'C': return make_input(KEY_RIGHT, 0, shift);
		case 'D': return make_input(KEY_LEFT, 0, shift);
		case 'H': return make_input(KEY_HOME, 0, shift);
		case 'F': return make_input(KEY_END, 0, shift);
		case '~':
			switch(atoi(params)) {
				case 1: case 7: return make_input(KEY_HOME, 0, shift);
				case 4: case 8: return make_input(KEY_END, 0, shift);
				case 3: return make_input(KEY_DELETE, 0, false);
			}
			break;
	}
	return make_input(KEY_NONE, 0, false);
}
#endif

static size_t line_start(const char *text, size_t cursor) {
	while(cursor > 0 && text[cursor - 1] != '\n') {
		cursor--;
	}
	return cursor;
}

static size_t line_end(const char *text, size_t length, size_t cursor) {
	while(cursor < length && text[cursor] != '\n') {
		cursor++;
	}
	return cursor;
}

static size_t cursor_row(const char *text, size_t cursor) {
	size_t row = 0;
	for(size_t i = 0; i < cursor; i++) {
		if(text[i] == '\n') {
			row++;
		}
	}
	return row;
}

static size_t display_column(const char *text, size_t cursor) {
	size_t column = 0;
	for(size_t i = line_start(text, cursor); i < cursor; i++) {
		column += text[i] == '\t' ? TAB_WIDTH - column % TAB_WIDTH : 1;
	}
	return column;
}

static bool selection_range(const Editor *editor, size_t *start, size_t *end) {
	if(!editor->selecting || editor->anchor == editor->cursor) {
		return false;
	}
	*start = editor->anchor < editor->cursor ? editor->anchor : editor->cursor;
	*end = editor->anchor < editor->cursor ? editor->cursor : editor->anchor;
	return true;
}

static void output_append(OutputBuffer *output, const char *data, size_t length) {
	if(output->data == NULL) {
		return;
	}
	if(output->length + length > output->capacity) {
		size_t new_capacity = (output->capacity + length) * 2;
		char *grown = (char *)realloc(output->data, new_capacity);
		if(grown == NULL) {
			free(output->data);
			output->data = NULL;
			return;
		}
		output->data = grown;
		output->capacity = new_capacity;
	}
	memcpy(output->data + output->length, data, length);
	output->length += length;
}

static void output_format(OutputBuffer *output, const char *format, size_t a, size_t b) {
	char sequence[48];
	int length = snprintf(sequence, sizeof(sequence), format, (unsigned long)a, (unsigned long)b);
	if(length > 0) {
		output_append(output, sequence, (size_t)length);
	}
}

static void set_highlight(OutputBuffer *output, bool *highlighted, bool wanted) {
	if(*highlighted != wanted) {
		output_append(output, wanted ? "\x1b[7m" : "\x1b[27m", wanted ? 4 : 5);
		*highlighted = wanted;
	}
}

static void render_editor(Editor *editor) {
	size_t rows, cols;
	get_terminal_size(&rows, &cols);
	size_t text_rows = rows - 1; // last row is reserved for the keybind bar

	size_t row = cursor_row(editor->text, editor->cursor);
	size_t column = display_column(editor->text, editor->cursor);
	if(row < editor->row_offset) {
		editor->row_offset = row;
	} else if(row >= editor->row_offset + text_rows) {
		editor->row_offset = row - text_rows + 1;
	}
	if(column < editor->col_offset) {
		editor->col_offset = column;
	} else if(column >= editor->col_offset + cols) {
		editor->col_offset = column - cols + 1;
	}

	size_t selection_start = 0, selection_end = 0;
	bool has_selection = selection_range(editor, &selection_start, &selection_end);

	OutputBuffer output = { (char *)malloc(4096), 0, 4096 };
	output_append(&output, "\x1b[?25l", 6);

	size_t position = 0;
	for(size_t skipped = 0; skipped < editor->row_offset; skipped++) {
		position = line_end(editor->text, editor->length, position) + 1;
	}

	for(size_t screen_row = 0; screen_row < text_rows; screen_row++) {
		output_format(&output, "\x1b[%lu;%luH", screen_row + 1, 1);
		bool highlighted = false;
		if(position <= editor->length) {
			size_t display = 0;
			for(; position < editor->length && editor->text[position] != '\n'; position++) {
				bool selected = has_selection && position >= selection_start && position < selection_end;
				size_t width = editor->text[position] == '\t' ? TAB_WIDTH - display % TAB_WIDTH : 1;
				for(size_t cell = 0; cell < width; cell++, display++) {
					if(display >= editor->col_offset && display < editor->col_offset + cols) {
						set_highlight(&output, &highlighted, selected);
						output_append(&output, editor->text[position] == '\t' ? " " : &editor->text[position], 1);
					}
				}
			}
			// show a selected newline as a highlighted cell so multi-line selections are visible.
			if(position < editor->length && has_selection &&
					position >= selection_start && position < selection_end &&
					display >= editor->col_offset && display < editor->col_offset + cols) {
				set_highlight(&output, &highlighted, true);
				output_append(&output, " ", 1);
			}
			position++; // skip newline (or step past the end of the text)
		}
		set_highlight(&output, &highlighted, false);
		output_append(&output, "\x1b[K", 3);
	}

	static const char keybinds[] =
			" <C+s> save | <ESC> cancel | shift + arrow keys select | <C+a> select all"
			" | <C+c> copy | <C+x> cut | <C+v> paste";
	size_t keybind_length = sizeof(keybinds) - 1;
	if(keybind_length > cols - 1) {
		keybind_length = cols - 1;
	}
	output_format(&output, "\x1b[%lu;%luH\x1b[7m", rows, 1);
	output_append(&output, keybinds, keybind_length);
	for(size_t i = keybind_length; i + 1 < cols; i++) {
		output_append(&output, " ", 1);
	}
	output_append(&output, "\x1b[27m\x1b[K", 8);

	output_format(&output, "\x1b[%lu;%luH", row - editor->row_offset + 1, column - editor->col_offset + 1);
	output_append(&output, "\x1b[?25h", 6);

	if(output.data != NULL) {
		fwrite(output.data, 1, output.length, stdout);
		free(output.data);
	}
	fflush(stdout);
}

static void delete_range(Editor *editor, size_t start, size_t end) {
	memmove(editor->text + start, editor->text + end, editor->length - end + 1);
	editor->length -= end - start;
	editor->cursor = start;
	editor->selecting = false;
}

static bool delete_selection(Editor *editor) {
	size_t start, end;
	if(!selection_range(editor, &start, &end)) {
		editor->selecting = false;
		return false;
	}
	delete_range(editor, start, end);
	return true;
}

/// inserts text at the cursor, replacing the selection if there is one.
static void insert_text(Editor *editor, const char *text, size_t text_length) {
	size_t start, end;
	size_t selected = selection_range(editor, &start, &end) ? end - start : 0;
	if(text_length > editor->capacity - 1 - (editor->length - selected)) {
		putchar('\a');
		return;
	}

	delete_selection(editor);
	memmove(editor->text + editor->cursor + text_length,
			editor->text + editor->cursor, editor->length - editor->cursor + 1);
	memcpy(editor->text + editor->cursor, text, text_length);
	editor->cursor += text_length;
	editor->length += text_length;
}

static bool copy_selection(Editor *editor) {
	size_t start, end;
	if(!selection_range(editor, &start, &end)) {
		putchar('\a');
		return false;
	}
	memcpy(editor->clipboard, editor->text + start, end - start);
	editor->clipboard[end - start] = '\0';
	return true;
}

static void move_cursor(Editor *editor, enum EditorKey key, bool shift) {
	if(shift) {
		if(!editor->selecting) {
			editor->anchor = editor->cursor;
			editor->selecting = true;
		}
	} else if(editor->selecting) {
		size_t start, end;
		bool has_selection = selection_range(editor, &start, &end);
		editor->selecting = false;
		if(has_selection && (key == KEY_LEFT || key == KEY_RIGHT)) {
			editor->cursor = key == KEY_LEFT ? start : end;
			return;
		}
	}

	const char *text = editor->text;
	size_t start = line_start(text, editor->cursor);
	size_t end = line_end(text, editor->length, editor->cursor);
	size_t column = editor->cursor - start;

	if(key == KEY_LEFT && editor->cursor > 0) {
		editor->cursor--;
	} else if(key == KEY_RIGHT && editor->cursor < editor->length) {
		editor->cursor++;
	} else if(key == KEY_HOME) {
		editor->cursor = start;
	} else if(key == KEY_END) {
		editor->cursor = end;
	} else if(key == KEY_UP && start > 0) {
		size_t previous_end = start - 1;
		size_t previous_start = line_start(text, previous_end);
		size_t previous_length = previous_end - previous_start;
		editor->cursor = previous_start + (column < previous_length ? column : previous_length);
	} else if(key == KEY_DOWN && end < editor->length) {
		size_t next_start = end + 1;
		size_t next_end = line_end(text, editor->length, next_start);
		size_t next_length = next_end - next_start;
		editor->cursor = next_start + (column < next_length ? column : next_length);
	}
}

int edit_text(char *content, size_t capacity) {
	if(content == NULL || capacity == 0) {
		errno = EINVAL;
		return -1;
	}

	size_t length = strlen(content);
	if(length >= capacity) {
		errno = EOVERFLOW;
		return -1;
	}

	Editor editor = { 0 };
	editor.text = (char *)malloc(capacity);
	editor.clipboard = (char *)malloc(capacity);
	if(editor.text == NULL || editor.clipboard == NULL) {
		free(editor.text);
		free(editor.clipboard);
		return -1;
	}
	memcpy(editor.text, content, length + 1);
	editor.length = length;
	editor.capacity = capacity;
	editor.cursor = length;
	editor.clipboard[0] = '\0';

	int raw_mode_error = enter_raw_mode();
	if(raw_mode_error != 0) {
		free(editor.text);
		free(editor.clipboard);
		errno = raw_mode_error;
		perror("Could not enter editor input mode");
		return -1;
	}

	// alternate screen buffer keeps the editor UI out of the shell's primary buffer.
	printf("\x1b[?1049h\x1b[2J");
	render_editor(&editor);

	int result = 0;
	bool input_error = false;
	for(;;) {
		EditorInput input = read_editor_input();
		if(input.type == KEY_ESCAPE) {
			result = 0;
			break;
		}
		if(input.type == KEY_ERROR) {
			input_error = true;
			result = -1;
			break;
		}

		if(input.type == KEY_DELETE) {
			if(!delete_selection(&editor) && editor.cursor < editor.length) {
				delete_range(&editor, editor.cursor, editor.cursor + 1);
			}
		} else if(input.type == KEY_CHARACTER) {
			int character = input.character;
			if(character == 19) { // <C+s>
				result = 1;
				break;
			} else if(character == 1) { // <C+a>
				editor.anchor = 0;
				editor.cursor = editor.length;
				editor.selecting = true;
			} else if(character == 3) { // <C+c>
				copy_selection(&editor);
			} else if(character == 24) { // <C+x>
				if(copy_selection(&editor)) {
					delete_selection(&editor);
				}
			} else if(character == 22) { // <C+v>
				insert_text(&editor, editor.clipboard, strlen(editor.clipboard));
			} else if(character == 8 || character == 127) { // backspace
				if(!delete_selection(&editor) && editor.cursor > 0) {
					delete_range(&editor, editor.cursor - 1, editor.cursor);
				}
			} else if(character == '\r' || character == '\n') {
				insert_text(&editor, "\n", 1);
			} else if(character == '\t' || (character >= 32 && character <= 126)) {
				char typed = (char)character;
				insert_text(&editor, &typed, 1);
			}
		} else if(input.type != KEY_NONE) {
			move_cursor(&editor, input.type, input.shift);
		}
		render_editor(&editor);
	}

	printf("\x1b[?1049l");
	fflush(stdout);
	if(exit_raw_mode() != 0) {
		perror("Could not restore terminal input mode");
		result = -1;
	}
	if(input_error) {
		errno = EIO;
		perror("Could not read editor input");
	}
	if(result == 1) {
		memcpy(content, editor.text, editor.length + 1);
	}
	free(editor.text);
	free(editor.clipboard);
	return result;
}
