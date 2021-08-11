#include "tfs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE* disk_device;
static bool mounted = false;
static union tfs_block super;

static bool disk_read(uint64_t sector, void* buffer) {
	if (!mounted) {
		printf("disk_read: no disk mounted\n");
		return false;
	}
	if (fseek(disk_device, 512 * sector, SEEK_SET)) {
		printf("disk_read: fseek\n");
		return false;
	}
	if (fread(buffer, 1, 512, disk_device) != 512) {
		printf("disk_read: fread\n");
		return false;
	}
	return true;
}

static bool disk_write(uint64_t sector, const void* buffer) {
	if (!mounted) {
		fprintf(stderr, "Error @ disk_write: no disk mounted\n");
		return false;
	}
	if (fseek(disk_device, 512 * sector, SEEK_SET)) {
		fprintf(stderr, "Error @ disk_write: fseek\n");
		return false;
	}
	if (fwrite(buffer, 1, 512, disk_device) != 512) {
		fprintf(stderr, "Error @ disk_write: fwrite\n");
		return false;
	}
	return true;
}

bool tfs_mount(FILE* device) {
	if (mounted) {
		fprintf(stderr, "Error @ tfs_mount: disk already mounted\n");
		return false;
	}
	disk_device = device;
	mounted = true;
	if (!disk_read(0, &super)) {
		mounted = false;
		disk_device = NULL;
		return false;
	}
	return true;
}

bool tfs_umount() {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_umount: disk not mounted\n");
		return false;
	}
	disk_device = NULL;
	memset(&super, 0, sizeof super);
	mounted = false;
	return true;
}

/*
 * Bitmap
 */
static uint64_t tfs_alloc_block() {
	uint64_t buffer_block = super.bitmap_offset;
	uint8_t buffer[512];

	for (uint64_t index = 0; index < super.total_blocks; index++) {
		if (index % 4096 == 0)
			if (!disk_read(buffer_block++, &buffer))
				return 0;

		uint64_t byte = (index % 4096) / 8;
		if (buffer[byte] == 0xFF)
			continue;
		uint64_t mask = (1 << (7 - (index % 8)));
		if (!(buffer[byte] & mask)) {
			buffer[byte] |= mask;
			if (!disk_write(buffer_block - 1, &buffer))
				return 0;
			return index;
		}
	}

	fprintf(stderr, "Error @ tfs_alloc_block: disk is full\n");
	return 0;
}

static bool tfs_free_block(uint64_t index) {
	uint8_t buffer[512];
	uint64_t block = super.bitmap_offset + (index / 4096);
	if (!disk_read(block, &buffer))
		return false;
	buffer[(index % 4096) / 8] &= ~(1 << (7 - (index % 8)));
	return disk_write(block, &buffer);
}

/*
 * Calls
 */
bool tfs_format(uint64_t total_blocks) {
	if (!mounted) {
		printf("tfs_format: no mounted disk\n");
		return false;
	}

	// Super block
	super.total_blocks = total_blocks;
	super.bitmap_blocks = (super.total_blocks + 4095) / 4096;
	super.bitmap_offset = super.total_blocks - super.bitmap_blocks;
	super.boot_signature = 0xAA55;
	if (!disk_write(0, &super))
		return false;

	// Root directory
	union tfs_block root = { 0 };
	root.index = TFS_ROOT_BLOCK;
	root.parent = 0;
	root.child = 0;
	root.next = 0;
	root.size = 0;
	root.time = time(NULL) * 65536;
	root.type = TFS_DIRECTORY;
	root.name[0] = 0;
	if (!disk_write(root.index, &root))
		return false;

	// Clear bitmap
	uint8_t buffer[512] = { 0 };
	for (uint64_t i = 1; i < super.bitmap_blocks; i++)
		if (!disk_write(super.bitmap_offset + i, &buffer))
			return false;

	// Set reserved blocks
	uint64_t full_bytes = (TFS_ROOT_BLOCK + 1) / 8;
	for (uint64_t i = 0; i < full_bytes; i++)
		buffer[i] = 0xFF;
	buffer[full_bytes] = (0xFF << (8 - ((TFS_ROOT_BLOCK + 1) % 8))) & 0xFF;
	return disk_write(super.bitmap_offset, &buffer);
}

