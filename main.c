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

typedef size_t u64;
/* NITPICK: It's preferred to define such constants in ALL CAPS. This can help you visually
 * differentiate them from variables defined in functions. */
#define max_num_files 10
#define max_file_name_size 256
#define max_file_size 256
#define max_command_length 20

#define success true
#define failure false

// string macros
/* Using strlen like this can mean you falsely indicate that strings are equal when LIT_2 is a
 * prefix of LIT_1. For example, LIT_1 = "ABCD" and LIT_2 = "ABC" will be considered equal.
 * You may want to add a +1 to factor in the NUL terminator as well. */
#define STREQL(LIT_1, LIT_2) strncmp(LIT_1, LIT_2, strlen(LIT_2)) == 0
#define STR(LIT) strlen(LIT), (LIT)

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

/* Word of caution: When you're computing size, you may have to take alignment into account.
 * The sizes may also depend on constants that you may change throughout the project.
 * Moreover, I think the math is off here. */
struct File { // 256 + 8 + 8 + 256 = 16 + 512 = 530 bytes
	/* NITPICK: Preferable to use the defines from earlier. Same for 'content'. */
	char file_name[256]; // Max size is 256 bytes
	/* Ideally, you may want to separate the position from the file's content in storage.
	 * This may be more appropriate for an 'opened file' structure. */
	u64 position; // Position of file in FileSystem
	u64 file_size; // Size of file in bytes
	char content[256]; // Max size is 256 bytes
};

/// file_names array is for searching for a file based on file_names.
/// files is the array containing the actual files.
/// occupied is for creating new files.
struct FileSystem { // (8 * 10) + (530 * 10) + (1 * 10) = 80 + 5300 + 10 = 90 + 5300 = 5390 bytes
	char* file_names[max_num_files];
	struct File* files[max_num_files];
	bool occupied[max_num_files]; 
};

/// Returns the next open position in the given FileSystem, if none are open, returns -1
/* NITPICK: You may want to mark the fs parameter as const to indicate that this only a querying
 * function rather than something that has 'side effects' on fs.  (Same for other functions like
 * get_total_occupied) */
int get_next_position(struct FileSystem* fs) {
	for(int i = 0; i < max_num_files; i++) {
		if(!fs->occupied[i]) { return i; }
	}
	return -1;
}

/* NITPICK: Since the count can't be negative, I think an unsigned return value is preferred, as long
 * as it doesn't cause more problems when you need to compare it with values of other types. */
int get_total_occupied(struct FileSystem* fs) {
	int count = 0;
	for(int i = 0; i < max_num_files; i++) {
		if(fs->occupied[i]) count++;
	}
	return count;
}

/// Returns the position of the file with the matching name, if no file with name exists, returns -1
int find_file_position(struct FileSystem* fs, char* file_name) {
	int total_occupied = get_total_occupied(fs);
	print_debug("Total occupied spaces: %d", total_occupied);
	
	for(int i = 0; i < max_num_files; i++) {
		if(fs->occupied[i]) { print_debug("%d | %s", i, fs->file_names[i]); }
		else { print_debug("%d | Empty", i); }

		if(STREQL(fs->file_names[i], file_name)) {
			print_debug("%s equals %s, %s's position: %d", file_name, fs->file_names[i], fs->file_names[i], i);
			return i;
		}
	}
	
	print_error("The file %s does not exist", file_name);
	return -1;
}

/// Creates a file and returns a true/false based on success/failure.
/// file_size is the size of the file in bytes.
/// The max size of a file is 256 bytes (for simplicity)
bool create_file(struct FileSystem* fs, char file_name[256], u64 file_size, char content[256]) {
	print_debug("Creating File...");
	int next_open_position = get_next_position(fs);

	if(next_open_position < 0) {
		print_error("No more free space. Delete a file before creating a new one.");
		return failure;
	}

	/* NITPICK: Though it likely won't happen with a small number of relatively small allocations,
	 * it's generally good practice to check whether malloc() calls were successful. */
	struct File* f = (struct File*)(malloc(sizeof(struct File)));

	strcpy_s(f->file_name, sizeof(f->file_name), file_name);
	
	/* NITPICK: If strlen(content) isn't expected to change while checking the conditions, it's
	 * preferable to assign it to a variable and use that instead. */
	if(strlen(content) == file_size && (strlen(content) < max_file_size || file_size <= max_file_size)) {
		f->file_size = file_size;
	} else if (strlen(content) > max_file_size || file_size > max_file_size){
		print_error("File was greater than max file size of 256 bytes. Truncating file...\n\tFile size: %llu", file_size);
		f->file_size = max_file_size; // truncate file if over max size
	} else {
		print_error("Given file size was not equivalent to actual file size. Overriding given file size...\n\tGiven Size: %llu\n\tActual Size: %llu", file_size, strlen(content));
		f->file_size = strlen(content);
	}

	print_debug("Copying contents over...");
	strcpy_s(f->content, sizeof(f->content), content);
	
	print_debug("Size of content: %llu\n\tSize of f.content: %llu", strlen(content), sizeof(f->content));

	print_debug("File Contents: %s, Given Contents: %s", f->content, content);

	fs->file_names[next_open_position] = f->file_name;
	fs->files[next_open_position] = f;
	fs->occupied[next_open_position] = true;

	print_system("Successfully created %s\n\tSize: %d\n\tPosition: %d", f->file_name, f->file_size, f->position);
	return success;
}

