/**
 * iRecovery - Utility for DFU 2.0, WTF and Recovery Mode
 * Copyright (C) 2008 - 2009 westbaer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <usb.h>

#define VENDOR_ID    0x05AC
#define NORM_MODE    0x1290
#define RECV_MODE    0x1281
#define WTF_MODE     0x1227
#define DFU_MODE     0x1222
#define BUF_SIZE     0x10000

void irecv_hexdump(char *buf, int len) {
	int i = 0;
	for (i = 0; i < len; i++) {
		if (i % 16 == 0 && i != 0)
			printf("\n");
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

struct usb_dev_handle *irecv_init(int devid) {
	struct usb_dev_handle *handle = NULL;
	struct usb_device *dev = NULL;
	struct usb_bus *bus = NULL;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == VENDOR_ID
					&& dev->descriptor.idProduct == devid) {
				handle = usb_open(dev);
				return handle;
			}
		}
	}

	return NULL;
}

void irecv_close(struct usb_dev_handle *handle) {
	printf("Closing USB connection...\n");
	if (handle != NULL) {
		usb_close(handle);
	}
}

int irecv_sendfile(struct usb_dev_handle *handle, char *filename) {
	FILE* file = fopen(filename, "rb");
	if (file == NULL) {
		printf("File %s not found.\n", filename);
		return 1;
	}

	fseek(file, 0, SEEK_END);
	int len = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* buffer = malloc(len);
	if (buffer == NULL) {
		printf("Error allocating memory!\n");
		fclose(file);
		return 1;
	}

	fread(buffer, 1, len, file);
	fclose(file);

	int packets = len / 0x800;
	if (len % 0x800) {
		packets++;
	}

	int last = len % 0x800;
	if (!last) {
		last = 0x800;
	}

	int i = 0;
	char response[6];
	for (i = 0; i < packets; i++) {
		int size = i + 1 < packets ? 0x800 : last;

		if (!usb_control_msg(handle, 0x21, 1, i, 0, &buffer[i * 0x800], size,
				1000)) {
			printf("Error!\n");
		}

		if (usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("Error receiving status!\n");

		} else {
			printf(".");
			if (response[4] != 5) {
				printf("Status error!\n");
			}
		}
	}

	printf("\n");
	usb_control_msg(handle, 0x21, 1, i, 0, buffer, 0, 1000);
	for (i = 6; i <= 8; i++) {
		if (usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("Error receiving status!\n");
		} else {
			if (response[4] != i) {
				printf("Status error!\n");
			}
		}
	}

	free(buffer);
	return 0;
}

int irecv_sendbuffer(struct usb_dev_handle* handle, char* data, int len) {
	if (!handle) {
		printf("Device has not been initialized!\n");
		return 1;
	}

	int packets = len / 0x800;
	if (len % 0x800) {
		packets++;
	}

	int last = len % 0x800;
	if (!last) {
		last = 0x800;
	}

	int i = 0;
	char response[6];
	for (i = 0; i < packets; i++) {
		int size = i + 1 < packets ? 0x800 : last;

		if (!usb_control_msg(handle, 0x21, 1, i, 0, &data[i * 0x800], size,
				1000)) {
			printf("error!\n");
		}

		if (usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("error receiving send status!\n");
			return 1;

		} else {
			if (response[4] != 5) {
				printf("send status error!\n");
				return 1;
			}
		}

	}

	usb_control_msg(handle, 0x21, 1, i, 0, data, 0, 1000);
	for (i = 6; i <= 8; i++) {
		if (usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("error receiving execution status!\n");
			return 1;

		} else {
			if (response[4] != i) {
				printf("execution status error!\n");
				return 1;
			}
		}
	}

	return 0;
}

int irecv_sendcmd(struct usb_dev_handle *handle, char *command) {
	size_t length = strlen(command);
	if (length >= 0x200) {
		printf("Error command is too long!\n");
		return 1;
	}

	if (!usb_control_msg(handle, 0x40, 0, 0, 0, command, length + 1, 10)) {
		printf("[!] %s", usb_strerror());
	}

	return 0;
}

int irecv_parsecmd(struct usb_dev_handle *handle, char *command) {
	char* action = strtok(strdup(command), " ");
	if (strcmp("/sendfile", action) == 0) {
		char* filename = strtok(NULL, " ");
		if (filename != NULL) {
			irecv_sendfile(handle, filename);
		}
	}

	if (strcmp("/run", action) == 0) {
		char* filename = strtok(NULL, " ");
		if (filename != NULL) {
			irecv_sendfile(handle, filename);
			irecv_sendcmd(handle, &command[strlen(action) + 1]);
		}
	}

	free(action);
	return 0;
}

void irecv_console(struct usb_dev_handle *handle) {
	if (usb_claim_interface(handle, 1) < 0) {
		printf("%s\n", usb_strerror());
		return;
	}

	if (usb_set_altinterface(handle, 1) < 0) {
		printf("%s\n", usb_strerror());
		return;
	}

	char* buffer = malloc(BUF_SIZE);
	if (buffer == NULL) {
		printf("Error allocating memory!\n");
		return;
	}
	
	FILE* fd = fopen("irecovery.log", "w");
	if(fd == NULL) {
	    printf("Unable to open log file!\n");
	    return;
	}

	while (1) {
		int bytes = 0;
		while (bytes >= 0) {
			memset(buffer, 0, BUF_SIZE);
			bytes = usb_bulk_read(handle, 0x81, buffer, BUF_SIZE, 250);
			if (bytes > 0) {
				int i = 0;
				int next = 0;
				for (i = 0; i < bytes; i += next) {
					next = strlen(&buffer[i]) + 1;
					printf("%s", &buffer[i]);
					fprintf(fd, "%s", &buffer[i]);
				}
			}
		}

		char* command = readline("] ");
		if (command && *command) {
			add_history(command);
		}

		if (strcmp(command, "/exit")) {
			if (command[0] != '/') {
				irecv_sendcmd(handle, command);

			} else {
				irecv_parsecmd(handle, command);
			}
		} else {
			free(command);
			break;
		}

		free(command);
	}
	
    fclose(fd);
	usb_release_interface(handle, 0);
	free(buffer);
}

void irecv_reset(struct usb_dev_handle *handle) {
	if (handle != NULL) {
		usb_reset(handle);
	}
}

void irecv_usage(void) {
	printf("./irecovery [args]\n");
	printf("\t-f <file>\t\tupload file.\n");
	printf("\t-r\t\t\treset usb.\n");
	printf("\t-c \"command\"\t\tsends a single command.\n");
	printf("\t-s\t\t\tstarts a shell.\n");
	printf("\t-k\t\t\tsend usb exploit.\n\n");
}

int main(int argc, char *argv[]) {
	struct usb_dev_handle *handle = NULL;
	printf("iRecovery - Recovery Utility\n");
	printf("by westbaer\nThanks to pod2g, tom3q, planetbeing, geohot and posixninja.\n\n");
	if (argc < 2) {
		irecv_usage();
		return 1;
	}

	handle = irecv_init(WTF_MODE);
	if (handle == NULL) {
		handle = irecv_init(RECV_MODE);
		if (handle != NULL) {
			printf("Found iPhone/iPod in Recovery mode\n");
		}
	} else {
		printf("Found iPhone/iPod in DFU/WTF mode\n");
	}

	if (handle == NULL) {
		printf("No iPhone/iPod found.\n");
		return 1;
	}

	if (argv[1][0] != '-') {
		irecv_usage();
		return 1;
	}

	if (strcmp(argv[1], "-f") == 0) {
		if (argc >= 3) {
			irecv_sendfile(handle, argv[2]);

		} else {
			printf("No valid file set.\n");
		}

	} else if (strcmp(argv[1], "-c") == 0) {
		if (argc >= 3) {
			irecv_sendcmd(handle, argv[2]);

		} else {
			printf("No valid command set.\n");
		}

	} else if (strcmp(argv[1], "-s") == 0) {
		irecv_console(handle);

	} else if (strcmp(argv[1], "-r") == 0) {
		irecv_reset(handle);

	} else if (strcmp(argv[1], "-k") == 0) {
		printf("usbhax 0x21-2-0-0: %04x\n", usb_control_msg(handle, 0x21, 2, 0,
				0, 0, 0, 1000));

	}
	
	irecv_close(handle);
	return 0;
}

