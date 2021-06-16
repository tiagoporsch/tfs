#include "tfs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

FILE* disk_device;
bool mounted = false;
struct tfs_super super;

int disk_read(uint32_t sector, void* buffer) {
	if (!mounted) {
		fprintf(stderr, "Error @ disk_read: no disk mounted\n");
		return -1;
	}
	if (fseek(disk_device, 512 * sector, SEEK_SET)) {
		fprintf(stderr, "Error @ disk_read: fseek\n");
		return -1;
	}
	if (fread(buffer, 1, 512, disk_device) != 512) {
		fprintf(stderr, "Error @ disk_read: fread\n");
		return -1;
	}
	return 0;
}

int disk_write(uint32_t sector, const void* buffer) {
	if (!mounted) {
		fprintf(stderr, "Error @ disk_write: no disk mounted\n");
		return -1;
	}
	if (fseek(disk_device, 512 * sector, SEEK_SET)) {
		fprintf(stderr, "Error @ disk_write: fseek\n");
		return -1;
	}
	if (fwrite(buffer, 1, 512, disk_device) != 512) {
		fprintf(stderr, "Error @ disk_write: fwrite\n");
		return -1;
	}
	return 0;
}

int tfs_mount(FILE* device) {
	if (mounted) {
		fprintf(stderr, "Error @ tfs_mount: disk already mounted\n");
		return -1;
	}
	mounted = true;
	disk_device = device;
	if (disk_read(0, &super)) {
		mounted = false;
		disk_device = NULL;
		return -1;
	}
	return 0;
}

int tfs_umount() {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_umount: disk not mounted\n");
		return -1;
	}
	disk_device = NULL;
	memset(&super, 0, sizeof super);
	mounted = false;
	return 0;
}

/*
 * Bitmap
 */
int tfs_bitmap_alloc(int size) {
	int buffer_block = super.bitmap_offset;
	uint8_t buffer[512];

	int index = 0;
	int length = 0;
	for (uint32_t i = 0; i < super.total_blocks; i++) {
		if (i % 4096 == 0) {
			if (disk_read(buffer_block++, &buffer))	{
				return -1;
			}
		}
		int byte = (i % 4096) / 8;
		if (buffer[byte] == 0xFF) {
			continue;
		}
		if (!(buffer[byte] & (1 << (7 - (i % 8))))) {
			if (length == 0)
				index = i;
			if (++length == size)
				break;
		} else {
			index = 0;
			length = 0;
		}
		if (length == size) {
			break;
		}
	}
	if (index == 0 || length != size) {
		fprintf(stderr, "Error @ tfs_bitmap_alloc: disk is full\n");
		return -1;
	}

	int index_block = super.bitmap_offset + (index / 4096);
	if (disk_read(index_block, &buffer))
		return -1;
	for (int i = index; i < index + size; i++) {
		buffer[(i % 4096) / 8] |= (1 << (7 - (i % 8)));
		if ((i + 1) % 4096 == 0 && (i + 1) < (index + size)) {
			if (disk_write(index_block, &buffer))
				return -1;
			if (disk_read(++index_block, &buffer))
				return -1;
		}
	}
	if (disk_write(index_block, &buffer))
		return -1;

	return index;
}

int tfs_bitmap_free(int index, int size) {
	uint8_t buffer[512];

	int index_block = super.bitmap_offset + (index / 4096);
	if (disk_read(index_block, &buffer))
		return -1;
	for (int i = index; i < index + size; i++) {
		buffer[(i % 4096) / 8] &= ~(1 << (7 - (i % 8)));
		if ((i + 1) % 4096 == 0 && (i + 1) < (index + size)) {
			if (disk_write(index_block, &buffer))
				return -1;
			if (disk_read(++index_block, &buffer))
				return -1;
		}
	}
	if (disk_write(index_block, &buffer))
		return -1;
	return 0;
}

/*
 * Children
 */