bool tfs_find(const char* path, union tfs_block* out) {
	if (!mounted) {
		printf("tfs_find: disk not mounted\n");
		return false;
	}
	if (*path++ != '/') {
		printf("tfs_find: path must be absolute\n");
		return false;
	}

	union tfs_block block;
	block.index = TFS_ROOT_BLOCK;
	if (!disk_read(block.index, &block))
		return false;

	char name[TFS_NAME_LENGTH];
	int name_index = 0;
	for (; path[-1]; path++) {
		name[name_index] = *path;
		if (name[name_index] == '/' || name[name_index] == 0) {
			if (name_index == 0)
				continue;
			name[name_index] = 0;
			if (block.type != TFS_DIRECTORY)
				return false;
			if (!tfs_child(&block, &block, name))
				return false;
			name_index = 0;
		} else {
			name_index++;
		}
	}

	if (out != NULL)
		memcpy(out, &block, sizeof(*out));
	return true;
}

bool tfs_parent(union tfs_block* block, union tfs_block* out) {
	if (block->parent == 0)
		return false;
	return out ? disk_read(block->parent, out) : true;
}

bool tfs_child(union tfs_block* block, union tfs_block* out, const char* name) {
	if (block->child == 0)
		return false;
	if (name == NULL)
		return out ? disk_read(block->child, out) : true;

	union tfs_block buffer = { 0 };
	if (!disk_read(block->child, &buffer))
		return false;
	while (1) {
		if (!strcmp(buffer.name, name))
			return out ? memcpy(out, &buffer, sizeof(*out)) != NULL : true;
		if (!tfs_next(&buffer, &buffer))
			return false;
	}
}

bool tfs_next(union tfs_block* block, union tfs_block* out) {
	if (block->next == 0)
		return false;
	return out ? disk_read(block->next, block) : true;
}

bool tfs_add(union tfs_block* parent, enum tfs_type type, const char* name) {
	if (!mounted) {
		printf("tfs_add: no mounted disk\n");
		return false;
	}
	if (parent->type != TFS_DIRECTORY) {
		printf("tfs_add: not a directory\n");
		return false;
	}
	if (tfs_child(parent, NULL, name)) {
		printf("tfs_add: '%s' already exists\n", name);
		return false;
	}

	union tfs_block block = { 0 };
	block.index = tfs_alloc_block();
	if (block.index == 0)
		return false;
	block.parent = parent->index;
	block.child = 0;
	block.next = 0;
	block.size = 0;
	block.time = time(NULL) * 65536;
	block.type = type;
	strncpy(block.name, name, TFS_NAME_LENGTH - 1);

	if (parent->child == 0) {
		parent->child = block.index;
		parent->size = 1;
		disk_write(parent->index, parent);
		disk_write(block.index, &block);
		return true;
	}

	union tfs_block curr;
	if (!disk_read(parent->child, &curr))
		return false;
	if (type >= curr.type && strcmp(name, curr.name) < 0) {
		parent->child = block.index;
		parent->size++;
		disk_write(parent->index, parent);
		block.next = curr.index;
		disk_write(block.index, &block);
		return true;
	}
	while (1) {
		union tfs_block prev;
		memcpy(&prev, &curr, sizeof(prev));
		if (!tfs_next(&curr, &curr)) {
			curr.next = block.index;
			disk_write(curr.index, &curr);
			parent->size++;
			disk_write(parent->index, parent);
			disk_write(block.index, &block);
			return true;
		}
		if (type >= curr.type && strcmp(name, curr.name) < 0) {
			prev.next = block.index;
			disk_write(prev.index, &prev);
			parent->size++;
			disk_write(parent->index, parent);
			block.next = curr.index;
			disk_write(block.index, &block);
			return true;
		}
	}
}

