#ifndef __DEFINES_H__
#define __DEFINES_H__

// Where reads/writes can target
#define FROM_ROM		1
#define FROM_SRAM		2
#define TO_ROM			FROM_ROM
#define TO_SRAM			FROM_SRAM

// One bank is 32 megabits
#define BANK_SIZE 0x400000
#define SRAM_SIZE 0x020000

// Operation modes
#define MODE_READ		1
#define MODE_WRITE	2
#define MODE_TITLE	3

// Default read/write buffer sizes
#define BUFFERSIZE_READ  4096
#define BUFFERSIZE_WRITE 32

#endif /* __DEFINES_H__ */
