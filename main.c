#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#define MAX_NUM_FILES 10
#define MAX_FILE_NAME_SIZE 256
#define MAX_FILE_SIZE 256
#define MAX_COMMAND_LENGTH 20

#define SUCCESS true
#define FAILURE false

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

struct File {
	char name[MAX_FILE_NAME_SIZE]; // Max size is 256 bytes
	size_t size; // Size of file in bytes
	char content[MAX_FILE_SIZE]; // Max size is 256 bytes
};

/// files is the array containing the actual files.
/// occupied is for creating new files.
struct FileSystem { 
	struct File* files[MAX_NUM_FILES];
	bool occupied[MAX_NUM_FILES]; 
};

/// Returns the next open position in the given FileSystem, if none are open, returns -1
int get_next_position(const struct FileSystem* fs) {
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(!fs->occupied[i]) { return i; }
	}
	return -1;
}

/// Returns the total number of occupied spaces in the filesystem
size_t get_total_occupied(const struct FileSystem* fs) {
	int count = 0;
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(fs->occupied[i]) count++;
	}
	return count;
}

/// Returns the position of the file with the matching name, if no file with name exists, returns -1
int find_file_position(const struct FileSystem* fs, char* file_name) {
	int total_occupied = get_total_occupied(fs);
	print_debug("Total occupied spaces: %d", total_occupied);
	
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		if(fs->occupied[i]) { print_debug("%d | %s", i, fs->files[i]->name); }
		else { print_debug("%d | Empty", i); }

		if(streql(fs->files[i]->name, file_name)) {
			print_debug("%s equals %s, %s's position: %d", file_name, fs->files[i]->name, fs->files[i]->name, i);
			return i;
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

/// Creates a file and returns a true/false based on success/failure.
/// file_size is the size of the file in bytes.
/// The max size of a file is 256 bytes (for simplicity)
bool create_file(struct FileSystem* fs, char* file_name, size_t file_size, char* content) {
	print_debug("Creating File...");
	int next_open_position = get_next_position(fs);

	if(next_open_position < 0) {
		print_error("No more free space. Delete a file before creating a new one.");
		return FAILURE;
	}

	struct File* f = (struct File*)(malloc(sizeof(struct File)));

	strcpy_s(f->name, sizeof(f->name), file_name);
	
	if(file_size <= MAX_FILE_SIZE) {
		f->size = file_size;
	} else {
		print_error("%s size is greater than max file size of 256 bytes. Truncating file...\n\tFile size: %llu", file_name, file_size);
		return FAILURE;
	}

	print_debug("Copying contents over...");
	strcpy_s(f->content, sizeof(f->content), content);
	
	print_debug("File Contents: %s, Given Contents: %s", f->content, content);

	fs->files[next_open_position] = f;
	fs->occupied[next_open_position] = true;

	print_system("Successfully created %s\n\tSize: %d\n\tPosition: %d", f->name, f->size, next_open_position);
	return SUCCESS;
}

struct File zeroed_file;

/// Deletes the file pointed to by the given pointer.
/// Returns false if failed and true if succeeded.
bool delete_file(struct FileSystem* fs, char* file_name, int position) {
	print_debug("Deleting file...");

	if(position >= MAX_NUM_FILES) {
		print_error("Attempted to access a non-existent position in the File System.");
		return FAILURE;
	}

	int file_name_position = 0;
	size_t size_of_file_name = strlen(file_name);
	for(int i = 0; i < MAX_NUM_FILES; i++) { // find true position
		print_debug("Checking if %s is the same as %s", fs->files[position]->name, file_name);
		if(size_of_file_name == strlen(fs->files[position]->name)) { // check if same length first
			print_debug("%s and %s are the same length", fs->files[position]->name, file_name);
			if(streql(file_name, fs->files[position]->name)) {
				print_debug("%s and %s are the same string.", fs->files[position]->name, file_name);
				file_name_position = i;
				break;
			}
		}
	}

	if(file_name_position == position) { // check if true position matches given position
		print_debug("True position matches given position: %d == %d", file_name_position, position);
		free(fs->files[position]);
		fs->files[position] = &zeroed_file;
		fs->occupied[position] = false;
		print_system("File successfully deleted.");
		return SUCCESS;
	} else { // error if true position != given position
		print_error("Attempted deletion of file that does not have the same position as given position.\n\tFile: %s,\n\tPosition: %d,\n\tGiven Position: %d", file_name, file_name_position, position);
		return FAILURE;
	}
}

/// Searches for a file in the filesystem by name
/// Prints out the information of the file.
/// Returns true if successful, returns false if no file could be found
bool search_file(const struct FileSystem* fs, char* file_name) {
	int position = find_file_position(fs, file_name);
	if(position >= 0 && fs->occupied[position]) {
		struct File* f = fs->files[position];
		print_system("Found file: %s\n\tSize: %llu\n\tPosition: %llu", f->name, strlen(f->content), position);
		return SUCCESS;
	}
	return FAILURE;
}

/// Renames the given file to the new name
/// Returns true if successful, returns false if no file could be found
bool rename_file(struct FileSystem* fs, char* file_name, char* new_name) {
	int position = find_file_position(fs, file_name);
	print_debug("Position: %d", position);

	if(position >= 0 && fs->occupied[position]) {
		print_debug("Attempting to rename %s to %s.", file_name, new_name);
		strcpy_s(fs->files[position]->name, sizeof(fs->files[position]->name), new_name);
		strcpy_s(fs->files[position]->name, sizeof(fs->files[position]->name), new_name);
		print_system("Successfully renamed %s to %s.", file_name, new_name);
		return SUCCESS;
	}
	print_error("Attempted to rename non-existent file: %s.", file_name);
	return FAILURE;
}

/// Moves the given file to the given position
/// Returns true if successful, returns false if no file could be found, or the positional argument is invalid 
bool move_file(struct FileSystem* fs, char* file_name, char* new_position) {
	int final_position = string_to_clamped_int(new_position);
	int current_position = find_file_position(fs, file_name);
	
	if(final_position < 0) {
		print_error("Invalid position argument: %s", new_position);
		return FAILURE;
	}

	if(current_position >= 0 && fs->occupied[current_position]) { // file exists
		struct File* file = fs->files[current_position]; // save pointer temporarily
		
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

bool starts_with(const char* str, const char* prefix) {
	size_t prefix_len = strlen(prefix);

	if(prefix_len > strlen(str)) {
		return false;
	}
	return strncmp(str, prefix, prefix_len) == 0;
}

int count_duplicate_files(const struct FileSystem* fs, char* file_name) {
	int count = 0;
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		print_debug("count_duplicate_files.count = %d", count);
		print_debug("count_duplicate_files.i = %d", i);
		if(starts_with(fs->files[i]->name, file_name)) {
			print_debug("%s is a duplicate of %s.", fs->files[i]->name, file_name);
			count++;
		}
	}
	return count;
}

/// Copy file to new position, will overwrite file in copy_position 
/// /// Returns true if successful, returns false if positional argument is invalid, no file could be found
bool copy_file(struct FileSystem* fs, char* file_name, char* copy_position) {
	int final_position = string_to_clamped_int(copy_position);
	int source_position = find_file_position(fs, file_name);

	if(final_position < 0) {
		print_error("Invalid position argument: %s", copy_position);
		return FAILURE;
	}


	if(source_position >= 0 && fs->occupied[source_position]) {
		print_debug("Copying name over...");
		strcpy_s(fs->files[final_position]->name, sizeof(fs->files[final_position]->name), file_name);

		// assemble copy mark
		print_debug("Counting duplicate files");
		int dup_file_count = count_duplicate_files(fs, file_name);
		print_debug("%d duplicates found.");
		char copy_mark_str[256] = " (";
		char count_as_str[256];
		snprintf(count_as_str, sizeof(count_as_str), "%d", dup_file_count);
		strcat_s(copy_mark_str, sizeof(copy_mark_str), count_as_str);
		strcat_s(copy_mark_str, sizeof(copy_mark_str), ")");
		print_debug("Copy mark: '%s'", copy_mark_str);

		// add on copy mark
		strcat_s(fs->files[final_position]->name, sizeof(fs->files[final_position]->name), copy_mark_str);
		print_debug("Final new name: %s", fs->files[final_position]->name);

		printf("[DEBUG] Occupying position %d...\n", final_position); // testing to see if this is causing crash
		fs->occupied[final_position] = true;

		print_debug("Attempting to copy %s to %llu", file_name, final_position);
		strcpy_s(fs->files[final_position]->content, sizeof(fs->files[final_position]->content), fs->files[source_position]->content);
		fs->files[final_position]->size = fs->files[source_position]->size; // copy size

		print_debug("File at %d: %s", source_position, file_name);
		print_debug("File at %d: %s", final_position, fs->files[final_position]->name);

		print_system("Successfully copied %s to %llu.", file_name, final_position);
		return SUCCESS;
	}

	print_error("Failed to find file: %s", file_name);
	return FAILURE;
}

/// Prints out information on a file at a given position
/// Returns true if successful, returns false if positional argument is invalid
bool get_info_on_file(const struct FileSystem* fs, char* position_as_str) {
	int position = string_to_clamped_int(position_as_str);
	
	if(position < 0) {
		print_error("Invalid position argument: %s", position_as_str);
		return FAILURE;
	}

	print_system("File name: %s\n\tSize: %d\n\tPosition: %d\n\tContents: %s", fs->files[position]->name, fs->files[position]->size, position, fs->files[position]->content);
	return SUCCESS;
}

bool list_files(const struct FileSystem* fs) {
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

bool echo_file(const struct FileSystem* fs, char* file_name) {
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

bool append_to_file(struct FileSystem* fs, char* file_name, char* content) {
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
		if(fs->files[current_position]->size + sizeof(content) < MAX_FILE_SIZE) {
			strncat_s(fs->files[current_position]->content, sizeof(fs->files[current_position]->content), content, MAX_FILE_SIZE);
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

bool prepend_to_file(struct FileSystem* fs, char* file_name, char* prefix) {
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

bool overwrite_file(struct FileSystem* fs, char* file_name, char* content) {
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
		strcpy_s(fs->files[current_position]->content, sizeof(fs->files[current_position]->content), content);
		print_system("Overwrote %s with %s.", file_name, content);
		return SUCCESS;
	}

	print_error("File %s does not exist.", file_name);
	return FAILURE;
}

int tests() {
	struct FileSystem* fs = (struct FileSystem*)(malloc(sizeof(struct FileSystem)));
	zeroed_file = (struct File) { .size = 0, .name = "", .content = ""};
	for(int i = 0; i < MAX_NUM_FILES; i++) {
		fs->occupied[i] = false; // initialize occupied to all be set to false
		fs->files[i] = &zeroed_file; // initialize files to all be zeroed out
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

	result = delete_file(fs, "foo.txt", 0);
	if(!result) {
		print_error("Failed the delete_file() test.");
		return 1;
	}

	// cleanup
	free(fs);
	return 0; // success
}


struct FileSystem* global_file_system;

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
	char* _newline; // this is completely unused

	command = strtok_s(line, " ", &arg_1);
	arg_1 = strtok_s(arg_1, " ", &arg_2);
	arg_2 = strtok_s(arg_2, "\n", &_newline);

	if(arg_2 == NULL) {
		print_debug("Command did not contain a second argument.");
		arg_1 = strtok_s(arg_1, "\n", &_newline);
	}

	print_debug("Command: %s\n\tFirst Argument: %s\n\tSecond Argument: %s", command, arg_1, arg_2);

	if(streql(command, "exit")) {
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
		bool result = search_file(global_file_system, arg_1); /// search <file>
		check_result(result, "Failed to find file.")
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
		print_system("search <file> - search for <file>, prints out some information about <file> if it exists.");
		print_system("info <position> - prints out some information about the file (if any) at <position>.");
		print_system("rename <file> <new name> - rename <file> to <new name>.");
		print_system("list - list the status of the file system, will print a table of all the positions in the filesystem and the name of the file occupying them (if any).");
		print_system("move <file> <new position> - moves <file> to <new position>, will overwrite the file at the new position.");
		print_system("copy <file> <destination> - copies <file> to <destination>, will overwrite the file at <destination>.");
		print_system("echo <file> - prints out the contents of <file>.");
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

#define INIT_FS(FS) FS = (struct FileSystem*)(malloc(sizeof(struct FileSystem))); \
	zeroed_file = (struct File) { .size = 0, .name = "", .content = "" }; \
	for(int i = 0; i < MAX_NUM_FILES; i++) { \
		FS->occupied[i] = false; \
		FS->files[i] = &zeroed_file; \
	} \

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
		if(streql(argv[1], "-d")) {
			is_debug = true;
		}

		if(streql(argv[1], "-t")){
			do_tests = true;
		}
	}
	
	if(do_tests) { tests(); }

	INIT_FS(global_file_system)
	
	// TODO:
	// File I/O
	//   - prepend, overwrite

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
