#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "../pennfat/pennfat_handler.h"

/* ------------------------------------------------------------------------
------------------------- Shell Behaviour Functions -----------------------
------------------------------------------------------------------------*/

/**
 * Function: runShell
 * --------------------
 * 
 * The controller file of the shell
 */
void runShell();

/**
 * Function: shellInit
 * --------------------
 * 
 * initialize the shell, including signal registering and job queue creating
 */
void shellInit();

/**
 * Function: setSignalRegister
 * --------------------
 * 
 * Ser signal registers for handling functions
 */
 void setSignalRegister();

 /**
 * Function: jobQueueInit
 * --------------------
 * 
 * Initialize the job queue
 */
//  void jobQueueInit(); (Existed)




/* ------------------------------------------------------------------------
------------------------- Built-ins Functions -----------------------
------------------------------------------------------------------------*/

/**
 * Function: 
 * --------------------
 * 
 * 
 */
void cmd_cat(char **argv);

void cmd_sleep(char **argv);

void cmd_busy(char **argv);

void cmd_echo(char **argv);

void cmd_ls(char **argv);

void cmd_touch(char **argv);

void cmd_mv(char **argv);

void cmd_cp(char **argv);

void cmd_rm(char **argv);

void cmd_chmod(char **argv);

void cmd_ps(char **argv);

void cmd_kill(char *argv[]);

void zombie_child();

void zombify();

void orphan_child();

void orphanify();

void cmd_man();

void cmd_cd(char *args[]);

void cmd_mkdir(char *args[]);


int getCommandCount();

/* ------------------------------------------------------------------------
------------------------- Helper Functions -----------------------
------------------------------------------------------------------------*/
void dispatch_command(char *command, char *args[]);