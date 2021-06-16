#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TFS_NAME_LENGTH 480
#define TFS_ROOT_BLOCK 2048

enum tfs_type {
	TFS_DIRECTORY = 1,
	TFS_FILE = 2,
};

struct tfs_super {
	uint8_t boot_code[498];
	uint32_t total_blocks;
	uint32_t bitmap_blocks;
	uint32_t bitmap_offset;
	uint16_t boot_signature;
} __attribute__ ((packed));

struct tfs_node {
	uint32_t type;
	uint32_t pointer;
	uint64_t size;
	uint64_t time;

	char name[TFS_NAME_LENGTH];

	uint32_t parent;
	uint32_t next;
} __attribute__ ((packed));

int tfs_mount(FILE*);
int tfs_umount();

int tfs_format(uint32_t, bool);
int tfs_getnode(const char*, struct tfs_node*);
int tfs_mknode(const char*, enum tfs_type);
int tfs_rmnode(const char*);
int tfs_write(const char*, const void*, uint64_t);
int tfs_read(const char*, void*, uint64_t);

void tfs_print_super();
void tfs_print_usage();
void tfs_print_files();
