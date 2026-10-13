#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include "editor.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#define MAX_NUM_FILES 10
#define MAX_FILE_NAME_SIZE 256
#define MAX_FILE_SIZE 256
#define MAX_TAG_COUNT 5
#define MAX_TAG_NAME_LENGTH 256
#define MAX_FILE_EXTENSION_LENGTH 256
#define MAX_EXTENSION_ALIAS_LENGTH MAX_FILE_EXTENSION_LENGTH
#define MAX_DEFINED_FILE_ASSOCIATIONS 5
#define MAX_COMMAND_LENGTH 20

#define SUCCESS true
#define FAILURE false

bool copy_string(char* destination, size_t destination_size, const char* source) {
	if(destination == NULL || source == NULL || destination_size == 0) {
		return FAILURE;
	}

	size_t source_length = strlen(source);
	if(source_length >= destination_size) {
		return FAILURE;
	}

	memcpy(destination, source, source_length + 1);
	return SUCCESS;
}

bool append_string(char* destination, size_t destination_size, const char* source) {
	if(destination == NULL || source == NULL || destination_size == 0) {
		return FAILURE;
	}

	size_t destination_length = strlen(destination);
	size_t source_length = strlen(source);
	if(destination_length >= destination_size || source_length >= destination_size - destination_length) {
		return FAILURE;
	}

	memcpy(destination + destination_length, source, source_length + 1);
	return SUCCESS;
}

char* next_token(char** cursor, const char* delimiters) {
	if(cursor == NULL || *cursor == NULL || delimiters == NULL) {
		return NULL;
	}

	char* token_start = *cursor + strspn(*cursor, delimiters);
	if(*token_start == '\0') {
		*cursor = token_start;
		return NULL;
	}

	char* token_end = token_start + strcspn(token_start, delimiters);
	if(*token_end != '\0') {
		*token_end = '\0';
		*cursor = token_end + 1;
	} else {
		*cursor = token_end;
	}

	return token_start;
}

// check if strings are equal
bool streql(char* str_1, char* str_2) {
	return strncmp(str_1, str_2, strlen(str_2)) == 0;
}

bool is_debug = false;
bool do_tests = false;


void print_debug(char* message, ...) {
	if(!is_debug) return;
	va_list args;
	va_start(args, message);
	printf("[DEBUG] ");
	vprintf(message, args);
	printf("\n");
	va_end(args);
}

void print_system(char* message, ...) {
	va_list args;
	va_start(args, message);
	printf("[SYSTEM] ");
	vprintf(message, args);
	printf("\n");
	va_end(args);
}

void print_error(char* message, ...) {
	va_list args;
	va_start(args, message);
	printf("[ERROR] ");
	vprintf(message, args);
	printf("\n");
	va_end(args);

}

typedef struct {
	char name[MAX_FILE_NAME_SIZE]; // Max size is 256 bytes
	size_t size; // Size of file in bytes
	char tags[MAX_TAG_COUNT][MAX_TAG_NAME_LENGTH]; // file tags
	char extension[MAX_FILE_EXTENSION_LENGTH]; // file extension (NOT THE ALIAS)
	char content[MAX_FILE_SIZE]; // Max size is 256 bytes
} File;

// A file type and its alias
typedef struct {
	char alias[MAX_EXTENSION_ALIAS_LENGTH];
	char extension[MAX_FILE_EXTENSION_LENGTH];
} FileAssociation;

/// files is the array containing the actual files.
/// occupied is for creating new files.
typedef struct { 
	File* files[MAX_NUM_FILES];
	bool occupied[MAX_NUM_FILES]; 
	FileAssociation file_extensions[MAX_DEFINED_FILE_ASSOCIATIONS];
} FileSystem;

File zeroed_file;

bool initialize_file_system(FileSystem* fs) {
	if(fs == NULL) {
		return FAILURE;
	}

	memset(fs, 0, sizeof(*fs));
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		fs->files[i] = &zeroed_file;
	}

	if(!copy_string(fs->file_extensions[0].alias, sizeof(fs->file_extensions[0].alias), "none") ||
			!copy_string(fs->file_extensions[0].extension, sizeof(fs->file_extensions[0].extension), ".none") ||
			!copy_string(fs->file_extensions[1].alias, sizeof(fs->file_extensions[1].alias), "text") ||
			!copy_string(fs->file_extensions[1].extension, sizeof(fs->file_extensions[1].extension), ".txt")) {
		print_error("Could not initialize built-in file extensions.");
		return FAILURE;
	}

	return SUCCESS;
}

int find_extension_association(const FileSystem* fs, const char* extension_or_alias) {
	if(fs == NULL || extension_or_alias == NULL) {
		return -1;
	}

	for(int i = 0; i < MAX_DEFINED_FILE_ASSOCIATIONS; i++) {
		const FileAssociation* association = &fs->file_extensions[i];
		if(association->extension[0] == '\0') {
			continue;
		}
		if(strcmp(association->alias, extension_or_alias) == 0 ||
				strcmp(association->extension, extension_or_alias) == 0) {
			return i;
		}

		if(extension_or_alias[0] != '.' &&
				strcmp(association->extension + 1, extension_or_alias) == 0) {
			return i;
		}
	}

	return -1;
}

