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

#include <readline/readline.h>
#include <readline/history.h>
#include <libusb-1.0/libusb.h>

#define VERSION			"2.0.2"
#define LIBUSB_VERSION	"1.0"
#define LIBUSB_DEBUG		0

#define VENDOR_ID       (int)0x05AC
#define NORM_MODE       (int)0x1290
#define RECV_MODE       (int)0x1281
#define WTF_MODE        (int)0x1227
#define DFU_MODE        (int)0x1222
#define BUF_SIZE        (int)0x10000

static struct libusb_device_handle *device = NULL;

void device_connect() {

	if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, RECV_MODE)) == NULL) {
		if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, WTF_MODE)) == NULL) {
			if ((device = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, DFU_MODE)) == NULL) {

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

int device_upload(char* filename) {

	FILE* file = fopen(filename, "rb");

	if(file == NULL) {

		printf("[Program] Unable to find file. (%s)\n",filename);
		return 1;

	}

	fseek(file, 0, SEEK_END);
	unsigned int len = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* buffer = malloc(len);

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
	char response[6];

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

	char response[6];

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

int device_sendrawusb0xA1(char *command) {

	printf("[Device] Sending raw command to 0xA1, x, 0, 0, 0, 0, 1000.\n", libusb_control_transfer(device, 0xA1, atoi(command), 0, 0, 0, 0, 1000));

}

int device_sendrawusb0x40(char *command) {

	printf("[Device] Sending raw command to 0x40, x, 0, 0, 0, 0, 1000.\n", libusb_control_transfer(device, 0x40, atoi(command), 0, 0, 0, 0, 1000));

}

int device_sendrawusb0x21(char *command) {

	printf("[Device] Sending raw command to 0x21, x, 0, 0, 0, 0, 1000.\n", libusb_control_transfer(device, 0x21, atoi(command), 0, 0, 0, 0, 1000));

}

void prog_usage() {

	printf("./irecovery [args]\n");
	printf("\t-a\t\tenables auto-boot and reboots the device (exit recovery loop).\n");
	printf("\t-s\t\tstarts a shell.\n");
	printf("\t-r\t\tusb reset.\n");
	printf("\t-u <file>\tuploads a file.\n");
	printf("\t-c \"command\"\tsend a single command.\n");
	printf("\t-b <file>\truns batch commands from a file(one per line).\n");
	printf("\t-x <file>\tuploads a file then resets the usb connection.\n");
	printf("\t-e <file>\tupload a file then run usb exploit.\n");
	printf("\t-x21 <command>\tsend a raw command to 0x21.\n");
	printf("\t-x40 <command>\tsend a raw command to 0x41.\n");
	printf("\t-xA1 <command>\tsend a raw command to 0xA1.\n");
	printf("\n");
	printf("== Console / Batch Commands ==\n");
	printf("\n");
	printf("\t/auto-boot\tenables auto-boot and reboots the device (exit recovery loop).\n");
	printf("\t/exit\t\texit the recovery console.\n");
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

int prog_parse(char *command) {

	char* action = strtok(strdup(command), " ");

	if(! strcmp(action, "help")) {

		printf("Commands:\n");
		printf("\t/exit\t\t\texit from recovery console.\n");
		printf("\t/upload <file>\t\tupload file to device.\n");
		printf("\t/exploit [payload]\tsend usb exploit packet.\n");
		printf("\t/batch <file>\t\texecute commands from a batch file.\n");
		printf("\t/auto-boot\t\t\tenable auto-boot (exit recovery loop).\n");

	} else if(! strcmp(action, "exit")) {

		free(action);
		return -1;

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

				char byte;
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

		int bytes = 0;

		memset(buffer, 0, BUF_SIZE);
		libusb_bulk_transfer(device, 0x81, buffer, BUF_SIZE, &bytes, 500);

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
}

void prog_handle(int argc, char *argv[]) {

	if (! strcmp(argv[1], "-a")) {

		device_autoboot();

	} else if (! strcmp(argv[1], "-c")) {

		if (argc >= 3) {

			if (argc > 3) {

				char command[0x200];
				int i = 2;

				for (i; i < argc; i++) {

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

	} else if (! strcmp(argv[1], "-s")) {

		if (argc >= 3) //Logfile specified
			prog_console(argv[2]);
		else
			prog_console(NULL);

	} else {

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