bool tfs_remove(union tfs_block* block) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_rmnode: no mounted disk\n");
		return false;
	}
	if (block->type == TFS_DIRECTORY && block->size > 0) {
		fprintf(stderr, "Error @ tfs_rmnode: directory not empty.\n");
		return false;
	}

	union tfs_block iter;
	if (!tfs_parent(block, &iter))
		return false;
	iter.size--;
	if (iter.child == block->index) {
		iter.child = block->next;
		disk_write(iter.index, &iter);
	} else {
		disk_write(iter.index, &iter);
		tfs_child(&iter, &iter, NULL);
		while (iter.next != block->index)
			tfs_next(&iter, &iter);
		iter.next = block->next;
		disk_write(iter.index, &iter);
	}
	return tfs_free_block(block->index);
}

uint64_t tfs_write(union tfs_block* block, uint64_t offset, const void* buffer, uint64_t length) {
	if (!mounted) {
		printf("tfs_write: no mounted disk\n");
		return 0;
	}
	if (block->type != TFS_FILE) {
		printf("tfs_write: not a file\n");
		return 0;
	}

	uint64_t data_offset = (offset & 0x1FF);
	uint64_t node_offset = (offset >> 9) % 63;
	uint64_t node_number = (offset >> 9) / 63;

	union tfs_block node = { 0 };
	if (block->child == 0) {
		block->child = tfs_alloc_block();
		disk_write(block->child, &node);
		disk_write(block->index, block);
	} else {
		disk_read(block->child, &node);
	}
	uint64_t node_index = block->child;
	while (node_number--) {
		if (node.pointer[63] == 0) {
			node.pointer[63] = tfs_alloc_block();
			disk_write(node_index, &node);
			node_index = node.pointer[63];
			memset(&node, 0, sizeof(node));
			disk_write(node_index, &node);
		} else {
			node_index = node.pointer[63];
			disk_read(node_index, &node);
		}
	}

	uint8_t* curr_buf = (uint8_t*) buffer;
	while (length) {
		if (node.pointer[node_offset] == 0)
			node.pointer[node_offset] = tfs_alloc_block();
		uint64_t to_write = length < (512 - data_offset) ? length : (512 - data_offset);

		if (length >= 512 && data_offset == 0) {
			disk_write(node.pointer[node_offset], curr_buf);
		} else {
			uint8_t data[512] = { 0 };
			memcpy(&data + data_offset, curr_buf, to_write);
			disk_write(node.pointer[node_offset], &data);
			data_offset = 0;
		}
		curr_buf += to_write;
		length -= to_write;

		if (++node_offset == 63) {
			if (node.pointer[node_offset] == 0) {
				node.pointer[node_offset] = tfs_alloc_block();
				disk_write(node_index, &node);
				node_index = node.pointer[node_offset];
				memset(&node, 0, sizeof(node));
				disk_write(node_index, &node);
			} else {
				node_index = node.pointer[node_offset];
				disk_read(node_index, &node);
			}
			node_offset = 0;
		}
	}
	disk_write(node_index, &node);

	uint64_t written = (uint64_t) curr_buf - (uint64_t) buffer;
	if (offset + written > block->size) {
		block->size = offset + written;
		disk_write(block->index, block);
	}
	return written;
}

