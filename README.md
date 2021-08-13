# Tiago's File System

An extremely simple file system design. Doesn't support much besides directories and files. It's currently only used by my hobby operating system, and to be honest, it's better to keep it this way. Because it sucks.

## File System Format Overview

The file system uses different areas on the physical media for different purposes. The areas are:

| Offset | Blocks | Description |
| ------ | ------ | ----------- |
| 0      | 1      | Super-block |
| 1      | 2047   | Reserved    |
| 2048   | ?      | Data        |
| ?      | ?      | Bitmap      |

## Super-Block Format

| Offset | Size | Description    |
| ------ | ---- | -------------- |
| 0x000  | 486  | Boot code      |
| 0x1E6  | 8    | Total blocks   |
| 0x1EE  | 8    | Bitmap blocks  |
| 0x1F6  | 8    | Bitmap offset  |
| 0x1FE  | 2    | Boot signature |

## Reserved Area Format

The reserved area immediately follows the super-block, and spans 2047 blocks. It may be used for any number of things that are outside the scope of this specification. All data within the reserved area must be ignored.

## Data Area Format

The data area is used to store all node and file data. It always starts with the root block at LBA 2048, and it has a total size of `total_blocks - bitmap_blocks - 2048`.

### Data Area Node Format

| Offset | Size | Description |
| ------ | ---- | ----------- |
| 0x000  | 8    | Index       |
| 0x008  | 8    | Parent      |
| 0x010  | 8    | Child       |
| 0x018  | 8    | Next        |
| 0x020  | 8    | Size        |
| 0x028  | 8    | Time        |
| 0x030  | 4    | Type        |
| 0x034  | 460  | Name        |

`index` is the block index associated with this node.

`parent` contains the index of the node of it's parent directory.

`child` points to the first pointer node if it's a file, or points to the first child node if it's a directory.

`next` contains the index of the node of the next entry in it's parent directory.

`size` is the file size in bytes, or the number of children of a directory.

`time` is the unix epoch time in which the entry was last modified.

`type` could either be 0, which indicates a file, or 1, which indicates a directory.

`name` is a 460 character sequence containing the name of the entry.

### Data Area Pointer Node Format

| Offset | Size | Description |
| ------ | ---- | ----------- |
| 0x000  | 504  | Pointer[63] |
| 0x1F8  | 8    | Next        |

`pointer` contains 63 indices for the file data.

`next` points to the next pointer node.

In order to access a specific `offset` byte in a file, you should skip `(offset / 512) / 63` nodes, look for the pointer number `(offset / 512) % 63)` and get the `offset % 512`th byte in the block.

## Bitmap Area format

The bitmap area is used to store information about which blocks are used. It has a length of `ceil(total_blocks / 4096)` and ends at the last block, so it starts at `total_blocks - bitmap_blocks`.

Information is stored such as if the bit at the nth position is set, the block is used, otherwise the block is available.