bool set_file_extension(const FileSystem* fs, File* file, const char* file_name) {
	const char* dot = strrchr(file_name, '.');
	const char* extension = ".none";
	if(dot != NULL && dot[1] != '\0') {
		int association = find_extension_association(fs, dot);
		if(association >= 0) {
			extension = fs->file_extensions[association].extension;
		}
	}

	if(!copy_string(file->extension, sizeof(file->extension), extension)) {
		print_error("Could not store the extension for %s.", file_name);
		return FAILURE;
	}
	return SUCCESS;
}

/// Returns the next open position in the given FileSystem, if none are open, returns -1
int get_next_position(const FileSystem* fs) {
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(!fs->occupied[i]) { return i; }
	}
	return -1;
}

/// Returns the total number of occupied spaces in the filesystem
size_t get_total_occupied(const FileSystem* fs) {
	int count = 0;
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(fs->occupied[i]) count++;
	}
	return count;
}

/// Returns the position of the file with the matching name, if no file with name exists, returns -1
int find_file_position(const FileSystem* fs, char* file_name) {
	int total_occupied = get_total_occupied(fs);
	print_debug("Total occupied spaces: %d", total_occupied);
	
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(fs->occupied[i]) { 
			print_debug("%d | %s", i, fs->files[i]->name); 
			if(strcmp(fs->files[i]->name, file_name) == 0) {
				print_debug("%s equals %s, %s's position: %d", file_name, fs->files[i]->name, fs->files[i]->name, i);
				return i;
			}
		} else { 
			print_debug("%d | Empty", i); 
		}
	}
	
	print_error("The file %s does not exist", file_name);
	return -1;
}

/// Returns an integer between 0 and 9 (inclusive)
/// Returns -1 if the string couldn't be converted or there is a partial conversion
int string_to_clamped_int(char* str) {
	char* endptr;
	size_t final_position = strtol(str, &endptr, 10);
	if(endptr == str) {
		print_error("Couldn't convert %s to an integer.", str);
		return -1; 
	} else if(*endptr != '\0') {
		print_error("Partial conversion of %s.", str);
		return -1;
	}

	// clamp final position
	if(final_position > 9) { // max index of fs->files[]
		print_system("Clamped given position of %d to 9.", final_position);
		final_position = 9;
	} else if(final_position < 0) { // just in case strtol() supports negative integers
		print_system("Clamped given position of %d to 0.", final_position);
		final_position = 0;
	}
	
	return final_position;
}

bool file_name_exists(const FileSystem* fs, const char* file_name, int ignored_position) {
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(i != ignored_position && fs->occupied[i] && strcmp(fs->files[i]->name, file_name) == 0) {
			return true;
		}
	}
	return false;
}

/// Writes a name that no other file uses into output (ignoring the file at ignored_position).
/// Duplicates get "(n)" inserted before the extension, e.g. notes.txt -> notes(1).txt.
/// Returns false if the resulting name would not fit in output_size.
bool make_unique_file_name(const FileSystem* fs, const char* file_name, int ignored_position, char* output, size_t output_size) {
	if(!file_name_exists(fs, file_name, ignored_position)) {
		if(!copy_string(output, output_size, file_name)) {
			print_error("File name is too long: %s", file_name);
			return FAILURE;
		}
		return SUCCESS;
	}

	// split at the same dot set_file_extension uses so the extension survives the suffix
	const char* dot = strrchr(file_name, '.');
	if(dot != NULL && dot[1] == '\0') {
		dot = NULL;
	}
	size_t base_length = dot != NULL ? (size_t)(dot - file_name) : strlen(file_name);
	const char* extension = dot != NULL ? dot : "";

	// at most MAX_NUM_FILES names can be taken, so a free suffix is always found within this range
	for(int n = 1; n <= MAX_NUM_FILES; n++) {
		int length = snprintf(output, output_size, "%.*s(%d)%s", (int)base_length, file_name, n, extension);
		if(length < 0 || (size_t)length >= output_size) {
			print_error("Cannot make %s unique without exceeding the file name limit of %d characters.", file_name, (int)output_size - 1);
			return FAILURE;
		}
		if(!file_name_exists(fs, output, ignored_position)) {
			return SUCCESS;
		}
	}

	print_error("Could not find a unique name for %s.", file_name);
	return FAILURE;
}

