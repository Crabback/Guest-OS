#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#include "file.h"
#include "utils.h"

dirEntryNode *initDirEntryNode(char *fileName, uint32_t size, uint16_t firstBlock, uint8_t type, uint8_t perm, time_t time) {
    dirEntryNode *newNode = malloc(sizeof(dirEntryNode));

    newNode->entry = malloc(sizeof(dirEntry));
    newNode->next = NULL;

    dirEntry *entry = newNode->entry;
    entry->size = size;
    entry->firstBlock = firstBlock;
    entry->type = type;
    entry->perm = perm;
    entry->mtime = time;

    for (int i = 0; i < 32; i++) {
        entry->name[i] = '\0';
    }
    strcpy(entry->name, fileName);

    for (int i = 0; i < 16; i++) {
        entry->reserved[i] = '\0';
    }
    
    return newNode;
}

void freeDirEntryNode(dirEntryNode *fNode) {
    free(fNode->entry);
    free(fNode);
}

pennfat *initFat(char *fileName, uint8_t totalBlocks, uint8_t blockSizeIndex, bool creating) {
    // Check FAT block size
    if (totalBlocks < 1 || totalBlocks > 32) {
        printf("WARNING: Number of blocks should be [1-32].\n");
        return NULL;
    }

    // check if numBlocks is valid
    if (blockSizeIndex < 1 || blockSizeIndex > 4) {
        printf("Block size config must be between 1 and 4.\n");
        return NULL;
    }

    pennfat *newFAT = malloc(sizeof(pennfat));
    if (newFAT == NULL) {
        perror("ERROR: Fail to malloc the fat.\n");
        return NULL;
    }
    
    // Set Fat filename
    int len = strlen(fileName);
    newFAT->fileName = malloc(len * sizeof(uint8_t) + 1);
    for (int i = 0; i < len + 1; i++) {
        newFAT->fileName[i] = '\0';
    }
    strcpy(newFAT->fileName, fileName);

    newFAT->totalBlocks = totalBlocks;
    newFAT->blockSize = FAT_BLOCK_SIZE[blockSizeIndex];

    newFAT->numEntries = (newFAT->blockSize * newFAT->totalBlocks) / 2;

    newFAT->numFile = 0;
    newFAT->head = NULL;
    newFAT->tail = NULL;

    newFAT->freeBlocks = newFAT->numEntries - 2;

    int f;
    if (creating) {
        // Open the file
        if ((f = open(fileName, O_RDWR | O_TRUNC | O_CREAT, 0644)) == -1) {
            perror("ERROR: Fail to open the file.");
            return NULL;
        }

        // Truncate the file if too large
        if (ftruncate(f, newFAT->totalBlocks * newFAT->blockSize) == -1) {
            perror("ERROR: Fail to truncate the file.");
            return NULL;
        }
    } else {
        // otherwise, just load the file
        if ((f = open(fileName, O_RDWR, 0644)) == -1) {
        perror("ERROR: Fail to open the file.");
        return NULL;
        }
    }
    
    // Map FAT table in memory to disk
    newFAT->blocks = mmap(NULL, newFAT->totalBlocks * newFAT->blockSize, PROT_READ | PROT_WRITE, MAP_SHARED, f, 0);
    if (newFAT->blocks == MAP_FAILED) {
        perror("ERROR: Fail to map FAT.\n");
        return NULL;
    }

    // Close the file
    if (close(f) == -1) {
        perror("ERROR: Fail to close the file.");
        return NULL;
    }

    // Store FAT metadata
    newFAT->blocks[0] = (uint16_t) totalBlocks << 8 | blockSizeIndex;
    #ifdef DEBUGGING
        printf("Storing the FAT metadata at %d\n", newFAT->blocks[0]);
    #endif

    // Link root directory for first init
    if (newFAT->blocks[1] == 0x0000) {
        #ifdef DEBUGGING
            printf("Link the first init (%d) by the root directory ", newFAT->blocks[1]);
        #endif
        newFAT->blocks[1] = 0xFFFF;
        #ifdef DEBUGGING
            printf("%d\n", newFAT->blocks[1]);
        #endif
    }

    #ifdef DEBUGGING
        if(creating) {
            printf("Creating new FAT...\n");
        } else {
            printf("Loading the FAT in initFAT...\n");
        }
        printf("newFAT->fileName =  %s\n", newFAT->fileName);
        printf("newFAT->totalBlocks =  %d\n", newFAT->totalBlocks);
        printf("newFAT->freeBlocks =  %d\n", newFAT->freeBlocks);
        printf("newFAT->blockSize =  %d\n", newFAT->blockSize);
        printf("newFAT->numEntries =  %d\n", newFAT->numEntries);
        printf("newFAT->numFile =  %d\n", newFAT->numFile);
        printf("newFAT->blocks[0] =  %d\n", newFAT->blocks[0]);
        printf("newFAT->blocks[1] =  %d\n", newFAT->blocks[1]);
    #endif

    return newFAT;
}

