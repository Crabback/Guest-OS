#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "../pennos/mounted_fat.h"
#include "file.h"
#include "pennfat_handler.h"
#include "utils.h"

int bytesToBlocks(int numBytes, pennfat *fat) { return ceil((double)numBytes / fat->blockSize); }

void freeFile(file *file) {
    free(file->contents);
    free(file);
}

void getDirEntryNode(dirEntryNode **prev, dirEntryNode **target, char *fileName, pennfat *fat) {
    dirEntryNode *prevNode = NULL;
    dirEntryNode *targetNode = fat->head;

    if (fileName == NULL) {
        return;
    }

    while (targetNode != NULL) {
        if (strcmp(targetNode->entry->name, fileName) == 0) {
            break;
        } else {
            prevNode = targetNode;
            targetNode = targetNode->next;
        }
    }

    if (prev != NULL) {
        *prev = prevNode;
    }

    if (target != NULL) {
        *target = targetNode;
    }
}

file *getAllFile(pennfat *fat) {
    // Open the file
    int fd;
    if ((fd = open(fat->fileName, O_RDWR, 0644)) == -1) {
        perror("ERROR: Fail to open the file");
        return NULL;
    }

    // Seek to the first block
    uint32_t fatSize = fat->totalBlocks * fat->blockSize;
    uint16_t currIndex = 1;

    if (lseek(fd, fatSize, SEEK_SET) == -1) {
        perror("ERROR: Fail to find the first block.\n");
        return NULL;
    }

    // Track counted files
    unsigned int filesCounted = 0;

    // Buffer for read
    uint8_t buffer[sizeof(dirEntry)];
    for (int i = 0; i < sizeof(dirEntry); i++) {
        buffer[i] = 0x00;
    }

    bool endedAtEdgeOfBlock = false;
    while (1) {
        if (filesCounted != 0 && (filesCounted * sizeof(dirEntry)) % fat->blockSize == 0) {
            if (fat->blocks[currIndex] == 0xFFFF) {
                // Last block
                endedAtEdgeOfBlock = true;
                break;
            }
            // Get next block
            currIndex = fat->blocks[currIndex];
            if (lseek(fd, fatSize + ((currIndex - 1) * fat->blockSize), SEEK_SET) == -1) {
                perror("ERROR: Fail to lseek the file.");
                return NULL;
            }
        }

        if (read(fd, buffer, sizeof(dirEntry)) == -1) {
            perror("ERROR: Fail to read the file.");
            return NULL;
        }

        if (buffer[0] == 0x00) {
            break;
        } else {
            filesCounted++;
        }
    }

    // Close fd
    f_close(fd);

#ifdef DEBUGGING
    writeHelper("Finishing counting files...");
    printf("filesCounted = %d\n", filesCounted);
#endif

    if (filesCounted == 0) {
        return NULL;
    } else {
        uint8_t *contents = getContents(1, filesCounted * sizeof(dirEntry) + endedAtEdgeOfBlock, fat);

        // Check if getContents() failed
        if (contents == NULL) {
            perror("ERROR: Fail to get the content.");
            return NULL;
        }

        file *result = malloc(sizeof(file));
        result->contents = contents;
        result->len = filesCounted * sizeof(dirEntry);
        result->type = DIRECTORY_FILETYPE;
        result->perm = NONE_PERMS;

        return result;
    }
}

uint8_t *getContents(uint16_t startIndex, uint32_t length, pennfat *fat) {
    uint8_t *result = malloc(length * sizeof(uint8_t) + 1);
    if (result == NULL) {
        perror("ERROR: Fail to malloc.");
        return NULL;
    }

    // Add null terminator
    result[length] = '\0';

    // Open the file
    int fd;
    if ((fd = open(fat->fileName, O_RDONLY, 0644)) == -1) {
        perror("ERROR: Fail to open the file.");
        free(result);
        return NULL;
    }

    // Read the content
    uint32_t fatSize = fat->totalBlocks * fat->blockSize;
    uint16_t currIndex = startIndex;

    if (lseek(fd, fatSize + ((currIndex - 1) * fat->blockSize), SEEK_SET) == -1) {
        perror("ERROR: Fail to lseek the file.");
        free(result);
        return NULL;
    }

    for (int i = 0; i < length; i = i + fat->blockSize) {
        if (i != 0 && i % fat->blockSize == 0) {
            // Get the next block
            currIndex = fat->blocks[currIndex];
            if (lseek(fd, fatSize + ((currIndex - 1) * fat->blockSize), SEEK_SET) == -1) {
                perror("ERROR: fail to lseek the file.");
                free(result);
                return NULL;
            }
        }

        int bytesToRead = fat->blockSize;
        if (bytesToRead > length - i) {
            bytesToRead = length - i;
        }

        if (read(fd, &result[i], bytesToRead) == -1) {
            perror("ERROR: fail to lseek the file.");
            free(result);
            return NULL;
        }
    }

    // Close the file
    if (close(fd) == -1) {
        free(result);
        return NULL;
    }

    return result;
}