uint64_t tfs_read(union tfs_block* block, uint64_t offset, void* buffer, uint64_t length) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_read: no mounted disk\n");
		return -1;
	}
	if (block->type != TFS_FILE) {
		fprintf(stderr, "Error @ tfs_write: not a file\n");
		return 0;
	}

	uint64_t data_offset = (offset & 0x1FF);
	uint64_t node_offset = (offset >> 9) % 63;
	uint64_t node_number = (offset >> 9) / 63;

	union tfs_block node = { 0 };
	if (block->child == 0) {
		block->child = tfs_alloc_block();
		disk_write(block->child, &node);
	} else {
		disk_read(block->child, &node);
	}
	uint64_t node_index = block->child;
	while (node_number--) {
		if (node.pointer[63] == 0) {
			node.pointer[63] = tfs_alloc_block();
			disk_write(node_index, &node);
			node_index = node.pointer[63];
			memset(&node, 0, sizeof(node));
			disk_write(node_index, &node);
		} else {
			node_index = node.pointer[63];
			disk_read(node_index, &node);
		}
	}

	uint8_t* curr_buf = (uint8_t*) buffer;
	while (length) {
		if (node.pointer[node_offset] == 0)
			return curr_buf - (uint8_t*) buffer;
		uint64_t to_read = length < (512 - data_offset) ? length : (512 - data_offset);

		if (length >= 512 && data_offset == 0) {
			disk_read(node.pointer[node_offset], curr_buf);
		} else {
			uint8_t data[512];
			disk_read(node.pointer[node_offset], &data);
			memcpy(curr_buf, &data + data_offset, to_read);
			data_offset = 0;
		}
		curr_buf += to_read;
		length -= to_read;

		if (++node_offset == 63) {
			if (node.pointer[node_offset] == 0)
				return curr_buf - (uint8_t*) buffer;
			node_index = node.pointer[node_offset];
			disk_read(node_index, &node);
			node_offset = 0;
		}
	}

	return curr_buf - (uint8_t*) buffer;
}

/*
 * Debug
 */
void tfs_print_super() {
	if (!mounted) {
		printf("tfs_print_super: no mounted disk\n");
		return;
	}
	printf("\033[97mFilesystem superblock:\033[0m\n");
	printf("  total_blocks: %lu\n", super.total_blocks);
	printf("  bitmap_blocks: %lu\n", super.bitmap_blocks);
	printf("  bitmap_offset: %lu\n\n", super.bitmap_offset);
}

void tfs_print_usage() {
	if (!mounted) {
		printf("tfs_print_usage: no mounted disk\n");
		return;
	}

	uint8_t bitmap[512];
	uint64_t used_blocks = 0;
	for (uint64_t i = 0; i < super.bitmap_blocks; i++) {
		if (!disk_read(super.total_blocks - super.bitmap_blocks + i, bitmap))
			return;
		for (int j = 0; j < 512; j++) {
			if (bitmap[j] == 0xFF)
				used_blocks += 8;
			else for (int k = 0; k < 8; k++)
				if (bitmap[j] & (1 << k))
					used_blocks++;
		}
	}

	printf("\033[97mFilesystem usage:\033[0m\n");
	printf("  total sectors: %lu\n", super.total_blocks);
	printf("  used sectors: %lu\n", used_blocks);
	printf("  used: %lu%%\n\n", (used_blocks * 100) / super.total_blocks);
}

void tfs_print_child_node(int, char*, bool);
void tfs_print_node(int block, char *indent) {
	union tfs_block node;
	if (!disk_read(block, &node))
		return;

	printf("%s", node.name);
	if (node.type == TFS_DIRECTORY) {
		printf("/\n");
		int child_block = node.child;
		union tfs_block child;
		while (child_block != 0) {
			if (!disk_read(child_block, &child))
				return;
			tfs_print_child_node(child_block, indent, child.next == 0);
			child_block = child.next;
		}
	} else {
		printf(" (%lu)\n", node.size);
	}
}
void tfs_print_child_node(int block, char *indent, bool last) {
	printf("\033[90m%s", indent);
	char *id = malloc(strlen(indent) + 3);
	memset(id, 0, strlen(indent) + 3);
	strcpy(id, indent);
	if (last) {
		printf("\\-");
		id[strlen(id)] = ' ';
		id[strlen(id)] = ' ';
		id[strlen(id)] = '\0';
	} else {
		printf("|-");
		id[strlen(id)] = '|';
		id[strlen(id)] = ' ';
		id[strlen(id)] = '\0';
	}
	printf("\033[0m");
	tfs_print_node(block, id);
	free(id);
}
void tfs_print_files() {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_print_files: no mounted disk\n");
		return;
	}

	printf("\033[97mFilesystem tree:\033[0m\n");
	tfs_print_node(TFS_ROOT_BLOCK, "");
	printf("\n");
}
