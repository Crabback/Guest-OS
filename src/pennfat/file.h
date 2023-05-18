#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fat.h"

#define DEFAULT_FILENAME "/"

#define UNKNOWN_FILETYPE 0
#define REGULAR_FILETYPE 1
#define DIRECTORY_FILETYPE 2
#define SYMLINK_FILETYPE 4

#define NONE_PERMS 0
#define WRITE_PERMS 2
#define READ_PERMS 4
#define READEXE_PERMS 5
#define READWRITE_PERMS 6
#define READWRITEEXE_PERMS 7

#define F_SEEK_SET
#define F_SEEK_CUR
#define F_SEEK_END

typedef struct file {
    uint8_t *contents;
    unsigned int len;
    uint8_t type;
    uint8_t perm;
} file;

void freeFile(file *file);
void getDirEntryNode(dirEntryNode **prev, dirEntryNode **target, char *fileName, pennfat *fat);
file *getAllFile(pennfat *fat);
uint8_t *getContents(uint16_t startIndex, uint32_t len, pennfat *fat);

file *readFile(char *fileName, pennfat *fat);
void deleteFileHelper(dirEntryNode *prev, dirEntryNode *entryNode, pennfat *fat, bool dirFile);
int deleteFile(char *fileName, pennfat *fat, bool flag);
int renameFile(char *oldFileName, char *newFileName, pennfat *fat);
int writeFile(char *fileName, uint8_t *bytes, uint32_t offset, uint32_t len, uint8_t type, uint8_t perm, pennfat *fat, bool flag, bool syscall, bool writeDir);
int appendFile(char *fileName, uint8_t *bytes, uint32_t len, pennfat *fat, bool flag);
int writeDirEntries(pennfat *fat);
int chmodFile(pennfat *fat, char *fileName, int newPerms);

int f_open(const char *filename, int mode);     // returns a file descriptor on success and a negative value on error
int f_close(int fd);                            // return 0 on success, or a negative value on failure.
int f_read(int fd, int n, char *buf);           // returns the number of bytes read, 0 if EOF is reached, or a negative number on error.
int f_write(int fd, char *str, int n);    // the number of bytes written, or a negative value on error.
int f_unlink(char *filename);             // return 0 on success, or a negative value on failure.
int f_lseek(int fd, int offset, int whence);    // return 0 on success, or a negative value on failure.