int loadDirEntries(pennfat *fat) {
    // Directory entry already initialized
    if (fat->numFile != 0) {
        writeHelper("Directory entry already initialized.\n");
        return -1;
    }

    file *file = getAllFile(fat);

    if (file == NULL) {
        #ifdef DEBUGGING
            printf("Directory file did not initialized.\n");
        #endif
        return 0;
    }
    
    // Create new dir node
    for (int i = 0; i < file->len; i = i + 64) {
        dirEntryNode *newNode = malloc(sizeof(dirEntryNode));
        newNode->next = NULL;

        if (newNode == NULL) {
            perror("ERROR: Fail to malloc the file.");
            return -1;
        }

        dirEntry *newEntry = malloc(sizeof(dirEntry));
        if (newEntry == NULL) {
            free(newNode);
            perror("ERROR: Fail to malloc the file.");
            return -1;
        }

        memcpy((uint8_t *) &newEntry->name,         &file->contents[i],         32 * sizeof(uint8_t));
        memcpy((uint8_t *) &newEntry->size,         &file->contents[i + 32],     4 * sizeof(uint8_t));
        memcpy((uint8_t *) &newEntry->firstBlock,   &file->contents[i + 36],     2 * sizeof(uint8_t));
        memcpy((uint8_t *) &newEntry->type,         &file->contents[i + 38],     1 * sizeof(uint8_t));
        memcpy((uint8_t *) &newEntry->perm,         &file->contents[i + 39],     1 * sizeof(uint8_t));
        memcpy((uint8_t *) &newEntry->mtime,        &file->contents[i + 40],     8 * sizeof(uint8_t));
        memcpy((uint8_t *) &newEntry->reserved,     &file->contents[i + 48],    16 * sizeof(uint8_t));

        newNode->entry = newEntry;

        if (fat->numFile == 0) { // FAT is empty
            // initialize FAT
            fat->head = newNode;
            fat->tail = newNode;
        } else {
            // Append FAT
            fat->tail->next = newNode;
            fat->tail = newNode;
        }

        // Get the number of freeblocks
        fat->freeBlocks -= ceil((double) newEntry->size / fat->blockSize);

        // Increment numFile
        fat->numFile++;
    }

    freeFile(file);
    return 0;
}

pennfat *loadFat(char *fileName) {
    int f;
    if ((f = open(fileName, O_RDONLY, 0644)) == -1) {
        perror("ERROR: Fail to open the file.");
        return NULL;
    }

    // Get the blockSizeIndex
    uint8_t blockSizeIndex = 0;
    if (read(f, &blockSizeIndex, sizeof(uint8_t)) == -1) {
        return NULL;
    }
    #ifdef DEBUGGING
        printf("blockSizeIndex is %d\n", blockSizeIndex);
    #endif

    // Get the totalBlocks
    uint8_t totalBlocks = 0;
    if (read(f, &totalBlocks, sizeof(uint8_t)) == -1) {
        perror("ERROR: Fail to read the file.");
        return NULL;
    }
    #ifdef DEBUGGING
        printf("totalBlocks is %d\n", totalBlocks);
    #endif

    if (close(f) == -1) {
        perror("ERROR: Fail to close the file.");
        return NULL;
    }

    // Overwrite the FAT
    pennfat *output = initFat(fileName, totalBlocks, blockSizeIndex, false);

    if (output == NULL) {
        printf("ERROR: Fail to load FAT.\n");
        return NULL;
    }

    if (loadDirEntries(output) == -1) {
        freeFat(&output);
        return NULL;
    }

    return output;
}

int saveFat(pennfat *fat) {
    if (fat == NULL) {
        printf("WARNING: The FAT is NULL.\n");
        return -1;
    }

    #ifdef DEBUGGING
        writeHelper("Saving the Fat...");
    #endif
    if (writeDirEntries(fat) == -1) {
        printf("ERROR: Fail to write directory entries\n");
        return -1;
    }

    return 0;
}

void freeFat(pennfat **fat) {
    struct pennfat *thisFat = (*fat);

    if (thisFat == NULL) {
        return;
    }

    // Free fileName
    if (thisFat->fileName != NULL)
        free(thisFat->fileName);

    // Free directory entries
    while (thisFat->head != NULL) {
        dirEntryNode *curr = thisFat->head;
        thisFat->head = curr->next;
        freeDirEntryNode(curr);
    }

    // Unmap FAT
    if (munmap(thisFat->blocks, thisFat->totalBlocks * thisFat->blockSize) == -1) {
        perror("ERROR: Fail to unmap FAT.\n");
        return;
    }

    free(thisFat);
    *fat = NULL;
}