file *readFile(char *fileName, pennfat *fat) {
    // Find the directory entry contain the file
    dirEntryNode *entryNode;
    getDirEntryNode(NULL, &entryNode, fileName, fat);

    if (entryNode == NULL) {
        printf("Error: Cannot found %s.\n", fileName);
        return NULL;
    }

    // Check read permission
    if (entryNode->entry->perm != READWRITE_PERMS && entryNode->entry->perm != READ_PERMS) {
        printf("Error: Lack of read permission for %s.\n", fileName);
        return NULL;
    }

    // Read the file
    file *result = malloc(sizeof(file));
    if (result == NULL) {
        perror("ERROR: Fail to malloc the file.");
        return NULL;
    }

    result->contents = getContents(entryNode->entry->firstBlock, entryNode->entry->size, fat);

    if (result->contents == NULL) {
        free(result);
        return NULL;
    }
    result->len = entryNode->entry->size;
    result->type = entryNode->entry->type;
    result->perm = entryNode->entry->perm;

    return result;
}

void deleteFileHelper(dirEntryNode *prev, dirEntryNode *entryNode, pennfat *fat, bool dirFile) {
    // Clear blocks
    uint16_t currBlock;
    if (dirFile) {
        currBlock = 1;
    } else {
        currBlock = entryNode->entry->firstBlock;
    }

    if (dirFile || entryNode->entry->size != 0) {
        // Delete all blocks for this file
        do {
            uint16_t nextBlock = fat->blocks[currBlock];
            fat->blocks[currBlock] = 0;
            currBlock = nextBlock;
        } while (currBlock != 0xFFFF && currBlock != 0x0000);
    }
}

int deleteFile(char *fileName, pennfat *fat, bool flag) {
    // Find the corresponidng directory entry
    dirEntryNode *prev;
    dirEntryNode *entryNode;
    getDirEntryNode(&prev, &entryNode, fileName, fat);

    if (entryNode == NULL) {
        printf("ERROR: Fail to find %s.\n", fileName);
        return -1;
    }

    // Check write permissions
    if (!flag && entryNode != NULL && entryNode->entry->perm != WRITE_PERMS && entryNode->entry->perm != READWRITE_PERMS) {
        printf("ERROR: Fail to delete the file %s due to lack of write permission.\n", fileName);
        return -1;
    }

    // Delete block
    deleteFileHelper(prev, entryNode, fat, false);

    // Delete the entry node
    if (entryNode == fat->head) {
        fat->head = entryNode->next;
    } else {
        prev->next = entryNode->next;
    }

    // Set the tail
    if (entryNode == fat->tail) {
        fat->tail = prev;
    }

    // Decrement numFile and increase freeblocks
    fat->numFile--;
    fat->freeBlocks += bytesToBlocks(entryNode->entry->size, fat);

    // Free the node
    freeDirEntryNode(entryNode);

    return 0;
}

int renameFile(char *oldFileName, char *newFileName, pennfat *fat) {
    // get directory entry for filename
    dirEntryNode *entryNode;
    getDirEntryNode(NULL, &entryNode, oldFileName, fat);

    if (entryNode == NULL) {
        printf("ERROR: file %s does not exist.\n", oldFileName);
        return -1;
    }

    // Check permission
    if (entryNode->entry->perm == NONE_PERMS || entryNode->entry->perm == READ_PERMS) {
        printf("%s lacks write permission\n", oldFileName);
        return -1;
    }

    // Check if file with newFileName already exists
    dirEntryNode *newFileNode;
    getDirEntryNode(NULL, &newFileNode, newFileName, fat);

    // Delete the exisitng file
    if (newFileNode != NULL) {
        if (deleteFile(newFileNode->entry->name, fat, false) == -1) {
            printf("Failed to overwrite %s\n", newFileNode->entry->name);
        }
    }

    // Rename
    strcpy(entryNode->entry->name, newFileName);

    // Update timestamp
    entryNode->entry->mtime = time(NULL);
    fat->head->entry->mtime = entryNode->entry->mtime;

    return 0;
}

