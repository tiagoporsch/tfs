#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tfs.h"

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "Error: invalid arguments\n");
		exit(EXIT_FAILURE);
	}

	// Commands that don't require the disk to be mounted
	if (!strcmp(argv[2], "help")) {
		printf("Usage:\n");
		printf("  tfstool <disk_file> <option> [ARGUMENTS]\n");
		printf("\n");
		printf("Options:\n");
		printf("  format\n");
		printf("  debug\n");
		printf("  mkdir <path>\n");
		printf("  put <file> <path>\n");
		printf("\n");
		exit(EXIT_SUCCESS);
	}

	// Open and mount disk file
	FILE* disk_file = fopen(argv[1], "r+");
	if (!disk_file) {
		fprintf(stderr, "Error: couldn't open file '%s'\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	fseek(disk_file, 0, SEEK_END);
	int total_blocks = (int) (ftell(disk_file) / 512);
	if (total_blocks <= TFS_ROOT_BLOCK) {
		fprintf(stderr, "Error: disk file is too small (%d < 132096)\n", total_blocks);
		exit(EXIT_FAILURE);
	}
	if (tfs_mount(disk_file) == -1) {
		fclose(disk_file);
		exit(EXIT_FAILURE);
	}

	// Disk commands
	if (!strcmp(argv[2], "debug")) {
		if (argc != 3) {
			fprintf(stderr, "Error: debug: invalid arguments\n");
		} else {
			tfs_print_super();
			tfs_print_usage();
			tfs_print_files();
		}
	} else if (!strcmp(argv[2], "format")) {
		if (argc != 3) {
			fprintf(stderr, "Error: format: invalid arguments\n");
		} else {
			tfs_format(total_blocks, true);
		}
	} else if (!strcmp(argv[2], "mkdir")) {
		if (argc != 4) {
			fprintf(stderr, "Error: mkdir: invalid arguments\n");
		} else {
			tfs_mknode(argv[3], TFS_DIRECTORY);
		}
	} else if (!strcmp(argv[2], "put")) {
		if (argc != 5) {
			fprintf(stderr, "Error: put: invalid arguments\n");
		} else {
			if (tfs_mknode(argv[4], TFS_FILE)) {
				FILE* file = fopen(argv[3], "r");
				if (!file) {
					fprintf(stderr, "Error opening file '%s'\n", argv[3]);
				} else {
					fseek(file, 0, SEEK_END);
					size_t length = ftell(file);
					fseek(file, 0, SEEK_SET);
					if (length > 0) {
						void* buffer = malloc(length);
						fread(buffer, 1, length, file);
						tfs_write(argv[4], buffer, length);
						free(buffer);
					}
				}
			}
		}
	}

	// Clean up
	tfs_umount();
	fclose(disk_file);
	return 0;
}