int tfs_child_add(uint32_t block, uint32_t child_block, enum tfs_type child_type, const char* child_name) {
	struct tfs_node node;
	if (disk_read(block, &node))
		return -1;

	if (node.type != TFS_DIRECTORY) {
		fprintf(stderr, "Error @ tfs_child_add: not a directory\n");
		return -1;
	}

	if (node.pointer == 0) {
		node.pointer = child_block;
		node.size = 1;
		if (disk_write(block, &node))
			return -1;
		return 0;
	}

	int prev_block, curr_block;
	struct tfs_node prev, curr;

	curr_block = node.pointer;
	if (disk_read(curr_block, &curr))
		return -1;
	if (child_type <= curr.type && strcmp(child_name, curr.name) < 0) {
		node.pointer = child_block;
		node.size++;
		if (disk_write(block, &node))
			return -1;
		return curr_block;
	}
	for (;;) {
		if (curr.next == 0) {
			curr.next = child_block;
			if (disk_write(curr_block, &curr))
				return -1;
			node.size++;
			if (disk_write(block, &node))
				return -1;
			return 0;
		}

		prev_block = curr_block;
		memcpy(&prev, &curr, 512);

		curr_block = curr.next;
		if (disk_read(curr_block, &curr))
			return -1;
		if (child_type <= curr.type && strcmp(child_name, curr.name) < 0) {
			prev.next = child_block;
			if (disk_write(prev_block, &prev))
				return -1;
			node.size++;
			if (disk_write(block, &node))
				return -1;
			return curr_block;
		}
	}
}

int tfs_child_remove(uint32_t block, uint32_t child_block) {
	struct tfs_node node;
	if (disk_read(block, &node))
		return -1;
	
	if (node.type != TFS_DIRECTORY) {
		fprintf(stderr, "Error @ tfs_child_remove: not a directory\n");
		return -1;
	}

	if (node.pointer == 0)
		return -1;

	uint32_t prev_block, curr_block;
	struct tfs_node prev, curr;

	curr_block = node.pointer;
	if (disk_read(curr_block, &curr))
		return -1;
	if (curr_block == child_block) {
		node.pointer = curr.next;
		node.size--;
		if (disk_write(block, &node))
			return -1;
		return 0;
	}
	for (;;) {
		if (curr.next == 0) {
			fprintf(stderr, "Error @ tfs_child_remove: child doesn't exist\n");
			return -1;
		}

		prev_block = curr_block;
		memcpy(&prev, &curr, 512);

		curr_block = curr.next;
		if (disk_read(curr_block, &curr))
			return -1;
		if (curr_block == child_block) {
			prev.next = curr.next;
			if (disk_write(prev_block, &prev))
				return -1;
			node.size--;
			if (disk_write(block, &node))
				return -1;
			return 0;
		}
	}
}

/*
 * Calls
 */
int tfs_format(uint32_t total_blocks, bool bootable) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_format: no mounted disk\n");
		return -1;
	}
	if (TFS_ROOT_BLOCK > 4096) {
		fprintf(stderr, "Error @ tfs_format: too many reserved blocks\n");
		return -1;
	}

	// Super block
	super.total_blocks = total_blocks;
	super.bitmap_blocks = (super.total_blocks + 4095) / 4096;
	super.bitmap_offset = super.total_blocks - super.bitmap_blocks;
	super.boot_signature = bootable ? 0xAA55 : 0;
	if (disk_write(0, &super))
		return -1;

	// Root directory
	struct tfs_node root = { 0 };
	root.type = TFS_DIRECTORY;
	root.size = 0;
	root.pointer = 0;
	root.time = time(NULL) * 65536;
	root.name[0] = 0;
	root.parent = 0;
	root.next = 0;
	if (disk_write(TFS_ROOT_BLOCK, &root))
		return -1;

	// Bitmap
	uint8_t buffer[512] = { 0 };
	for (unsigned i = 1; i < super.bitmap_blocks; i++) {
		disk_write(super.bitmap_offset + i, &buffer);
	}
	unsigned full_bytes = (TFS_ROOT_BLOCK + 1) / 8;
	for (unsigned i = 0; i < full_bytes; i++) {
		buffer[i] = 0xFF;
	}
	buffer[full_bytes] = (0xFF << (8 - ((TFS_ROOT_BLOCK + 1) % 8))) & 0xFF;
	disk_write(super.bitmap_offset, &buffer);

	return 0;
}