int writeFile(char *fileName, uint8_t *bytes, uint32_t offset, uint32_t len, uint8_t type, uint8_t perm, pennfat *fat, bool appending, bool flag, bool writeDir) {
    // Find the corresponidng directory entry
    dirEntryNode *prev;
    dirEntryNode *entryNode;
#ifdef DEBUGGING
    writeHelper("Getting dirctory entry node\n");
#endif
    getDirEntryNode(&prev, &entryNode, fileName, fat);

// Check write permissions
#ifdef DEBUGGING
    writeHelper("Checking write perm\n");
#endif
    if (!flag && entryNode != NULL && entryNode->entry->perm != WRITE_PERMS && entryNode->entry->perm != READWRITE_PERMS) {
        printf("ERROR: Fail to delete the file %s due to lack of write permission.\n", fileName);
        return -1;
    }

// Get the number of free blocks needed
#ifdef DEBUGGING
    writeHelper("Getting number of free blocks needed\n");
#endif
    int32_t newNumOfFreeBlocks = 0;

    if (flag && writeDir) {
        // Do not need to create new directory entires
    } else if (entryNode == NULL) {
        // Need new directory entries
        if (fat->numFile != 0 && (sizeof(dirEntry) * fat->numFile) % fat->blockSize == 0) {
            newNumOfFreeBlocks -= 1;
        }
        newNumOfFreeBlocks -= bytesToBlocks(len, fat);
    } else if (appending) {
        // Appending the file
        if (entryNode->entry->size % fat->blockSize == 0) {
            newNumOfFreeBlocks -= bytesToBlocks(len, fat);
        } else {
            newNumOfFreeBlocks -= bytesToBlocks(len - (fat->blockSize - (entryNode->entry->size % fat->blockSize)), fat);
        }
    } else if (offset > 0) {
        newNumOfFreeBlocks -= bytesToBlocks(offset + len, fat) - bytesToBlocks(entryNode->entry->size, fat);
    } else {
        newNumOfFreeBlocks -= bytesToBlocks(len, fat) - bytesToBlocks(entryNode->entry->size, fat);
    }

    // Fail to find enough space
    if ((int32_t)fat->freeBlocks + newNumOfFreeBlocks < 0) {
        printf("ERROR: Fail to find enough free blocks, %d blocks required, %d blocks is free.\n", -newNumOfFreeBlocks, fat->freeBlocks);
        return -1;
    }

// If not appending, delete the exisitng file
#ifdef DEBUGGING
    writeHelper("Deleting the existing file\n");
#endif
    if (writeDir || (entryNode != NULL && !appending)) {
        deleteFileHelper(prev, entryNode, fat, flag && writeDir);
    }

    uint16_t currIndex = 1;
    uint32_t thisOffset = 0;

#ifdef DEBUGGING
    writeHelper("Checking the write type\n");
#endif
    if (flag && writeDir) {
// Writing
#ifdef DEBUGGING
        writeHelper("Flag is Writing\n");
#endif
        currIndex = 1;
        thisOffset = 0;
    } else if (appending && entryNode != NULL && entryNode->entry->size != 0) {
// Appending
#ifdef DEBUGGING
        writeHelper("Flag is Appending\n");
#endif
        currIndex = entryNode->entry->firstBlock;
        while (fat->blocks[currIndex] != 0xFFFF) {
            currIndex = fat->blocks[currIndex];
        }

        if (entryNode->entry->size % fat->blockSize == 0) {
            // Find the free free block
            uint16_t nextIndex = 0;
            for (unsigned int i = 1; i < fat->numEntries; i++) {
                if (fat->blocks[i] == 0) {
                    nextIndex = i;
                    break;
                }
            }
            fat->blocks[currIndex] = nextIndex;
            currIndex = nextIndex;
        }

// Get the current offset
#ifdef DEBUGGING
        writeHelper("Getting the current offset\n");
#endif
        thisOffset = entryNode->entry->size % fat->blockSize;
    } else if (offset > 0 && entryNode != NULL) {
#ifdef DEBUGGING
        writeHelper("Writing in offset\n");
#endif
        if (offset > entryNode->entry->size) {
            printf("ERROR: Offset is greater than file length.\n");
            return -1;
        }
        currIndex = entryNode->entry->firstBlock;

        int blockAt = offset / fat->blockSize;
        for (int i = 0; i < blockAt; i++) {
            currIndex = fat->blocks[currIndex];
        }

        if (entryNode->entry->size % fat->blockSize == 0) {
            // Find the free free block
            uint16_t nextIndex = 0;
            for (unsigned int i = 1; i < fat->numEntries; i++) {
                if (fat->blocks[i] == 0) {
                    nextIndex = i;
                    break;
                }
            }
            fat->blocks[currIndex] = nextIndex;
            currIndex = nextIndex;
        }

        // Get the current offset
        thisOffset = offset % fat->blockSize;
    } else {
// Get the first free block
#ifdef DEBUGGING
        writeHelper("Getting the first free block\n");
#endif
        for (unsigned int i = 1; i < fat->numEntries; i++) {
#ifdef DEBUGGING
            printf("Iterating through all blocks, curr block = %d\n", i);
            printf("fat->numEntries is %d\n", fat->numEntries);
#endif
            if (fat->blocks[i] == 0) {
#ifdef DEBUGGING
                printf("Looking at block %d = %d\n", i, fat->blocks[i]);
#endif
                currIndex = i;
                break;
            }
        }
    }

    // Get the first index
    uint16_t firstIndex = currIndex;

    int fd;
#ifdef DEBUGGING
    writeHelper("Opening the des file\n");
#endif
    if ((fd = open(fat->fileName, O_WRONLY | O_CREAT, 0644)) == -1) {
        perror("ERROR: Fail to open the file.");
        return -1;
    }

// Seek to the first empty block
#ifdef DEBUGGING
    writeHelper("Finding the first empty block\n");
#endif
    uint32_t fatSize = fat->totalBlocks * fat->blockSize;
    if (lseek(fd, fatSize + ((currIndex - 1) * fat->blockSize) + thisOffset, SEEK_SET) == -1) {
        perror("ERROR: Fail to lseek the block.");
        return -1;
    }

// Write the content
#ifdef DEBUGGING
    writeHelper("Writing...\n");
#endif
    int byteIdx = 0;
    while (byteIdx < len) {
        if (byteIdx != 0 && (byteIdx + thisOffset) % fat->blockSize == 0) {
            if (fat->blocks[currIndex] == 0x0000 || fat->blocks[currIndex] == 0xFFFF) {
                // Find the new block to write
                uint16_t nextIndex = currIndex + 1;
                if (nextIndex >= fat->numEntries) {
                    nextIndex = 2;
                }

                for (unsigned int j = nextIndex; j < fat->numEntries; j++) {
                    if (fat->blocks[j] == 0) {
                        nextIndex = j;
                        break;
                    }
                }
                fat->blocks[currIndex] = nextIndex;
                currIndex = nextIndex;
                if (lseek(fd, fatSize + ((currIndex - 1) * fat->blockSize), SEEK_SET) == -1) {
                    perror("ERROR: Fail to lseek the block.");
                    return -1;
                }
            } else {
                currIndex = fat->blocks[currIndex];
            }
        }

        // Write either enough bytes to get to the end of this block or the number of bytes to the end of the file
        int bytesToWrite = fat->blockSize - (byteIdx + thisOffset) % fat->blockSize;
        if (bytesToWrite > len - byteIdx) {
            bytesToWrite = len - byteIdx;
        }

        int totalBytesWritten = 0;
        while (totalBytesWritten < bytesToWrite) {
            int bytesWritten = write(fd, &bytes[byteIdx], bytesToWrite);
            if (bytesWritten == -1) {
                return -1;
            }
            totalBytesWritten += bytesWritten;
        }
        byteIdx = byteIdx + bytesToWrite;
    }

// Set the end of the file
#ifdef DEBUGGING
    writeHelper("Setting the end of the file\n");
#endif
    if (len != 0) {
        fat->blocks[currIndex] = 0xFFFF;
    } else {
        firstIndex = 0x0000;
    }

    if (f_close(fd) == -1) {
        return -1;
    }

// Create a new directory entry if needed
#ifdef DEBUGGING
    writeHelper("Creating a new directory entry if needed\n");
#endif
    if (flag && writeDir) {
        // Do not need new directory entry
    } else if (entryNode == NULL) {
        dirEntryNode *newNode = initDirEntryNode(fileName, len, firstIndex, type, perm, time(NULL));

        if (fat->numFile == 0) {
            // Initialize the fat
            fat->head = newNode;
            fat->tail = newNode;
        } else {
            // Add new entry to the fat
            fat->tail->next = newNode;
            fat->tail = newNode;
        }

        // Update block count
        fat->numFile++;
    } else {
        // Update existing entry
        if (offset > 0 && offset + len > entryNode->entry->size) {
            entryNode->entry->size = offset + len;
        } else if (appending) {
            entryNode->entry->size += len;
        } else {
            entryNode->entry->size = len;
        }
        if (!appending)
            entryNode->entry->firstBlock = firstIndex;
        entryNode->entry->mtime = time(NULL);
    }

    // Update free block count
    fat->freeBlocks += newNumOfFreeBlocks;

#ifdef DEBUGGING
    writeHelper("Finishing writing\n");
#endif

    return 0;
}

