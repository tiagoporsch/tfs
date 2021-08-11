#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TFS_NAME_LENGTH 460
#define TFS_ROOT_BLOCK 2048

enum tfs_type {
	TFS_FILE, TFS_DIRECTORY,
};

union tfs_block {
	struct {
		uint64_t index;
		uint64_t parent;
		uint64_t child;
		uint64_t next;
		uint64_t size;
		uint64_t time;
		uint32_t type;
		char name[TFS_NAME_LENGTH];
	} __attribute__ ((packed));
	struct {
		uint8_t boot_code[486];
		uint64_t total_blocks;
		uint64_t bitmap_blocks;
		uint64_t bitmap_offset;
		uint16_t boot_signature;
	} __attribute__ ((packed));
	uint64_t pointer[64];
} __attribute__ ((packed));

bool tfs_mount(FILE*);
bool tfs_umount();

bool tfs_format(uint64_t);

bool tfs_find(const char*, union tfs_block*);
bool tfs_parent(union tfs_block*, union tfs_block*);
bool tfs_child(union tfs_block*, union tfs_block*, const char*);
bool tfs_next(union tfs_block*, union tfs_block*);

bool tfs_add(union tfs_block*, enum tfs_type, const char*);
bool tfs_remove(union tfs_block*);

uint64_t tfs_write(union tfs_block*, uint64_t, const void*, uint64_t);
uint64_t tfs_read(union tfs_block*, uint64_t, void*, uint64_t);

void tfs_print_super();
void tfs_print_usage();
void tfs_print_files();
