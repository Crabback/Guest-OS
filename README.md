# 23sp-pennOS-group-55

## Group Members:
Qi Xue
Ze Sheng
Zhengjia Mao
Zhiqi Cui

## Project Description
PennOS is a UNIX-like operating system that runs as a guest OS within a single process on a host OS. PennOS models standard UNIX, including kernel features, FAT file system, and a user shell. This file serves as a companion document to PennOS and PennFAT, and provides necessary information to understand the project architecture, as well as the declared data structures, variables, and functions. This document also specifies the purposes of each function and explains the passed-in arguments. More specifically, the project is structured as below.

## How to run the project

# PennFAT
/root: make pennfat
/root/bin: ./pennfat

# PennOS
/root: make pennos
/root/bin: ./pennos

## Tips
# parser
For apple M1 computers, copy and paste the parser.o from /root/src/parsers_backup/parser_mac/parser.o to /root/src/pennos/parser.o.

For windows or older apple computers, copy and paste the parser.o from /root/src/parsers_backup/parser_windows/parser.o to /root/src/pennos/parser.o.

# log
Logs can be found at /root/log/