/// Creates a file and returns a true/false based on success/failure.
/// file_size is the size of the file in bytes.
/// The max size of a file is 256 bytes (for simplicity)
bool create_file(FileSystem* fs, char* file_name, size_t file_size, char* content) {
	print_debug("Creating File...");
	int next_open_position = get_next_position(fs);

	if(next_open_position < 0) {
		print_error("No more free space. Delete a file before creating a new one.");
		return FAILURE;
	}

	File* f = (File*)(malloc(sizeof(File)));

	if(f == NULL) {
		print_error("Could not allocate memory for %s.", file_name);
		return FAILURE;
	}

	if(!make_unique_file_name(fs, file_name, -1, f->name, sizeof(f->name))) {
		free(f);
		return FAILURE;
	}
	
	if(file_size <= MAX_FILE_SIZE) {
		f->size = file_size;
	} else {
		print_error("%s size is greater than max file size of 256 bytes. Truncating file...\n\tFile size: %llu", file_name, file_size);
		free(f);
		return FAILURE;
	}

	print_debug("Copying contents over...");
	if(!copy_string(f->content, sizeof(f->content), content)) {
		print_error("Contents for %s exceed the file size limit.", file_name);
		free(f);
		return FAILURE;
	}
	
	print_debug("File Contents: %s, Given Contents: %s", f->content, content);

	memset(f->tags, 0, sizeof(f->tags));
	if(!set_file_extension(fs, f, file_name)) {
		free(f);
		return FAILURE;
	}

	fs->files[next_open_position] = f;
	fs->occupied[next_open_position] = true;

	print_system("Successfully created %s\n\tSize: %d\n\tPosition: %d", f->name, f->size, next_open_position);
	return SUCCESS;
}

/// Deletes the file pointed to by the given pointer.
/// Returns false if failed and true if succeeded.
bool delete_file(FileSystem* fs, char* file_name, int position) {
	print_debug("Deleting file...");

	if(position < 0 || position >= MAX_NUM_FILES) {
		print_error("Attempted to access a non-existent position in the File System.");
		return FAILURE;
	}

	if(fs->occupied[position] && strcmp(fs->files[position]->name, file_name) == 0) { // check the file at position is the named file
		print_debug("%s is at the given position: %d", file_name, position);
		free(fs->files[position]);
		fs->files[position] = &zeroed_file;
		fs->occupied[position] = false;
		print_system("File successfully deleted.");
		return SUCCESS;
	} else { // error if true position != given position
		print_error("Attempted deletion of file that is not at the given position.\n\tFile: %s,\n\tActual Position: %d,\n\tGiven Position: %d", file_name, find_file_position(fs, file_name), position);
		return FAILURE;
	}
}

/// Searches for a file in the filesystem by name
/// Prints out the information of the file.
/// Returns true if successful, returns false if no file could be found
bool search_file(const FileSystem* fs, char* query) {
	if(query[0] == '#' || query[0] == '@') {
		bool search_tags = query[0] == '#';
		const char* value = query + 1;
		if(*value == '\0') {
			print_error("Search query must include a tag or extension after %c.", query[0]);
			return FAILURE;
		}

		int association = search_tags ? -1 : find_extension_association(fs, value);
		if(!search_tags && association < 0) {
			print_error("No file extension is associated with %s.", value);
			return FAILURE;
		}

		const char* extension = search_tags ? NULL : fs->file_extensions[association].extension;
		int matches = 0;
		for(int i = 0; i < MAX_NUM_FILES; i++) {
			if(!fs->occupied[i]) {
				continue;
			}

			File* file = fs->files[i];
			bool matched = search_tags ? false : strcmp(file->extension, extension) == 0;
			if(search_tags) {
				for(int j = 0; j < MAX_TAG_COUNT; j++) {
					if(file->tags[j][0] != '\0' && strcmp(file->tags[j], value) == 0) {
						matched = true;
						break;
					}
				}
			}

			if(matched) {
				print_system("Found file: %s\n\tSize: %llu\n\tPosition: %d\n\tExtension: %s",
						file->name, file->size, i, file->extension);
				matches++;
			}
		}

		if(matches == 0) {
			print_error("No files found for search query %s.", query);
			return FAILURE;
		}
		return SUCCESS;
	}

	int position = find_file_position(fs, query);
	if(position >= 0 && fs->occupied[position]) {
		File* f = fs->files[position];
		print_system("Found file: %s\n\tSize: %llu\n\tPosition: %llu\n\tExtension: %s", f->name, f->size, position, f->extension);
		return SUCCESS;
	}
	return FAILURE;
}