int tfs_getnode(const char *path, struct tfs_node *out) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_getnode: no mounted disk\n");
		return -1;
	}

	int path_offset = 0;
	char name[TFS_NAME_LENGTH];

	int block = TFS_ROOT_BLOCK;
	struct tfs_node node;

	for (;;) {
		for (int i = 0; i < TFS_NAME_LENGTH; i++) {
			if (path[path_offset + i] == '/' || path[path_offset + i] == 0) {
				path_offset += i;
				name[i] = 0;
				break;
			}
			name[i] = path[path_offset + i];
		}

		for (;;) {
			if (disk_read(block, &node))
				return -1;
			if (strcmp(name, node.name) == 0)
				break;
			if (node.next == 0)
				return -1;
			block = node.next;
		}

		if (path[path_offset] == 0) {
			if (out != NULL)
				memcpy(out, &node, 512);
			return block;
		}

		path_offset++;
		if (node.type == TFS_DIRECTORY) {
			if (node.pointer == 0)
				return -1;
			block = node.pointer;
		} else {
			fprintf(stderr, "Error @ tfs_getnode: '%s' (%s) is not a directory.\n", name, path);
			return -1;
		}
	}
}

int tfs_mknode(const char *path, enum tfs_type type) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_mknode: no mounted disk\n");
		return -1;
	}

	if (tfs_getnode(path, NULL) != -1) {
		fprintf(stderr, "Error @ tfs_mknode: node '%s' already exists.\n", path);
		return -1;
	}

	char *parent_path = strdup(path);
	char *last_slash = strrchr(parent_path, '/');
	if (last_slash) *last_slash = 0;
	struct tfs_node parent;
	int parent_block = tfs_getnode(parent_path, &parent);
	if (parent_block == -1) {
		fprintf(stderr, "Error @ tfs_mknode: parent node '%s' doesn't exist.\n", parent_path);
		free(parent_path);
		return -1;
	}
	if (parent.type != TFS_DIRECTORY) {
		fprintf(stderr, "Error @ tfs_mknode: parent node '%s' is not a directory.\n", parent_path);
		free(parent_path);
		return -1;
	}
	free(parent_path);

	int block = tfs_bitmap_alloc(1);
	if (block == -1)
		return -1;
	struct tfs_node node = { 0 };
	node.type = type;
	node.pointer = 0;
	node.size = 0;
	node.time = time(NULL) * 65536;
	strcpy(node.name, strrchr(path, '/') + 1);
	node.parent = parent_block;
	node.next = (uint32_t) tfs_child_add(parent_block, block, node.type, node.name);
	if (node.next == (uint32_t) -1)
		return -1;
	if (disk_write(block, &node))
		return -1;
	return block;
}

int tfs_rmnode(const char *path) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_rmnode: no mounted disk\n");
		return -1;
	}

	struct tfs_node node;
	int block = tfs_getnode(path, &node);
	if (block == -1) {
		fprintf(stderr, "Error @ tfs_rmnode: node '%s' doesn't exist.\n", path);
		return -1;
	} else if (block == 1) {
		fprintf(stderr, "Error @ tfs_rmnode: can't remove the root node.\n");
		return -1;
	}

	if (node.type == TFS_FILE) {
		if (tfs_bitmap_free(node.pointer, (node.size + 511) / 512))
			return -1;
	} else if (node.type == TFS_DIRECTORY) {
		if (node.size > 0) {
			fprintf(stderr, "Error @ tfs_rmnode: directory '%s' is not empty.\n", path);
			return -1;
		}
	}

	if (tfs_child_remove(node.parent, block))
		return -1;
	if (tfs_bitmap_free(block, 1))
		return -1;
	return 0;
}

