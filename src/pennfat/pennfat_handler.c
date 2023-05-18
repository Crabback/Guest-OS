#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pennfat_handler.h"
#include "utils.h"

int pennfatMkfs(char *fileName, uint8_t numBlocks, uint8_t blockSizeIndex, pennfat **fat) {
    if (fat != NULL) {
        freeFat(fat);
    }

    *fat = initFat(fileName, numBlocks, blockSizeIndex, true);
    if (*fat == NULL) {
        printf("ERROR: Fail to initialize FAT.\n");
        return -1;
    }

    return 0;
}

int pennfatMount(char *fileName, pennfat **fat) {
    if (*fat != NULL) {
        freeFat(fat);
    }
    
    #ifdef DEBUGGING
        writeHelper("Fat name is ");
        writeHelper(fileName);
        writeHelper("\n");
    #endif

    *fat = loadFat(fileName);

    if (*fat == NULL) {
        printf("ERROR: Fail to load FAT.\n");
        return -1;
    }

    return 0;
}

int pennfatUnmount(pennfat **fat) {
    saveFat(*fat);
    freeFat(fat);
    return 0;
}

int pennfatTouch(char **files, pennfat *fat) {
    int idx = 1;
    char *fileName = files[idx];

    while (fileName != NULL) {
        if (writeFile(fileName, NULL, 0, 0, REGULAR_FILETYPE, READWRITE_PERMS, fat, true, false, false) == -1) {
            printf("ERROR: Fail to write %s.\n", fileName);
            return -1;
        }
        fileName = files[++idx];
    }

    saveFat(fat);
    return 0;
}

int pennfatMove(char *oldFileName, char *newFileName, pennfat *fat) {
    if (renameFile(oldFileName, newFileName, fat) == -1)
        printf("ERROR: Fail to rename %s as %s.\n", oldFileName, newFileName);
    return -1;

    saveFat(fat);
    return 0;
}

int pennfatRemove(char **files, pennfat *fat) {
    int idx = 1;
    char *fileName = files[idx];

    while (fileName != NULL) {
        if (deleteFile(fileName, fat, false) == -1) {
            printf("ERROR: Fail to delete %s.\n", fileName);
            return -1;
        }
        fileName = files[++idx];
    }

    saveFat(fat);
    return 0;
}

