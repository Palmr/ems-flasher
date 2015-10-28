#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <libusb-1.0\libusb.h>

#include "defines.h"
#include "ems.h"

/* EMS Cart vendor/product ID magic numbers */
#define EMS_VID 0x4670
#define EMS_PID 0x9394

#define EMS_EP_SEND (2 | LIBUSB_ENDPOINT_OUT)
#define EMS_EP_RECV (1 | LIBUSB_ENDPOINT_IN)

enum {
  CMD_READ    		= 0xff,
  CMD_WRITE				= 0x57,
  CMD_READ_SRAM		= 0x6d,
  CMD_WRITE_SRAM	= 0x4d,
};

static const int _test = 0x1234;
static const char * _ptest = (char*)&_test;
uint32_t host_to_network_long(uint32_t host32) {
	if(*_ptest == 0x12) {
		return host32;
	}
	else {
		return ((host32 & 0xff) << 24) | ((host32 & 0xff00) << 8) | ((host32 & 0xff0000) >> 8) | ((host32 & 0xff000000) >> 24);
	}
}

static struct libusb_device_handle *device_handle = NULL;
static int claimed = 0;

int verbose_level = 0;

/**
 * Attempt to find the EMS cart by vid/pid.
 *
 * Returns:
 *  0       success
 *  < 0     failure
 */
static int find_ems_device(void) {
	ssize_t num_devices = 0;
	libusb_device **device_list = NULL;
	struct libusb_device_descriptor device_descriptor;
	int i = 0;
	int retval = 0;

	num_devices = libusb_get_device_list(NULL, &device_list);
	if (verbose_level > 0) {
		printf("Searching for EMS cart USB device:\n");
	}

	if (num_devices >= 0) {
		for (; i < num_devices; ++i) {
			(void) memset(&device_descriptor, 0, sizeof(device_descriptor));
			retval = libusb_get_device_descriptor(device_list[i], &device_descriptor);
			if (retval == 0) {
				if (verbose_level > 0) {
					printf("  [%d/%d] %04x:%04x (bus %d, device %d)\n", (i+1), num_devices,
																															device_descriptor.idVendor, device_descriptor.idProduct,
																															libusb_get_bus_number(device_list[i]), libusb_get_device_address(device_list[i]));
				}

				if (device_descriptor.idVendor == EMS_VID && device_descriptor.idProduct == EMS_PID) {
					retval = libusb_open(device_list[i], &device_handle);
					if (retval != 0) {
							/*
							 * According to the documentation, device_handle will not
							 * be populated on error, so it should remain
							 * NULL.
							 */
							fprintf(stderr, "Failed to open device (libusb error: %s).\n", libusb_error_name(retval));
#ifdef __linux__
							if (retval == LIBUSB_ERROR_ACCESS) {
								fprintf(stderr, "Try running as root/sudo or update udev rules (check the FAQ for more info).\n");
							}
#endif
					}
					else {
						if (verbose_level > 0) {
							printf("  EMS cart found!\n");
						}
					}
					break;
				}
			}
			else {
				fprintf(stderr, "Failed to get device description (libusb error: %s).\n", libusb_error_name(retval));
			}
		}
		if (i == num_devices) {
			fprintf(stderr, "Could not find device, is it plugged in?\n");
		}
		libusb_free_device_list(device_list, 1);
		device_list = NULL;
	}
	else {
		fprintf(stderr, "Failed to get device list: %s\n", libusb_error_name((int)num_devices));
	}

	return device_handle != NULL ? 0 : -ENODEV;
}

/**
 * Init the flasher. Inits libusb and claims the device. Aborts if libusb
 * can't be initialized.
 *
 * TODO replace printed error with return code
 *
 * Returns:
 *  0       Success
 *  < 0     Failure
 */