int tfs_write(const char *path, const void *buffer, uint64_t length) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_write: no mounted disk\n");
		return -1;
	}

	struct tfs_node node;
	int block = tfs_getnode(path, &node);
	if (block == -1) {
		fprintf(stderr, "Error @ tfs_write: node '%s' doesn't exist.\n", path);
		return -1;
	} else if (node.type != TFS_FILE) {
		fprintf(stderr, "Error @ tfs_write: node '%s' is not a file.\n", path);
		return -1;
	}

	if (node.pointer != 0) {
		if (tfs_bitmap_free(node.pointer, (node.size + 511) / 512))
			return -1;
		node.pointer = 0;
	}

	int blocks = (length + 511) / 512;
	int pointer = tfs_bitmap_alloc(blocks);
	if (pointer == -1) {
		disk_write(block, &node);
		return -1;
	}

	node.pointer = pointer;
	node.size = length;
	node.time = time(NULL) * 65536;
	if (disk_write(block, &node))
		return -1;

	for (int i = 0; i < blocks; i++) {
		if (i == blocks - 1) {
			char disk_buffer[512] = { 0 };
			memcpy(disk_buffer, buffer + i * 512, length - i * 512);
			if (disk_write(node.pointer + i, disk_buffer))
				return i * 512;
		} else {
			if (disk_write(node.pointer + i, buffer + i * 512))
				return i * 512;
		}
	}

	return length;
}

int tfs_read(const char *path, void *buffer, uint64_t length) {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_read: no mounted disk\n");
		return -1;
	}

	struct tfs_node node;
	int block = tfs_getnode(path, &node);
	if (block == -1) {
		fprintf(stderr, "Error @ tfs_read: node '%s' doesn't exist.\n", path);
		return -1;
	} else if (node.type != TFS_FILE) {
		fprintf(stderr, "Error @ tfs_read: node '%s' is not a file.\n", path);
		return -1;
	}

	if (node.pointer == 0)
		return 0;
	if (length > node.size)
		length = node.size;

	int blocks = (length + 511) / 512;
	for (int i = 0; i < blocks; i++) {
		if (i == blocks - 1) {
			char disk_buffer[512] = { 0 };
			if (disk_read(node.pointer + i, disk_buffer))
				return i * 512;
			memcpy(buffer + i * 512, disk_buffer, length - i * 512);
		} else {
			if (disk_read(node.pointer + i, buffer + i * 512))
				return i * 512;
		}
	}

	return length;
}

/*
 * Debug
 */
void tfs_print_super() {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_print_super: no mounted disk\n");
		return;
	}

	struct tfs_super super;
	if (disk_read(0, &super))
		return;
	printf("\033[97mFilesystem superblock:\033[0m\n");
	printf("  total_blocks: %d\n", super.total_blocks);
	printf("  bitmap_blocks: %d\n", super.bitmap_blocks);
	printf("  bitmap_offset: %d\n\n", super.bitmap_offset);
}

void tfs_print_usage() {
	if (!mounted) {
		fprintf(stderr, "Error @ tfs_print_usage: no mounted disk\n");
		return;
	}

	struct tfs_super super;
	if (disk_read(0, &super))
		return;

	uint8_t bitmap[512];
	int used_blocks = 0;
	for (uint32_t i = 0; i < super.bitmap_blocks; i++) {
		if (disk_read(super.total_blocks - super.bitmap_blocks + i, bitmap))
			return;
		for (int j = 0; j < 512; j++) {
			for (int k = 0; k < 8; k++) {
				if (bitmap[j] & (1 << k))
					used_blocks++;
			}
		}
	}

	printf("\033[97mFilesystem usage:\033[0m\n");
	printf("  total sectors: %d\n", super.total_blocks);
	printf("  used sectors: %d\n", used_blocks);
	printf("  used: %d%%\n\n", (used_blocks * 100) / super.total_blocks);
}

void tfs_print_child_node(int, char*, bool);
void tfs_print_node(int block, char *indent) {
	struct tfs_node node;
	if (disk_read(block, &node))
		return;

	printf("%s", node.name);
	if (node.type == TFS_DIRECTORY) {
		printf("/\n");
		int child_block = node.pointer;
		struct tfs_node child;
		while (child_block != 0) {
			if (disk_read(child_block, &child))
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