bool tag_file(FileSystem* fs, char* file_name, char* tag) {
	int position = find_file_position(fs, file_name);
	if(position < 0 || !fs->occupied[position]) {
		print_error("File %s does not exist.", file_name);
		return FAILURE;
	}

	if(tag[0] == '#') {
		tag++;
	}
	if(tag[0] == '\0' || strlen(tag) >= MAX_TAG_NAME_LENGTH) {
		print_error("Tag must contain between 1 and %d characters.", MAX_TAG_NAME_LENGTH - 1);
		return FAILURE;
	}

	File* file = fs->files[position];
	int empty_slot = -1;
	for(int i = 0; i < MAX_TAG_COUNT; i++) {
		if(strcmp(file->tags[i], tag) == 0) {
			print_system("%s already has the %s tag.", file_name, tag);
			return SUCCESS;
		}
		if(file->tags[i][0] == '\0' && empty_slot < 0) {
			empty_slot = i;
		}
	}

	if(empty_slot < 0) {
		print_error("%s already has the maximum of %d tags.", file_name, MAX_TAG_COUNT);
		return FAILURE;
	}
	if(!copy_string(file->tags[empty_slot], sizeof(file->tags[empty_slot]), tag)) {
		print_error("Tag is too long to store.");
		return FAILURE;
	}

	print_system("Added the %s tag to %s.", tag, file_name);
	return SUCCESS;
}

bool associate_file_extension(FileSystem* fs, char* extension, char* alias) {
	if(extension[0] == '.') {
		extension++;
	}
	if(extension[0] == '\0' || alias[0] == '\0') {
		print_error("Extension and extension name must not be empty.");
		return FAILURE;
	}

	char normalized_extension[MAX_FILE_EXTENSION_LENGTH];
	int extension_length = snprintf(normalized_extension, sizeof(normalized_extension), ".%s", extension);
	if(extension_length < 0 || (size_t)extension_length >= sizeof(normalized_extension) ||
			strlen(alias) >= MAX_EXTENSION_ALIAS_LENGTH) {
		print_error("Extension or extension name is too long.");
		return FAILURE;
	}
	if(strchr(extension, '.') != NULL || strchr(alias, '.') != NULL) {
		print_error("Provide an extension without additional dots and an extension name without dots.");
		return FAILURE;
	}
	if(find_extension_association(fs, normalized_extension) >= 0 ||
			find_extension_association(fs, alias) >= 0) {
		print_error("The extension or extension name is already associated.");
		return FAILURE;
	}

	for(int i = 0; i < MAX_DEFINED_FILE_ASSOCIATIONS; i++) {
		if(fs->file_extensions[i].extension[0] == '\0') {
			copy_string(fs->file_extensions[i].extension, sizeof(fs->file_extensions[i].extension), normalized_extension);
			copy_string(fs->file_extensions[i].alias, sizeof(fs->file_extensions[i].alias), alias);
			for(int file_index = 0; file_index < MAX_NUM_FILES; file_index++) {
				if(fs->occupied[file_index]) {
					const char* dot = strrchr(fs->files[file_index]->name, '.');
					if(dot != NULL && strcmp(dot, normalized_extension) == 0 &&
							!set_file_extension(fs, fs->files[file_index], fs->files[file_index]->name)) {
						return FAILURE;
					}
				}
			}
			print_system("Associated .%s with %s.", extension, alias);
			return SUCCESS;
		}
	}

	print_error("No more file extension associations can be added.");
	return FAILURE;
}

/// Renames the given file to the new name
/// Returns true if successful, returns false if no file could be found
bool rename_file(FileSystem* fs, char* file_name, char* new_name) {
	int position = find_file_position(fs, file_name);
	print_debug("Position: %d", position);

	if(position >= 0 && fs->occupied[position]) {
		print_debug("Attempting to rename %s to %s.", file_name, new_name);
		char unique_name[MAX_FILE_NAME_SIZE];
		if(!make_unique_file_name(fs, new_name, position, unique_name, sizeof(unique_name))) {
			return FAILURE;
		}
		if(!set_file_extension(fs, fs->files[position], unique_name)) {
			return FAILURE;
		}
		copy_string(fs->files[position]->name, sizeof(fs->files[position]->name), unique_name);
		print_system("Successfully renamed %s to %s.", file_name, unique_name);
		return SUCCESS;
	}
	print_error("Attempted to rename non-existent file: %s.", file_name);
	return FAILURE;
}

/// Moves the given file to the given position
/// Returns true if successful, returns false if no file could be found, or the positional argument is invalid 
bool move_file(FileSystem* fs, char* file_name, char* new_position) {
	int final_position = string_to_clamped_int(new_position);
	int current_position = find_file_position(fs, file_name);
	
	if(final_position < 0) {
		print_error("Invalid position argument: %s", new_position);
		return FAILURE;
	}

	if(current_position >= 0 && fs->occupied[current_position]) { // file exists
		File* file = fs->files[current_position]; // save pointer temporarily
		
		// move pointer
		fs->files[final_position] = file;
		fs->occupied[final_position] = true;
		
		// free up previous position
		fs->occupied[current_position] = false;
		fs->files[current_position] = &zeroed_file;
		
		print_system("Successfully moved %s from %llu to %llu", file_name, current_position, final_position);
		return SUCCESS;
	}

	print_error("Attempted to move non-existent file: %s.", file_name);
	return FAILURE;
}

