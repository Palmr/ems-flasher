#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "ems.h"

// Logo bytes to use for header finding and validating
const unsigned char ninty_logo[0x30] = {
	0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
	0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
	0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
	0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
	0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
	0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E};

// Offsets to parts of the rom header
enum header_offsets {
	HEADER_LOGO					= 0x104,
	HEADER_TITLE				= 0x134,
	HEADER_CGBFLAG			= 0x143,
	HEADER_SGBFLAG			= 0x146,
	HEADER_ROMSIZE			= 0x148,
	HEADER_RAMSIZE			= 0x149,
	HEADER_REGION				= 0x14A,
	HEADER_OLDLICENSEE	= 0x14B,
	HEADER_ROMVER				= 0x14C,
	HEADER_CHKSUM				= 0x14D,
};

const int limits[3] = {0, BANK_SIZE, SRAM_SIZE};

/* options */
typedef struct _options_t {
    int verbose;
    int blocksize;
    int mode;
    char *file;
    int bank;
    int space;
} options_t;

// defaults
options_t opts = {
    .verbose            = 1,
    .blocksize          = 0,
    .mode               = 0,
    .file               = NULL,
    .bank               = 0,
    .space              = 0,
};

/**
 * Main
 */
int main(int argc, char **argv) {
    int r;
		
		int mode = MODE_WRITE;
		char *filename = "legit.sav";

		int bank = 0;

    r = ems_init(opts.verbose);
    if (r < 0)
        return 1;

    // we'll need a buffer one way or another
    int blocksize = BUFFERSIZE_WRITE;
    uint32_t offset = 0;
    uint32_t base = bank * BANK_SIZE;

    printf("base address is 0x%X\n", base);
    
    unsigned char *buf = malloc(blocksize);
    if (buf == NULL) {
			printf("malloc\n");
      return 1;
		}

    // determine what we're reading/writing from/to
    int space = TO_SRAM;

		
		if (mode == MODE_READ) {
        FILE *save_file = fopen(filename, "wb");
        if (save_file == NULL){
					printf("Can't open %s for writing\n");
					return 1;
				}

        if (opts.verbose > 0 && space == FROM_ROM)
            printf("Saving ROM into %s\n", filename);
        else if (opts.verbose > 0)
            printf("Saving SAVE into %s\n", filename);

        while ((offset + blocksize) <= limits[space]) {
            r = ems_read(space, offset + base, buf, blocksize);
            if (r < 0) {
                printf("can't read %d bytes at offset %u\n", blocksize, offset);
                return 1;
            }

            r = fwrite(buf, blocksize, 1, save_file);
            if (r != 1){
							printf("can't write %d bytes into file at offset %u\n", blocksize, offset);
							return 1;
						}

            offset += blocksize;
        }

        fclose(save_file);

        if (opts.verbose > 0)
            printf("Successfully wrote %u bytes into %s\n", offset, filename);
    }
		else if (mode == MODE_WRITE) {
			FILE *write_file = fopen(filename, "rb");
			if (write_file == NULL) {
				if (space == TO_ROM) {
					printf("Can't open ROM file %s", filename);
					return 1;
				}
				else {
					printf("Can't open SAVE file %s", filename);
					return 1;
				}
			}

			if (opts.verbose && space == TO_ROM)
				printf("Writing ROM file %s\n", filename);
			else if (opts.verbose)
				printf("Writing SAVE file %s\n", filename);
				
			fseek(write_file, 0, SEEK_END); // seek to end of file
			int size = ftell(write_file); // get current file pointer
			fseek(write_file, 0, SEEK_SET); // seek back to beginning of file
			int perc = 0;
			printf("Size to write: %d (%d writes to do)\n", size, (int)((float)size/(float)blocksize));

			while ((offset + blocksize) <= limits[space] && fread(buf, blocksize, 1, write_file) == 1) {
				r = ems_write(space, offset + base, buf, blocksize);
				if (r < 0) {
					printf("can't write %d bytes at offset %u", blocksize, offset);
					return 1;
				}

				offset += blocksize;
				perc = (int)((100.0/(float)size) * (float)offset);
				printf("progress %3d%%\r", perc);
			}
				printf("\n", perc);

			fclose(write_file);

			if (opts.verbose)
				printf("Successfully wrote %u from %s\n", offset, filename);
		}
		else if (mode == 586) {
			// Calculate checksum
			unsigned char buf[512];
			int i;
			FILE *write_file = fopen(filename, "rb");
			if (write_file == NULL) {
				printf("Can't open ROM file %s", filename);
				return 1;
			}
			
			fread(buf, 512, 1, write_file);
			
			uint8_t calculated_chk = 0;
			for (i = HEADER_TITLE; i < HEADER_CHKSUM; i++) {
				calculated_chk -= buf[i] + 1;
			}
			printf("Checksum: %x\n\n", calculated_chk);
			printf("Checksum: %x\n\n", buf[HEADER_CHKSUM]);
			
			fclose(write_file);
		}
		else if (mode == MODE_TITLE) {

        unsigned char buf[512];
        int i;

        r = ems_read(FROM_ROM, 0, buf, 512);
        if (r < 0){
					printf("Couldn't read ROM header at bank 0, offset 0, len 512\n");
					return 1;
				}
        
        printf("\n\nBank 0: ");
        for (i = HEADER_TITLE; i < (HEADER_TITLE + 16) && buf[i] > 0; i++) {
            putchar(buf[i]);
        }
        printf("\nHardware support: ");

        if ((buf[HEADER_CGBFLAG] & 128) && (buf[HEADER_CGBFLAG] & 64)) {
            printf("CGB\n");
        } else if ((buf[HEADER_CGBFLAG] & 128) && (buf[HEADER_CGBFLAG] & 64) && buf[HEADER_SGBFLAG] == 0x03) {
            printf("CGB <+SGB>, not real option set\n");
        } else if ((buf[HEADER_CGBFLAG] & 128) && buf[HEADER_SGBFLAG] == 0x03) {
            printf("DMG <+CGB, +SGB>\n");
        } else if ((buf[HEADER_CGBFLAG] & 128)) {
            printf("DMG <+CGB>\n");
        } else if (buf[HEADER_SGBFLAG] == 0x03) {
            printf("DMG <+SGB>\n");
        } else {
            printf("DMG\n");
        }

        //Verify cartridge header checksum while we're at it
        uint8_t calculated_chk = 0;
        for (i = HEADER_TITLE; i < HEADER_CHKSUM; i++) {
            calculated_chk -= buf[i] + 1;
        }

        if (calculated_chk != buf[HEADER_CHKSUM]) {
            printf("Cartridge header checksum invalid. This game will NOT boot on real hardware.\n");
        } else {
					if (opts.verbose > 0) 
							printf("Cartridge header checksum OK.\n");
				}

        if (buf[HEADER_SGBFLAG] == 0x03 && buf[HEADER_OLDLICENSEE] != 0x33) {
            printf("SGB functions were enabled, but Old Licensee field is not set to 33h. This game will not be able to use SGB functions on real hardware.\n");
        }

        if (opts.verbose > 0) {
            switch (buf[HEADER_ROMSIZE]) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                    printf("%u KB ROM\n", 32 << buf[HEADER_ROMSIZE]);
                    break;
                case 0x52:
                    printf("1152 KB ROM\n");
                    break;
                case 0x53:
                    printf("1280 KB ROM\n");
                    break;
                case 0x54:
                    printf("1536 KB ROM\n");
                    break;
                default:
                    printf("Unknown ROM size code\n");
                    break;
            }
        }

        r = ems_read(FROM_ROM, BANK_SIZE, (unsigned char *)buf, 512);
        if (r < 0){
					printf("Couldn't read ROM header at bank 1, offset 0, len 512\n");
					return 1;
				}
        
        printf("\n\nBank 1: ");
        for (i = HEADER_TITLE; i < (HEADER_TITLE + 16) && buf[i] > 0; i++) {
            putchar(buf[i]);
        }
        printf("\nHardware support: ");

        if (buf[HEADER_CGBFLAG] == 0x80 && buf[HEADER_SGBFLAG] == 0x03) {
            printf("CGB enhanced, SGB enhanced, DMG compatible\n");
        } else if (buf[HEADER_CGBFLAG] == 0x80) {
            printf("CGB enhanced, DMG compatible\n");
        } else if (buf[HEADER_CGBFLAG] == 0xC0) {
            printf("CGB only\n");
        } else if (buf[HEADER_CGBFLAG] == 0xC0 && buf[HEADER_SGBFLAG] == 0x03) {
            printf("CGB only, SGB enhanced (not a real set of options)\n");
        } else if (buf[HEADER_SGBFLAG] == 0x03) {
            printf("DMG, SGB enhanced\n");
        } else {
            printf("DMG\n");
        }

        //Verify cartridge header checksum while we're at it
        calculated_chk = 0;
        for (i = HEADER_TITLE; i < HEADER_CHKSUM; i++) {
            calculated_chk -= buf[i] + 1;
        }

        if (calculated_chk != buf[HEADER_CHKSUM]) {
            printf("Cartridge header checksum invalid. This game will NOT boot on real hardware.\n");
        } else {
					if (opts.verbose > 0) 
							printf("Cartridge header checksum OK.\n");
				}

        if (buf[HEADER_SGBFLAG] == 0x03 && buf[HEADER_OLDLICENSEE] != 0x33) {
            printf("SGB functions were enabled, but Old Licensee field is not set to 33h. This game will not be able to use SGB functions on real hardware.\n");
        }

        if (opts.verbose > 0) {
            switch (buf[HEADER_ROMSIZE]) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                    printf("%u KB ROM\n", 32 << buf[HEADER_ROMSIZE]);
                    break;
                case 0x52:
                    printf("1152 KB ROM\n");
                    break;
                case 0x53:
                    printf("1280 KB ROM\n");
                    break;
                case 0x54:
                    printf("1536 KB ROM\n");
                    break;
                default:
                    printf("Unknown ROM size code\n");
                    break;
            }
        }
		}


    // belt and suspenders
    free(buf);

    return 0;
}