int appendFile(char *fileName, uint8_t *bytes, uint32_t len, pennfat *fat, bool flag) { return writeFile(fileName, bytes, 0, len, REGULAR_FILETYPE, READWRITE_PERMS, fat, true, flag, false); }

int writeDirEntries(pennfat *fat) {
    uint32_t fileSize = fat->numFile * sizeof(dirEntry);
    uint32_t length = fileSize;
    if (fileSize % fat->blockSize != 0)
        length += 1;

    uint8_t bytes[length];

    if (fileSize % fat->blockSize != 0)
        bytes[length - 1] = 0x00;

    int bufIdx = 0;
    dirEntryNode *entryNode = fat->head;
    while (entryNode != NULL) {
        memcpy(&bytes[bufIdx], entryNode->entry, sizeof(dirEntry));
        entryNode = entryNode->next;
        bufIdx += sizeof(dirEntry);
    }

    if (writeFile(NULL, bytes, 0, length, DIRECTORY_FILETYPE, NONE_PERMS, fat, false, true, true) == -1) {
        printf("ERROR: Failed to wrtie the directory file.\n");
        return -1;
    }

    return 0;
}

int chmodFile(pennfat *fat, char *fileName, int newPerms) {
    dirEntryNode *entryNode;
    getDirEntryNode(NULL, &entryNode, fileName, fat);

    if (entryNode == NULL) {
        printf("ERROR: Fail to find %s.\n", fileName);
        return -1;
    }

    if (newPerms != NONE_PERMS && newPerms != WRITE_PERMS && newPerms != READ_PERMS && newPerms != READEXE_PERMS && newPerms != READWRITE_PERMS && newPerms != READWRITEEXE_PERMS) {
        printf("ERROR: Invalid new permission %02x.\n", newPerms);
        return -1;
    }

    entryNode->entry->perm = newPerms;

    return 0;
}

