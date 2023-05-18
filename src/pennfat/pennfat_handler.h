#pragma once

#include "file.h"

// Standalone handler
int pennfatMkfs(char *fileName, uint8_t numBlocks, uint8_t blockSizeIndex, pennfat **fat);
int pennfatMount(char *fileName, pennfat **fat);
int pennfatUnmount(pennfat **fat);
int pennfatTouch(char **files, pennfat *fat);
int pennfatMove(char *oldFileName, char *newFileName, pennfat *fat);
int pennfatRemove(char **files, pennfat *fat);
int pennfatCat(char **commands, int count, pennfat *fat);
int pennfatCopy(char **commands, int count, bool copyingFromHost, bool copyingToHost, pennfat *fat);
int pennfatLs(pennfat *fat);
int pennfatChmod(char **commands, int perm, pennfat *fat);
int pennfatShow(pennfat *fat);

/* PROGRESS NOTES:
COMMAND     FUNCTION_NAME           IMPLEMENTATION      TESTING
mkfs        pennfatMkfs             Done                
mount       pennfatMount            Done
unmount     pennfatUnmount          Done
touch       pennfatTouch            Done
move        pennfatMove             Done
remove      pennfatRemove           Done
cat         pennfatCat              Done
copy        pennfatCopy             Done
ls          pennfatLs               Done
chmod       pennfatChmod            Done
*/