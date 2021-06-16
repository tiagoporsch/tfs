# Tiago's File System

An extremely simple file system design. Doesn't support anything besides directories and files with contiguous data. It's currently only used by my hobby operating system, and to be honest, it's better to keep it this way. Because it sucks.

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
| 0x000  | 498  | Boot code      |
| 0x1F2  | 4    | Total blocks   |
| 0x1F6  | 4    | Bitmap blocks  |
| 0x1FA  | 4    | Bitmap offset  |
| 0x1FE  | 2    | Boot signature |

## Reserved Area Format

The reserved area immediately follows the super-block, and spans 2047 blocks. It may be used for any number of things that are outside the scope of this specification. All data within the reserved area must be ignored.

## Data Area Format

The data area is used to store all node and file data. It always starts with the root block at LBA 2048, and it has a total size of `total_blocks - bitmap_blocks - 2048`.

The data for any specific file always consumes sequential blocks in the data area (file fragmentation is not supported), so in the case of appending, if there's not enought free blocks right after the last data block, the entire contents must be rewritten somewhere else.

### Data Area Node Format

| Offset | Size | Description |
| ------ | ---- | ----------- |
| 0x000  | 4    | Type        |
| 0x004  | 4    | Pointer     |
| 0x008  | 8    | Size        |
| 0x010  | 8    | Time        |
| 0x018  | 480  | Name        |
| 0x1F8  | 4    | Parent      |
| 0x1FC  | 4    | Next        |

`type` could either be 1, which indicates a directory, or 2, which indicates a file.

`pointer` points to the first data block if it's a file, or points to the first child node if it's a directory.

`size` is the file size in bytes (only applies to files).

`time` is the unix epoch time in which the entry was last modified.

`name` is a 480 character sequence containing the name of the entry.

`parent` contains the index of the node of it's parent directory.

`next` contains the index of the node of the next entry in it's parent directory.

## Bitmap Area format

The bitmap area is used to store information about which blocks are used. It has a length of `ceil(total_blocks / 4096)` and ends at the last block, so it starts at `total_blocks - bitmap_blocks`.

Information is stored such as if the bit at the nth position is set, the block is used, otherwise the block is available.