int f_open(const char *filename, int mode) {
    int fd;
    int flag = -1;

    switch (mode) {
    case 0: // Read
        flag = O_RDONLY;
        break;
    case 1: // Write
        flag = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case 2: // Append
        flag = O_WRONLY | O_CREAT | O_APPEND;
        break;
    default:
        fprintf(stderr, "Invalid mode\n");
        return -1;
    }

    if ((fd = open(filename, flag, 0644)) == -1) {
        perror("ERROR: Fail to open the file.");
        return -1;
    }

    return fd;
}

int f_close(int fd) {
    int result = close(fd);
    if (result < 0) {
        perror("close");
        return -1;
    }
    return 0;
}

int f_read(int fd, int n, char *buf) {
    if (read(fd, buf, n) == -1) {
        perror("ERROR: Fail to read the file.");
        return -1;
    }
    return 0;
}

int f_write(int fd, char *str, int n) {
    if (write(fd, str, n) == -1) {
        perror("ERROR: fail to write the file.");
        return -1;
    }
    return 0;
}

int f_unlink(char *filename) {
    if (deleteFile(filename, mounted_fat, false) == -1) {
        return -1;
    }
    saveFat(mounted_fat);
    return 0;
}

int f_lseek(int fd, int offset, int whence) {
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("ERROR: fail to lseek the file.");
        return -1;
    }

    return 0;
}