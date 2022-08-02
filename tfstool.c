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
		printf("  tfstool <disk_file> <command> [arguments]\n");
		printf("\n");
		printf("Commands:\n");
		printf("  cat <path>\n");
		printf("  debug\n");
		printf("  format\n");
		printf("  mkdir <path>\n");
		printf("  put <path> <file>\n");
		printf("  reserve <file>\n");
		printf("\n");
		exit(EXIT_SUCCESS);
	}

	// Open and mount disk file
	FILE* disk_file = fopen(argv[1], "r+");
	if (!disk_file)
		die("tfstool: couldn't open file '%s'\n", argv[1]);
	fseek(disk_file, 0, SEEK_END);
	uint64_t total_blocks = ftell(disk_file) / 512;
	if (total_blocks <= TFS_ROOT_BLOCK)
		die("tfstool: disk file is too small\n");
	if (!tfs_mount(disk_file))
		die("tfstool: unable to mount disk\n");

	// Commands
	if (!strcmp(argv[2], "cat")) {
		if (argc != 4)
			die("tfstool: cat: invalid arguments\n");

		union tfs_block block;
		if (!tfs_find(argv[3], &block))
			die("tfstool: cat: '%s': not found\n", argv[3]);
		if (block.type != TFS_FILE)
			die("tfstool: cat: '%s': not a file\n", argv[3]);

		char* buffer = malloc(block.size + 1);
		if (!buffer)
			die("tfstool: cat: error allocating memory\n");
		buffer[tfs_read(&block, 0, buffer, block.size)] = 0;
		puts(buffer);
		free(buffer);
	} else if (!strcmp(argv[2], "debug")) {
		if (argc != 3)
			die("tfstool: debug: invalid arguments\n");
		tfs_print_super();
		tfs_print_usage();
		tfs_print_files();
	} else if (!strcmp(argv[2], "format")) {
		if (argc != 3)
			die("tfstool: format: invalid arguments\n");
		tfs_format(total_blocks);
	} else if (!strcmp(argv[2], "mkdir")) {
		if (argc != 4)
			die("tfstool: mkdir: invalid arguments\n");

		char* base = strdup(argv[3]);
		char* name = strrchr(base, '/');
		if (name == NULL)
			die("tfstool: mkdir: invalid path '%s'\n", base);
		*name = 0;

		union tfs_block block;
		if (!tfs_find(base == name ? "/" : base, &block))
			die("tfstool: mkdir: parent doesn't exist\n");
		name++;

		if (!tfs_add(&block, TFS_DIRECTORY, name))
			die("tfstool: mkdir: error creating entry\n");
		free(base);
	} else if (!strcmp(argv[2], "put")) {
		if (argc != 5)
			die("tfstool: put: invalid arguments\n");

		char* base = strdup(argv[3]);
		char* name = strrchr(base, '/');
		if (name == NULL)
			die("tfstool: put: invalid path '%s'\n", base);
		*name = 0;

		union tfs_block block;
		if (!tfs_find(base == name ? "/" : base, &block))
			die("tfstool: put: parent doesn't exist\n");
		name++;

		if (!tfs_add(&block, TFS_FILE, name))
			die("tfstool: put: error creating entry\n");
		if (!tfs_child(&block, &block, name))
			die("tfstool: put: error accessing entry?\n");
		free(base);

		FILE* file = fopen(argv[4], "r");
		if (file == NULL)
			die("tfstool: put: couldn't open file '%s'\n", argv[4]);

		fseek(file, 0, SEEK_END);
		size_t length = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (length > 0) {
			void* buffer = malloc(length);
			if (buffer == NULL)
				die("tfstool: put: error allocating memory\n");
			if (fread(buffer, 1, length, file) != length)
				die("tfstool: put: error reading file\n");
			tfs_write(&block, 0, buffer, length);
			free(buffer);
		}
		fclose(file);
	} else if (!strcmp(argv[2], "reserve")) {
		if (argc != 4)
			die("tfstool: reserve: invalid arguments\n");

		FILE* file = fopen(argv[3], "r");
		if (file == NULL)
			die("tfstool: reserve: couldn't open file '%s'\n", argv[4]);

		fseek(file, 0, SEEK_END);
		size_t length = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (length > 2047 * 512)
			die("tfstool: reserve: file is too big\n");
		if (length > 0) {
			void* buffer = malloc(length);
			if (buffer == NULL)
				die("tfstool: reserve: error allocating memory\n");
			if (fread(buffer, 1, length, file) != length)
				die("tfstool: reserve: error reading file\n");
			fseek(disk_file, 512, SEEK_SET);
			fwrite(buffer, 1, length, disk_file);
			free(buffer);
		}
		fclose(file);
	} else {
		die("tfstool: %s: invalid command\n", argv[2]);
	}

	// Clean up
	tfs_umount();
	fclose(disk_file);
	return 0;
}
