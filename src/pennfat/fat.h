#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define MAX_FILENAME 32

static int FAT_BLOCK_SIZE[] = {256, 512, 1024, 2048, 4096};

/* ------------------------------------------------------------------------
--------------------------------- Dir Entry -------------------------------
------------------------------------------------------------------------*/

// Dir entry
typedef struct dirEntry {
    char name[MAX_FILENAME]; // name[0] - 0: end of directory; 1: deleted entry; the file is also deleted; 2: deleted entry; the file is still being used
    uint32_t size;
    uint16_t firstBlock;
    uint8_t type; // – 0: unknown; 1: a regular file; 2: a directory file; 4: a symbolic link
    uint8_t perm; // 0: none; 2: write only; 4: read only; 5: read and executable (shell scripts); 6: read and write; 7: read, write, and executable
    time_t mtime;

    uint8_t reserved[16]; // For extra credits
} dirEntry;

// Dir entry Linked-list struct
typedef struct dirEntryNode {
    dirEntry *entry;
    struct dirEntryNode *next;
} dirEntryNode;

dirEntryNode *initDirEntryNode(char *fileName, uint32_t size, uint16_t firstBlock, uint8_t type, uint8_t perm, time_t time); // Create a new file entry
void freeDirEntryNode(dirEntryNode *fNode);                                                                                  // Free the file entry node

/* ------------------------------------------------------------------------
---------------------------------- Penn Fat -------------------------------
------------------------------------------------------------------------*/

typedef struct pennfat {
    char *fileName; // Filename on disk

    uint8_t totalBlocks; // FAT blocks number
    uint32_t freeBlocks; // Free block number
    uint32_t blockSize;  // Block size

    uint32_t numEntries; // Entry number
    uint32_t numFile;    // File number

    dirEntryNode *head; // First node in the file entry linked-list
    dirEntryNode *tail; // Last node in the file entry linked-list

    uint16_t *blocks; // Blocks metadata
} pennfat;

pennfat *initFat(char *fileName, uint8_t totalBlocks, uint8_t blockSizeIndex, bool creating);
int loadDirEntries(pennfat *fat);
pennfat *loadFat(char *fileName);
int saveFat(pennfat *fat);
void freeFat(pennfat **fat);

/* PROGRESS NOTES:
FUNCTION_NAME       IMPLEMENTATION      TESTING
initDirEntryNode    Done
freeDirEntryNode    Done
initFat             Done
loadDirEntry        Done
loadFat             Done
saveFat             Done
freeFat             Done
*/