/// Copy file to new position, will overwrite file in copy_position
/// Returns true if successful, returns false if positional argument is invalid, no file could be found
bool copy_file(FileSystem* fs, char* file_name, char* copy_position) {
	int final_position = string_to_clamped_int(copy_position);
	int source_position = find_file_position(fs, file_name);

	if(final_position < 0) {
		print_error("Invalid position argument: %s", copy_position);
		return FAILURE;
	}


	if(source_position >= 0 && fs->occupied[source_position]) {
		// Allocate memory for the new file at destination
		File* new_file = (File*)(malloc(sizeof(File)));
		if(new_file == NULL) {
			print_error("Could not allocate memory to copy %s.", file_name);
			return FAILURE;
		}
		
		// assemble copy mark; the file at final_position is overwritten, so its name doesn't count as taken
		if(!make_unique_file_name(fs, file_name, final_position, new_file->name, sizeof(new_file->name))) {
			free(new_file);
			return FAILURE;
		}
		print_debug("Final new name: %s", new_file->name);

		// assemble copy of given file
		print_debug("Attempting to copy %s to %d", file_name, final_position);
		if(!copy_string(new_file->content, sizeof(new_file->content), fs->files[source_position]->content)) {
			print_error("Contents of %s exceed the file size limit.", file_name);
			free(new_file);
			return FAILURE;
		}
		new_file->size = fs->files[source_position]->size;
		memcpy(new_file->tags, fs->files[source_position]->tags, sizeof(new_file->tags));
		if(!copy_string(new_file->extension, sizeof(new_file->extension), fs->files[source_position]->extension)) {
			print_error("Could not copy extension metadata for %s.", file_name);
			free(new_file);
			return FAILURE;
		}
		
		// since I am allocating every time now, I need to free old files if they exist in the copy position
		if(fs->occupied[final_position]) {
			free(fs->files[final_position]);
		}
		
		// copy over file
		fs->files[final_position] = new_file;
		fs->occupied[final_position] = true;

		print_debug("File at %d: %s", source_position, file_name);
		print_debug("File at %d: %s", final_position, fs->files[final_position]->name);

		print_system("Successfully copied %s to %d.", file_name, final_position);
		return SUCCESS;
	}

	print_error("Failed to find file: %s", file_name);
	return FAILURE;
}

/// Prints out information on a file at a given position
/// Returns true if successful, returns false if positional argument is invalid
bool get_info_on_file(const FileSystem* fs, char* position_as_str) {
	int position = string_to_clamped_int(position_as_str);
	
	if(position < 0) {
		print_error("Invalid position argument: %s", position_as_str);
		return FAILURE;
	}

	File* file = fs->files[position];
	print_system("File name: %s\n\tSize: %d\n\tPosition: %d\n\tContents: %s\n\tExtension: %s\n\tTags:",
			file->name, file->size, position, file->content, file->extension);
	for(int i = 0; i < MAX_TAG_COUNT; i++) {
		if(file->tags[i][0] != '\0') {
			printf(" %s", file->tags[i]);
		}
	}
	printf("\n");
	return SUCCESS;
}

bool list_files(const FileSystem* fs) {
	printf("Position │ File Name\n");
	printf("─────────┼──────────\n");
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(fs->occupied[i]) {
			printf("       %d │ %s\n", i, fs->files[i]->name);
		} else {
			printf("       %d │ Empty\n", i);
		}
	}
	return SUCCESS;
}

bool echo_file(const FileSystem* fs, char* file_name) {
	int current_position = find_file_position(fs, file_name);

	if(current_position < 0 ) {
		print_error("File %s does not exist.", file_name);
		return FAILURE;
	}

	if(fs->occupied[current_position]) {
		print_system("%s", fs->files[current_position]->content);
		return SUCCESS;
	}

	// if current_position is not occupied, don't access files[current_position] to avoid weird memory reads
	print_error("File %s does not exist.", file_name);
	return FAILURE;
}

bool edit_file(FileSystem* fs, char* file_name) {
	int position = find_file_position(fs, file_name);
	if(position < 0 || !fs->occupied[position]) {
		print_error("File %s does not exist.", file_name);
		return FAILURE;
	}

	int result = edit_text(fs->files[position]->content, sizeof(fs->files[position]->content));
	if(result < 0) {
		print_error("Could not edit %s.", file_name);
		return FAILURE;
	}
	if(result == 0) {
		print_system("Changes to %s were discarded.", file_name);
		return SUCCESS;
	}

	fs->files[position]->size = strlen(fs->files[position]->content);
	print_system("Saved changes to %s.", file_name);
	return SUCCESS;
}