int ems_init(int verbosity) {
	int retval;

	verbose_level = verbosity;

	// Make sure we let the device go when we're done
	void ems_deinit(void);
	atexit(ems_deinit);

	retval = libusb_init(NULL);
	if (retval != 0) {
		fprintf(stderr, "Failed to initialize libusb: %d\n", retval);
		exit(retval);
	}

	retval = find_ems_device();
	if (retval != 0) {
		fprintf(stderr, "Failed to find device: %d\n", retval);
		return retval;
	}

	retval = libusb_claim_interface(device_handle, 0);
	if (retval != 0) {
		fprintf(stderr, "Failed to claim device: %d\n", retval);
		return retval;
	}
	else {
		claimed = 1;
	}
	
	if (verbose_level > 0) {
		printf("EMS cart found and claimed\n");
	}

	return 0;
}

/**
 * Cleanup / release the device. Registered with atexit.
 */
void ems_deinit(void) {
	if (claimed) {
		int retval = libusb_release_interface(device_handle, 0);
		if (retval != 0) {
			fprintf(stderr, "Failed to release device: %d\n", retval);
		}
	}

	libusb_close(device_handle);
	libusb_exit(NULL);
	
	if (verbose_level > 0) {
		printf("Deinitialising EMS cart\n");
	}
}

/**
 * Initialize a command buffer. Commands are a 1 byte command code followed by
 * a 4 byte address and a 4 byte value.
 *
 * buf must point to a memory chunk of size >= 9 bytes
 */
static void ems_command_init(
        unsigned char *buf, // buffer to init
        unsigned char cmd,  // command to run
        uint32_t addr,      // address
        uint32_t val        // value
) {
    buf[0] = cmd;
    *(uint32_t *)(buf + 1) = host_to_network_long(addr);
    *(uint32_t *)(buf + 5) = host_to_network_long(val);
}

/**
 * Read some bytes from the cart.
 *
 * Params:
 *  from    FROM_ROM or FROM_SRAM
 *  offset  absolute read address from the cart
 *  buf     buffer to read into (buffer must be at least count bytes)
 *  count   number of bytes to read
 *
 * Returns:
 *  >= 0    number of bytes read (will always == count)
 *  < 0     error sending command or reading data
 */
int ems_read(int from, uint32_t offset, unsigned char *buf, size_t count) {
    int r, transferred;
    unsigned char cmd;
    unsigned char cmd_buf[9];

    assert(from == FROM_ROM || from == FROM_SRAM);

    cmd = from == FROM_ROM ? CMD_READ : CMD_READ_SRAM;
    ems_command_init(cmd_buf, cmd, offset, count);

    // send the read command
    r = libusb_bulk_transfer(device_handle, EMS_EP_SEND, cmd_buf, sizeof(cmd_buf), &transferred, 0);
    if (r < 0)
        return r;

    // read the data
    r = libusb_bulk_transfer(device_handle, EMS_EP_RECV, buf, count, &transferred, 0);
    if (r < 0)
        return r;

    return transferred;
}

/**
 * Write to the cartridge.
 *
 * Params:
 *  to      TO_ROM or TO_SRAM
 *  offset  address to write to
 *  buf     data to write
 *  count   number of bytes out of buf to write
 *
 * Returns:
 *  >= 0    number of bytes written (will always == count)
 *  < 0     error writing data
 */
int ems_write(int to, uint32_t offset, unsigned char *buf, size_t count) {
    int r, transferred;
    unsigned char cmd;
    unsigned char *write_buf;

    assert(to == TO_ROM || to == TO_SRAM);
    cmd = to == TO_ROM ? CMD_WRITE : CMD_WRITE_SRAM;

		
    // thx libusb for having no scatter/gather io
    write_buf = malloc(count + 9);
    if (write_buf == NULL) {
      printf("malloc\n");
      return 1;
    }

    // set up the command buffer
    ems_command_init(write_buf, cmd, offset, count);
    memcpy(write_buf + 9, buf, count);

    r = libusb_bulk_transfer(device_handle, EMS_EP_SEND, write_buf, count + 9, &transferred, 0);
    if (r == 0)
        r = transferred; // return number of bytes sent on success

    free(write_buf);

    return r;
}