struct File zeroed_file;

/// Deletes the file pointed to by the given pointer.
/// Returns false if failed and true if succeeded.
bool delete_file(struct FileSystem* fs, char* file_name, int position) {
	print_debug("Deleting file...");

	if(position >= max_num_files) {
		print_error("Attempted to access a non-existent position in the File System.");
		return failure;
	}

	int file_name_position = 0;
	u64 size_of_file_name = strlen(file_name);
	for(int i = 0; i < max_num_files; i++) { // find true position
		if(size_of_file_name == strlen(fs->file_names[position])) { // check if same length first
			if(strncmp(file_name, fs->file_names[position], size_of_file_name)) {
				file_name_position = i;
				break;
			}
		}
	}

	if(file_name_position == position) { // check if true position matches given position
		free(fs->files[position]);
		fs->files[position] = &zeroed_file;
		fs->occupied[position] = false;
		print_system("File successfully deleted.");
		return success;
	} else { // error if true position != given position
		print_error("Attempted deletion of file that does not have the same position as given position.\n\tFile: %s,\n\tPosition: %d,\n\tGiven Position: %d", file_name, file_name_position, position);
		return failure;
	}
}

bool search_file(struct FileSystem* fs, char* file_name) {
	int position = find_file_position(fs, file_name);
	if(position >= 0 && fs->occupied[position]) {
		struct File* f = fs->files[position];
		print_system("Found file: %s\n\tSize: %llu\n\tPosition: %llu", f->file_name, strlen(f->content), f->position);
		return success;
	}
	return failure;
}

bool rename_file(struct FileSystem* fs, char* file_name, char* new_name) {
	int position = find_file_position(fs, file_name);
	print_debug("Position: %d", position);

	if(position >= 0 && fs->occupied[position]) {
		print_debug("Attempting to rename %s to %s.", file_name, new_name);
		strcpy_s(fs->file_names[position], sizeof(fs->file_names[position]), new_name);
		strcpy_s(fs->files[position]->file_name, sizeof(fs->files[position]->file_name), new_name);
		print_system("Successfully renamed %s to %s.", file_name, new_name);
		return success;
	}
	print_error("Attempted to rename non-existent file: %s.", file_name);
	return failure;
}

bool move_file(struct FileSystem* fs, char* file_name, char* new_position) {
	/* NITPICK: It can likely help to separate the issue of figuring out the true position from
	 * the function that deals with moving the file. */
	char* endptr;
	u64 final_position = strtol(new_position, &endptr, 10);
	if(endptr == new_position) {
		print_error("Couldn't convert %s to an integer.", new_position);
		return failure;
	} else if(*endptr != '\0') {
		print_error("Partial conversion of %s.", new_position);
		return failure;
	}

	// clamp final position
	/* You may want to warn if the position changes as a result of this. It sounds like it shouldn't
	 * happen in ideal cases.  Same with copy_file() below. */
	if(final_position > 9) { // max index of fs->files[]
		final_position = 9;
	} else if(final_position < 0) { // just in case strtol() supports negative integers
		final_position = 0;
	}

	int current_position = find_file_position(fs, file_name);
	if(current_position >= 0 && fs->occupied[current_position]) { // file exists
		struct File* file = fs->files[current_position]; // save pointer temporarily
		
		// move pointer
		fs->file_names[final_position] = file->file_name;
		fs->files[final_position] = file;
		fs->occupied[final_position] = true;
		
		// free up previous position
		fs->occupied[current_position] = false;
		fs->file_names[current_position] = "";
		fs->files[current_position] = &zeroed_file;
		
		print_system("Successfully moved %s from %llu to %llu", file_name, current_position, final_position);
		return success;
	}

	print_error("Attempted to move non-existent file: %s.", file_name);
	return failure;
}