int pennfatCat(char **commands, int count, pennfat *fat) {
    // Get flags
    bool w_flag = strcmp(commands[count - 2], "-w") == 0;
    bool a_flag = strcmp(commands[count - 2], "-a") == 0;

    // Read user input to overwiret [cat -w OUTPUT_FILE] or append [cat -a OUTPUT_FILE] the file
    if (count == 3 && (w_flag || a_flag)) {
        // Get user input
        char *line = NULL;
        size_t len = 0;
        ssize_t n;

        #ifdef DEBUGGING
            printf("Reading user input...\n");
        #endif
        n = getline(&line, &len, stdin);

        // User input reading error handling
        if (n == -1) {
            if (line != NULL) {
                free(line);
            }

            if (errno == EINVAL || errno == ENOMEM) {
                perror("ERROR: Fail to execute getline().");
                return -1;
            } else {
                ssize_t numBytes = write(STDERR_FILENO, "\n", 1);
                if (numBytes == -1) {
                    perror("ERROR: Fail to write.");
                    return -1;
                }
                // TODO: add a writeHelper which check the error when writing
                return -1;
            }
        }

        #ifdef DEBUGGING
                if (w_flag) {
                    printf("Writing to the output file...\n");
                } else if (a_flag) {
                    printf("Appending to the output file...\n");
                }
        #endif
        if (writeFile(commands[2], (uint8_t *)line, 0, n, REGULAR_FILETYPE, READWRITE_PERMS, fat, a_flag, false, false) == -1) {
            free(line);
            return -1;
        }
        free(line);
    } else {
        // Concatenates the files and prints them to stdout [cat FILE ...], or overwrites [cat FILE ... -w OUTPUT_FILE], or append [cat FILE ... -a OUTPUT_FILE]
        int lastInputFile;

        if (w_flag || a_flag) {
            lastInputFile = count - 3;
        } else {
            lastInputFile = count - 1;
        }

        file *files[lastInputFile];
        for (int i = 0; i < lastInputFile; i++) {
            files[i] = readFile(commands[i + 1], fat);
            if (files[i] == NULL) {
                for (int j = 0; j < i; j++)
                    freeFile(files[j]);
                return -1;
            }
        }

        #ifdef DEBUGGING
            if (w_flag) {
                printf("Writing to the output file...\n");
            } else if (a_flag) {
                printf("Appending to the output file...\n");
            } else {
                printf("Printing to stdout...\n");
            }
        #endif

        for (int i = 0; i < lastInputFile; i++) {
            if (!w_flag && !a_flag) {
                printf("%s", (char *)files[i]->contents);
            } else if (i == 0 && w_flag) {
                if (writeFile(commands[count - 1], files[i]->contents, 0, files[i]->len, REGULAR_FILETYPE, READWRITE_PERMS, fat, false, false, false) == -1)
                    return -1;
            // } else {
            //     if (writeFileToFAT(commands[count - 1], files[i]->contents, 0, files[i]->len, REGULAR_FILETYPE, READWRITE_PERMS, fat, true, false, false) == -1)
            //         return -1;
            }
            freeFile(files[i]);
        }

        // Flush the output if haven't
        if (!w_flag && !a_flag && fflush(stdout) != 0) {
            perror("ERROR: Fail to flush.");
            return -1;
        }
    }

    if (w_flag || a_flag) {
        saveFat(fat);
    }

    #ifdef DEBUGGING
        printf("Finished.\n");
    #endif

    return 0;
}

