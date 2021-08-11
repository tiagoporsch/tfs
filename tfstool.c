#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tfs.h"

void die(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
	if (argc < 3 || !strcmp(argv[2], "help")) {
		printf("Usage:\n");
		printf("  tfstool <disk_file> <option> [ARGUMENTS]\n");
		printf("\n");
		printf("Options:\n");
		printf("  format\n");
		printf("  debug\n");
		printf("  mkdir <path>\n");
		printf("  put <path> <file>\n");
		printf("\n");
		exit(EXIT_SUCCESS);
	}

	// Open and mount disk file
	FILE* disk_file = fopen(argv[1], "r+");
	if (!disk_file)
		die("couldn't open file '%s'\n", argv[1]);
	fseek(disk_file, 0, SEEK_END);
	uint64_t total_blocks = ftell(disk_file) / 512;
	if (total_blocks <= TFS_ROOT_BLOCK)
		die("disk file is too small\n");
	if (!tfs_mount(disk_file))
		die("unable to mount disk\n");

	// Commands
	if (!strcmp(argv[2], "cat")) {
		if (argc != 4)
			die("cat: invalid arguments\n");

		union tfs_block block;
		if (!tfs_find(argv[3], &block))
			die("cat: '%s': not found\n", argv[3]);
		if (block.type != TFS_FILE)
			die("cat: '%s': not a file\n", argv[3]);

		char* buffer = malloc(block.size + 1);
		buffer[tfs_read(&block, 0, buffer, block.size)] = 0;
		puts(buffer);
		free(buffer);
	} else if (!strcmp(argv[2], "debug")) {
		if (argc != 3)
			die("debug: invalid arguments\n");
		tfs_print_super();
		tfs_print_usage();
		tfs_print_files();
	} else if (!strcmp(argv[2], "format")) {
		if (argc != 3)
			die("format: invalid arguments\n");
		tfs_format(total_blocks);
	} else if (!strcmp(argv[2], "mkdir")) {
		if (argc != 4)
			die("mkdir: invalid arguments\n");

		char* base = strdup(argv[3]);
		char* name = strrchr(base, '/');
		if (name == NULL)
			die("mkdir: invalid path '%s'\n", base);
		*name = 0;

		union tfs_block block;
		if (!tfs_find(base == name ? "/" : base, &block))
			die("mkdir: parent doesn't exist\n");
		name++;

		if (!tfs_add(&block, TFS_DIRECTORY, name))
			die("mkdir: error creating entry\n");
		free(base);
	} else if (!strcmp(argv[2], "put")) {
		if (argc != 5)
			die("put: invalid arguments\n");

		char* base = strdup(argv[3]);
		char* name = strrchr(base, '/');
		if (name == NULL)
			die("put: invalid path '%s'\n", base);
		*name = 0;

		union tfs_block block;
		if (!tfs_find(base == name ? "/" : base, &block))
			die("put: parent doesn't exist\n");
		name++;

		if (!tfs_add(&block, TFS_FILE, name))
			die("put: error creating entry\n");
		if (!tfs_child(&block, &block, name))
			die("put: error accessing entry?\n");
		free(base);

		FILE* file = fopen(argv[4], "r");
		if (file == NULL)
			die("put: couldn't open file '%s'\n", argv[4]);

		fseek(file, 0, SEEK_END);
		size_t length = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (length > 0) {
			void* buffer = malloc(length);
			if (buffer == NULL)
				die("put: error allocating memory\n");
			fread(buffer, 1, length, file);
			tfs_write(&block, 0, buffer, length);
			free(buffer);
		}
	}

	// Clean up
	tfs_umount();
	fclose(disk_file);
	return 0;
}