bool copy_file(struct FileSystem* fs, char* file_name, char* copy_position) {
	char* endptr;
	u64 final_position = strtol(copy_position, &endptr, 10);
	if(endptr == copy_position) {
		print_error("Couldn't convert %s to an integer.", copy_position);
		return failure;
	} else if(*endptr != '\0') {
		print_error("Partial conversion of %s.", copy_position);
		return failure;
	}

	// clamp final position
	if(final_position > 9) { // max index of fs->files[]
		final_position = 9;
	} else if(final_position < 0) { // just in case strtol() supports negative integers
		final_position = 0;
	}

	int source_position = find_file_position(fs, file_name);
	/* NITPICK: It's normally preferred to error out in the invalid cases, and then have the rest
	 * of the program follow the 'happy flow'. */
	if(source_position >= 0) {
		print_debug("Copying name over...");
		/* NITPICK: There's an inconsistency with the way you pass the size parameter t
		 * strcpy_s. In other cases, you used sizeof(). */
		strcpy_s(fs->file_names[final_position], max_file_name_size, file_name);
		
		print_debug("Occupying position: %s", final_position);
		fs->occupied[final_position] = true;

		print_debug("Attempting to copy %s to %llu", file_name, final_position);
		memcpy_s(&fs->files[final_position], sizeof(struct File), &fs->files[source_position], sizeof(struct File));

		print_debug("File at %d: %s", source_position, file_name);
		print_debug("File at %d: %s", final_position, fs->file_names[final_position]);

		print_system("Successfully copied %s to %llu.", file_name, final_position);
		return success;
	}

	print_error("Failed to find file: %s", file_name);
	return failure;
}

bool list_files(struct FileSystem* fs) {
	printf("Position │ File Name\n");
	printf("─────────┼──────────\n");
	for(int i = 0; i < max_num_files; i++) {
		if(fs->occupied[i]) {
			printf("       %d │ %s\n", i, fs->file_names[i]);
		} else {
			printf("       %d │ Empty\n", i);
		}
	}
	return success;
}

int tests() {
	struct FileSystem* fs = (struct FileSystem*)(malloc(sizeof(struct FileSystem)));
	zeroed_file = (struct File) { .file_size = 0, .file_name = "", .content = ""};
	for(int i = 0; i < max_num_files; i++) {
		fs->occupied[i] = false; // initialize occupied to all be set to false
		fs->files[i] = &zeroed_file; // initialize files to all be zeroed out
		fs->file_names[i] = ""; // initialize file names to be ""
	}
	
	int result = create_file(fs, "foo.txt", STR("0123456789"));
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

	if(STREQL(command, "exit")) {
		print_debug("Exiting shell..."); /// exit
		return RESULT_EXIT;
	} else if(STREQL(command, "create")) {
		bool result = create_file(global_file_system, arg_1, STR(arg_2)); /// create <file> <content>
		check_result(result, "Failed to create file.")
	} else if(STREQL(command, "delete")) {
		int position = find_file_position(global_file_system, arg_1);
		if(position >= 0 && global_file_system->occupied[position]) {
			bool result = delete_file(global_file_system, arg_1, position); /// delete <file>
			check_result(result, "Failed to delete file.")
		}
		parser_error = "File does not exist";
		return RESULT_ERROR;
	} else if(STREQL(command, "search")) {
		bool result = search_file(global_file_system, arg_1); /// search <file>
		check_result(result, "Failed to find file.")
	} else if(STREQL(command, "rename")) {
		bool result = rename_file(global_file_system, arg_1, arg_2); /// rename <file> <new name>
		check_result(result, "Failed to rename file.")
	} else if(STREQL(command, "list")) {
		bool result = list_files(global_file_system);
		check_result(result, "Failed to list files.")
	} else if(STREQL(command, "move")) {
		bool result = move_file(global_file_system, arg_1, arg_2); /// move <file> <new position>
		check_result(result, "Failed to move file.");
	} else if(STREQL(command, "copy")) {
		bool result = copy_file(global_file_system, arg_1, arg_2);
		check_result(result, "Failed to copy file.");
	}

	parser_error = "Parser Incomplete.";
	return RESULT_ERROR;
}

#define INIT_FS(FS) FS = (struct FileSystem*)(malloc(sizeof(struct FileSystem))); \
	zeroed_file = (struct File) { .file_size = 0, .file_name = "", .content = "" }; \
	for(int i = 0; i < max_num_files; i++) { \
		FS->occupied[i] = false; \
		FS->files[i] = &zeroed_file; \
		FS->file_names[i] = (char*)(malloc(max_file_name_size)); \
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

	/* NITPICK: Although it's only 2 arguments, you may want to rewrite it as a loop. */
	if(argc > 1) {
		/* NITPICK: Probably better as a function, since you're reusing it later. */
		if(STREQL(argv[1], "-d")) {
			is_debug = true;
		}

		if(STREQL(argv[1], "-t")){
			do_tests = true;
		}
	}

	if(argc > 2) {
		if(STREQL(argv[2], "-d")) {
			is_debug = true;
		}

		if(STREQL(argv[2], "-t")) {
			do_tests = true;
		}
	}
	
	if(do_tests) { tests(); }

	INIT_FS(global_file_system)
	
	// TODO:
	// 1. Basic File Management Commands
	//     - copy, info (prints info about the file at a specific position)
	// 2. File I/O
	//     - append, prepend, overwrite, print

	// begin shell
	bool quit = false;
	while (quit != true) {
		printf("[USER] > ");
		char line[max_command_length + max_file_size + max_file_name_size]; // max command length + max_file_name_size + max_file_size
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