int pennfatCopy(char **commands, int count, bool copyingFromHost, bool copyingToHost, pennfat *fat) {
    if (copyingFromHost) {
        #ifdef DEBUGGING
            writeHelper("Copying from host...\n");
        #endif
        int f;
        if ((f = open(commands[2], O_RDONLY, 0644)) == -1) {
            perror("ERROR: fail to open the file.");
            return -1;
        }

        // get length of the file
        int size = lseek(f, 0, SEEK_END);
        if (size == -1) {
            perror("ERROR: fail to get the length of the file.");
            return -1;
        }

        if (lseek(f, 0, SEEK_SET) == -1) {
            perror("ERROR: fail to lseek the file.");
            return -1;
        }

        // Create an empty file
        if (size == 0) {
            if (writeFile(commands[3], NULL, 0, size, REGULAR_FILETYPE, READWRITE_PERMS, fat, false, false, false) == -1) {
                printf("ERROR: Failed to copy host file %s to %s\n", commands[2], commands[3]);
                return -1;
            }
        }

        // Read the file
        #ifdef DEBUGGING
            writeHelper("Malloc the buffer in pennfatCopy\n");
        #endif
        uint8_t *buffer = malloc(sizeof(uint8_t) * size);
        if (buffer == NULL) {
            perror("ERROR: Fail to malloc the file.");
            return -1;
        }

        ssize_t bytesRead;
        int bufIdx = 0;

        #ifdef DEBUGGING
            writeHelper("Reading all bytes in pennfatCopy\n");
        #endif
        // read all bytes to buffer
        while ((bytesRead = read(f, &buffer[bufIdx], fminl(size, SSIZE_MAX))) != 0) {
            if (bytesRead == -1) {
                perror("ERROR: fail to read the file.");
                free(buffer);
                return -1;
            }
            bufIdx += bytesRead;
            if (bufIdx == size)
                break;
        }

        #ifdef DEBUGGING
            writeHelper("Finished reading all bytes in pennfatCopy\n");
        #endif

        if (close(f) == -1) {
            perror("ERROR: fail to close the file.");
            free(buffer);
            return -1;
        }

        #ifdef DEBUGGING
            writeHelper("Writing to the file in pennfatCopy\n");
        #endif
        // Write to the file
        if (writeFile(commands[3], buffer, 0, size, REGULAR_FILETYPE, READWRITE_PERMS, fat, false, false, false) == -1) {
            printf("Failed to copy host file %s to %s\n", commands[2], commands[3]);
            free(buffer);
            return -1;
        }

        free(buffer);
        saveFat(fat);
    } else if (copyingToHost) {
        #ifdef DEBUGGING
            writeHelper("Copying to host...\n");
        #endif
        file *file = readFile(commands[1], fat);
        if (file == NULL) {
            printf("ERROR: Fail to read the host files.\n");
            return -1;
        }

        int f;
        if ((f = open(commands[3], O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1) {
            perror("ERROR: fail to open the file.");
            return -1;
        }

        // Write on the host
        for (int i = 0; i < file->len; i++) {
            if (write(f, &file->contents[i], sizeof(uint8_t)) == -1) {
                perror("ERROR: fail to write the file.");
                return -1;
            }
        }

        if (close(f) == -1) {
            perror("ERROR: fail to close the file.");
            return -1;
        }

        freeFile(file);
    } else {
        file *file = readFile(commands[1], fat);
        if (file == NULL) {
            printf("ERROR: Fail to get the host files.\n");
            return -1;
        }

        if (writeFile(commands[2], file->contents, 0, file->len, REGULAR_FILETYPE, READWRITE_PERMS, fat, false, false, false) == -1) {
            printf("Failed to copy file %s to %s\n", commands[2], commands[3]);
            return -1;
        }

        freeFile(file);
        saveFat(fat);
    }

    return 0;
}

int pennfatLs(pennfat *fat) {
    #ifdef DEBUGGING
        printf("listing the fat...numFile in Fat-%s is %d\n", fat->fileName, fat->numFile);
    #endif
    dirEntryNode *entryNode = fat->head;

    while (entryNode != NULL) {
        dirEntry *entry = entryNode->entry;

        /* Get perm:
            - NONE_PERMS 0
            - WRITE_PERMS 2
            - READ_PERMS 4
            - READEXE_PERMS 5
            - READWRITE_PERMS 6
            - READWRITEEXE_PERMS 7
        */
        char *perms;
        switch (entry->perm) {
        case (NONE_PERMS):
            perms = "---";
            break;
        case (READ_PERMS):
            perms = "-r-";
            break;
        case (READEXE_PERMS):
            perms = "xr-";
            break;
        case (WRITE_PERMS):
            perms = "--w";
            break;
        case (READWRITE_PERMS):
            perms = "-rw";
            break;
        case (READWRITEEXE_PERMS):
            perms = "xrw";
            break;
        }

        // Get mtime
        struct tm *localTime = localtime(&entry->mtime);

        // Get month, day, and time
        char month[4];
        char day[3];
        char time[6];
        strftime(month, 4, "%b", localTime);
        strftime(day, 3, "%d", localTime);
        strftime(time, 6, "%H:%M", localTime);

        // Print
        printf("%3s %6d %4s %3s %6s %s\n", perms, entry->size, month, day, time, entry->name);

        entryNode = entryNode->next;
    }

    return 0;
}

int pennfatChmod(char **commands, int perm, pennfat *fat) {
    if (chmodFile(fat, commands[1], perm) == -1)
        return -1;

    saveFat(fat);
    return 0;
}

int pennfatShow(pennfat *fat) {
    printf("*****************************\n");
    printf("fat->fileName =  %s\n",     fat->fileName);
    printf("fat->totalBlocks =  %d\n",  fat->totalBlocks);
    printf("fat->freeBlocks =  %d\n",   fat->freeBlocks);
    printf("fat->blockSize =  %d\n",    fat->blockSize);
    printf("fat->numEntries =  %d\n",   fat->numEntries);
    printf("fat->numFile =  %d\n",      fat->numFile);
    printf("*****************************\n");
    return 0;
}