bool append_to_file(FileSystem* fs, char* file_name, char* content) {
	int current_position = find_file_position(fs, file_name);

	if(current_position < 0 ) {
		print_error("File %s does not exist.", file_name);
		return FAILURE;
	}

	if(strlen(content) >= MAX_FILE_SIZE) {
		print_error("The size of %s is greater than 256 bytes.", content);
		return FAILURE;
	}

	if(fs->occupied[current_position]) {
		if(append_string(fs->files[current_position]->content, sizeof(fs->files[current_position]->content), content)) {
			fs->files[current_position]->size = strlen(fs->files[current_position]->content);
			print_system("Appended %s to %s.", content, file_name);
			return SUCCESS;
		} else if(fs->files[current_position]->size >= MAX_FILE_SIZE) {
			print_error("%s is already at maximum size.", file_name);
			return FAILURE;
		} else {
			print_error("%s cannot be appended to %s because it will go over the file size limit of 256 bytes.", content, file_name);
			return FAILURE;
		}
	}

	print_error("File %s does not exist.", file_name);
	return FAILURE;
}

bool prepend_to_file(FileSystem* fs, char* file_name, char* prefix) {
	int current_position = find_file_position(fs, file_name);

	if(current_position < 0 ) {
		print_error("File %s does not exist.", file_name);
		return FAILURE;
	}
	
	if(strlen(prefix) >= MAX_FILE_SIZE) {
		print_error("The size of %s is greater than 256 bytes.", prefix);
		return FAILURE;
	}

	if(fs->occupied[current_position]) {
		print_debug("File + Prefix Size: %d", sizeof(fs->files[current_position]->content) + strlen(prefix));
		if(fs->files[current_position]->size + strlen(prefix) >= MAX_FILE_SIZE) {
			print_error("%s is already at maximum size.", file_name);
			return FAILURE;
		}

		print_debug("Content pointer position: %llu", fs->files[current_position]->content);
		print_debug("Content pointer offset position: %llu", fs->files[current_position]->content + strlen(prefix));

		memmove(fs->files[current_position]->content + strlen(prefix),
				fs->files[current_position]->content,
				strlen(fs->files[current_position]->content) + 1 // only move the used up space and \0
		);

		memcpy(fs->files[current_position]->content, prefix, strlen(prefix));
		
		print_system("Prepended %s to %s.", prefix, file_name);
		return SUCCESS;
	}

	print_error("File %s does not exist.", file_name);
	return FAILURE;
}

bool overwrite_file(FileSystem* fs, char* file_name, char* content) {
	int current_position = find_file_position(fs, file_name);

	if(current_position < 0 ) {
		print_error("File %s does not exist.", file_name);
		return FAILURE;
	}

	if(strlen(content) >= MAX_FILE_SIZE) {
		print_error("The size of %s is greater than 256 bytes.", content);
		return FAILURE;
	}

	if(fs->occupied[current_position]) {
		memset(fs->files[current_position]->content, 0, sizeof(fs->files[current_position]->content));
		if(!copy_string(fs->files[current_position]->content, sizeof(fs->files[current_position]->content), content)) {
			print_error("Contents for %s exceed the file size limit.", file_name);
			return FAILURE;
		}
		fs->files[current_position]->size = strlen(content);
		print_system("Overwrote %s with %s.", file_name, content);
		return SUCCESS;
	}

	print_error("File %s does not exist.", file_name);
	return FAILURE;
}

int tests(void) {
	FileSystem* fs = (FileSystem*)(malloc(sizeof(FileSystem)));
	if(fs == NULL || !initialize_file_system(fs)) {
		print_error("Failed to initialize the file system test fixture.");
		free(fs);
		return 1;
	}
	
	int result = create_file(fs, "foo.txt", strlen("0123456789"), "0123456789");
	if(!result) {
		print_error("Failed the create_file() test.");
		return 1;
	}

	result = search_file(fs, "foo.txt");
	if(!result) {
		print_error("Failed the search_file() test.");
		return 1;
	}

	result = tag_file(fs, "foo.txt", "notes");
	if(!result || strcmp(fs->files[0]->tags[0], "notes") != 0) {
		print_error("Failed the tag_file() test.");
		return 1;
	}

	if(find_extension_association(fs, "text") < 0 ||
			strcmp(fs->files[0]->extension, ".txt") != 0) {
		print_error("Failed the file extension metadata test.");
		return 1;
	}

	result = search_file(fs, "#notes");
	if(!result) {
		print_error("Failed the tag search test.");
		return 1;
	}

	result = search_file(fs, "@text");
	if(!result) {
		print_error("Failed the extension search test.");
		return 1;
	}

	result = rename_file(fs, "foo.txt", "bar.txt");
	if(!result) {
		print_error("Failed the rename_file() test.");
		return 1;
	}

	result = list_files(fs);
	if(!result) {
		print_error("Failed the list_files test.");
		return 1;
	}

	result = delete_file(fs, "bar.txt", 0);
	if(!result) {
		print_error("Failed the delete_file() test.");
		return 1;
	}

	// duplicate names get "(n)" before the extension
	create_file(fs, "a.txt", 1, "a");
	create_file(fs, "b.txt", 1, "b");
	create_file(fs, "a.txt", 1, "c");
	result = rename_file(fs, "b.txt", "a.txt");
	if(!result || strcmp(fs->files[0]->name, "a.txt") != 0 ||
			strcmp(fs->files[2]->name, "a(1).txt") != 0 ||
			strcmp(fs->files[1]->name, "a(2).txt") != 0 ||
			strcmp(fs->files[1]->extension, ".txt") != 0) {
		print_error("Failed the duplicate file name test.");
		return 1;
	}

	result = rename_file(fs, "a(2).txt", "a(2).txt");
	if(!result || strcmp(fs->files[1]->name, "a(2).txt") != 0) {
		print_error("Failed the rename to own name test.");
		return 1;
	}

	create_file(fs, "plain", 1, "p");
	create_file(fs, "plain", 1, "p");
	if(strcmp(fs->files[4]->name, "plain(1)") != 0) {
		print_error("Failed the duplicate name without extension test.");
		return 1;
	}

	result = copy_file(fs, "a.txt", "5");
	if(!result || strcmp(fs->files[5]->name, "a(3).txt") != 0) {
		print_error("Failed the copy duplicate name test.");
		return 1;
	}

	if(!delete_file(fs, "a(1).txt", 2) || !delete_file(fs, "a.txt", 0) || delete_file(fs, "a", 1)) {
		print_error("Failed the delete duplicate file test.");
		return 1;
	}

	char long_name[MAX_FILE_NAME_SIZE];
	memset(long_name, 'x', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	long_name[sizeof(long_name) - 5] = '.'; // 255 chars ending in ".xxx"
	if(!create_file(fs, long_name, 1, "l") || create_file(fs, long_name, 1, "l") ||
			rename_file(fs, "plain", long_name)) {
		print_error("Failed the duplicate name length limit test.");
		return 1;
	}

	// cleanup
	free(fs);
	return 0; // success
}


FileSystem* global_file_system;

#define check_result(r, ERR_MSG) if(r) { return RESULT_SUCCESS; }\
	else { \
		parser_error = ERR_MSG; \
		return RESULT_ERROR; \
	} \

enum Results {
	RESULT_EXIT,
	/// puts error message in the parser_error variable
	RESULT_ERROR,
	RESULT_SUCCESS
};

char* parser_error;
int parse_command(char* line) {
	print_debug(line);
	char* command;
	char* arg_1; // not used in exit
	char* arg_2; // only use in move, copy, rename, and File I/O
	char* cursor = line;

	command = next_token(&cursor, " \n");
	arg_1 = next_token(&cursor, " \n");
	arg_2 = next_token(&cursor, "\n");

	print_debug("Command: %s\n\tFirst Argument: %s\n\tSecond Argument: %s",
			command != NULL ? command : "(none)",
			arg_1 != NULL ? arg_1 : "(none)",
			arg_2 != NULL ? arg_2 : "(none)");

	if(command == NULL) {
		parser_error = "No command entered.";
		return RESULT_ERROR;
	} else if(streql(command, "exit")) {
		print_debug("Exiting shell..."); /// exit
		return RESULT_EXIT;
	} else if(streql(command, "create") && arg_1 && arg_2) {
		bool result = create_file(global_file_system, arg_1, strlen(arg_2), arg_2); /// create <file> <content>
		check_result(result, "Failed to create file.")
	} else if(streql(command, "delete") && arg_1) {
		int position = find_file_position(global_file_system, arg_1);
		if(position >= 0 && global_file_system->occupied[position]) {
			bool result = delete_file(global_file_system, arg_1, position); /// delete <file>
			check_result(result, "Failed to delete file.")
		}
		parser_error = "File does not exist";
		return RESULT_ERROR;
	} else if(streql(command, "search") && arg_1) {
		bool result = search_file(global_file_system, arg_1); /// search <file>, search #tag, or search @extension
		check_result(result, "Failed to find file.")
	} else if(streql(command, "tag") && arg_1 && arg_2) {
		bool result = tag_file(global_file_system, arg_1, arg_2); /// tag <file> <tag>
		check_result(result, "Failed to tag file.")
	} else if(streql(command, "associate") && arg_1 && arg_2) {
		bool result = associate_file_extension(global_file_system, arg_1, arg_2); /// associate <extension> <extension name>
		check_result(result, "Failed to associate file extension.")
	} else if(streql(command, "rename") && arg_1 && arg_2) {
		bool result = rename_file(global_file_system, arg_1, arg_2); /// rename <file> <new name>
		check_result(result, "Failed to rename file.")
	} else if(streql(command, "list")) {
		bool result = list_files(global_file_system);
		check_result(result, "Failed to list files.")
	} else if(streql(command, "move") && arg_1 && arg_2) {
		bool result = move_file(global_file_system, arg_1, arg_2); /// move <file> <new position>
		check_result(result, "Failed to move file.")
	} else if(streql(command, "copy") && arg_1 && arg_2) {
		bool result = copy_file(global_file_system, arg_1, arg_2);
		check_result(result, "Failed to copy file.")
	} else if(streql(command, "info") && arg_1) {
		bool result = get_info_on_file(global_file_system, arg_1);
		check_result(result, "Failed to get info on file.")
	} else if(streql(command, "echo") && arg_1) {
		bool result = echo_file(global_file_system, arg_1);
		check_result(result, "Failed to echo file.")
	} else if(streql(command, "edit") && arg_1) {
		bool result = edit_file(global_file_system, arg_1);
		check_result(result, "Failed to edit file.")
	} else if(streql(command, "append") && arg_1 && arg_2) {
		bool result = append_to_file(global_file_system, arg_1, arg_2);
		check_result(result, "Failed to append to file.")
	} else if(streql(command, "prepend") && arg_1 && arg_2) {
		bool result = prepend_to_file(global_file_system, arg_1, arg_2);
		check_result(result, "Failed to prepend to file.")
	} else if(streql(command, "overwrite") && arg_1 && arg_2) {
		bool result = overwrite_file(global_file_system, arg_1, arg_2);
		check_result(result, "Failed to overwrite file.");
	} else if(streql(command, "cls") || streql(command, "clear")) {

#if defined(_WIN32) || defined(_WIN64)
		system("cls");
#else
		system("clear");
#endif

		return RESULT_SUCCESS;
	} else if(streql(command, "help")) {
		print_system("exit - exit the program.");
		print_system("help - print this message.");
		print_system("cls/clear - clear the terminal.");
		print_system("create <name> <content> - create a file called <name> containing <content>.");
		print_system("delete <file> - delete <file>.");
		print_system("search <file> - search by file name.");
		print_system("search #<tag> - search for all files with the given tag.");
		print_system("search @<extension> - search for all files with the given extension name or suffix.");
		print_system("tag <file> <tag> - add a tag to a file (maximum 5 tags per file).");
		print_system("associate <extension> <extension name> - associate a suffix (for example md) with a searchable name (for example markdown).");
		print_system("info <position> - prints out some information about the file (if any) at <position>.");
		print_system("rename <file> <new name> - rename <file> to <new name>.");
		print_system("list - list the status of the file system, will print a table of all the positions in the filesystem and the name of the file occupying them (if any).");
		print_system("move <file> <new position> - moves <file> to <new position>, will overwrite the file at the new position.");
		print_system("copy <file> <destination> - copies <file> to <destination>, will overwrite the file at <destination>.");
		print_system("echo <file> - prints out the contents of <file>.");
		print_system("edit <file> - open <file> in the text editor (<C+s> saves, <ESC> discards unsaved changes).");
		print_system("append <file> <content> - adds <content> to the end of <file>, will fail if <file>'s size + <content>'s size is greater than 256 bytes or if <file>'s size is already 256 bytes.");
		print_system("prepend <file> <content> - adds <content> to the beginning of <file>, will fail if <file>'s size + <content>'s size is greater than 256 bytes or if <file>'s size is already 256 bytes.");
		print_system("overwrite <file> <content> - overwrites <file> with <content>, will fail if <content>'s size is greater than 256 bytes.");
		print_system("Additional information:");
		print_system("\tAll commands that take in a <file> argument will fail if the file doesn't already exist.");
		return RESULT_SUCCESS;
	} else if(!arg_1 || !arg_2) {
		parser_error = "Missing one of more arguments for command.";
		return RESULT_ERROR;
	}

	parser_error = "Unrecognized command.";
	return RESULT_ERROR;
}

int main(int argc, char* argv[]) {

// stole this off the interwebs for getting Unicode support so I could do box drawing
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode = 0;
	GetConsoleMode(hOut, &dwMode);
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, dwMode);
#endif
	
	for(int i = 1; i < argc; i++) { // iterate over args
		if(streql(argv[i], "-d")) {
			is_debug = true;
		}

		if(streql(argv[i], "-t")){
			do_tests = true;
		}
	}
	
	if(do_tests && tests() != 0) {
		return EXIT_FAILURE;
	}

	global_file_system = (FileSystem*)malloc(sizeof(FileSystem));
	if(global_file_system == NULL || !initialize_file_system(global_file_system)) {
		free(global_file_system);
		print_error("Could not initialize the file system.");
		return EXIT_FAILURE;
	}

	// begin shell
	bool quit = false;
	while (quit != true) {
		printf("[USER] > ");
		char line[MAX_COMMAND_LENGTH + MAX_FILE_SIZE + MAX_FILE_NAME_SIZE];
		int result = 0;

		if(fgets(line, sizeof(line), stdin)) {
			result = parse_command(line);
		}

		if(result == RESULT_ERROR) {
			print_error(parser_error);
		} else if(result == RESULT_EXIT) {
			quit = true;
			break;
		} // do nothing if parser succeeded
	}
	
	return 0;
}
