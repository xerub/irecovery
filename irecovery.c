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

#define VENDOR_ID       0x05AC
#define NORM_MODE       0x1290
#define RECV_MODE       0x1281
#define WTF_MODE        0x1227
#define DFU_MODE        0x1222
#define BUF_SIZE        0x10000

void irecv_hexdump(unsigned char* buf, unsigned int len) {
	unsigned int i = 0;
	for(i = 0; i < len; i++) {
		if(i % 16 == 0 && i != 0)
			printf("\n");
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

struct usb_dev_handle* irecv_init(unsigned int devid) {
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

void irecv_close(struct usb_dev_handle* handle) {
	printf("Closing USB connection...\n");
	if (handle != NULL) {
		usb_close(handle);
	}
}

void irecv_reset(struct usb_dev_handle* handle) {
	if (handle != NULL) {
		usb_reset(handle);
	}
}

int irecv_upload(struct usb_dev_handle* handle, char* filename) {
	if(handle == NULL) {
		printf("irecv_upload: Device has not been initialized!\n");
		return -1;
	}
	
	printf("%s", filename);
	FILE* file = fopen(filename, "rb");
	if(file == NULL) {
		printf("irecv_upload: Unable to find file!\n");
		return 1;
	}

	fseek(file, 0, SEEK_END);
	long len = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* buffer = malloc(len);
	if (buffer == NULL) {
		printf("irecv_upload: Error allocating memory!\n");
		fclose(file);
		return 1;
	}

	fread(buffer, 1, len, file);
	fclose(file);

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

		if(!usb_control_msg(handle, 0x21, 1, i, 0, &buffer[i * 0x800], size, 1000)) {
			printf("irecv_upload: Error sending packet!\n");
			return -1;
		}

		if(usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("irecv_upload: Error receiving status!\n");
			return -1;

		} else {
			if(response[4] != 5) {
				printf("irecv_upload: Invalid status error!\n");
				return -1;
			}
		}

	}

	usb_control_msg(handle, 0x21, 1, i, 0, buffer, 0, 1000);
	for(i = 6; i <= 8; i++) {
		if(usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("irecv_upload: Error receiving execution status!\n");
			return -1;

		} else {
			if(response[4] != i) {
				printf("irecv_upload: Invalid execution status!\n");
				return -1;
			}
		}
	}

	free(buffer);
	return 0;
}

int irecv_buffer(struct usb_dev_handle* handle, char* data, int len) {
	if(handle == NULL) {
		printf("irecv_buffer: Device has not been initialized!\n");
		return -1;
	}

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

		if(!usb_control_msg(handle, 0x21, 1, i, 0, &data[i * 0x800], size, 1000)) {
			printf("irecv_buffer: Error sending packet!\n");
			return -1;
		}

		if(usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("irecv_buffer: Error receiving status!\n");
			return -1;

		} else {
			if(response[4] != 5) {
				printf("irecv_buffer: Invalid status error!\n");
				return -1;
			}
		}

	}

	usb_control_msg(handle, 0x21, 1, i, 0, data, 0, 1000);
	for(i = 6; i <= 8; i++) {
		if(usb_control_msg(handle, 0xA1, 3, 0, 0, response, 6, 1000) != 6) {
			printf("irecv_buffer: Error receiving execution status!\n");
			return -1;

		} else {
			if(response[4] != i) {
				printf("irecv_buffer: Invalid execution status!\n");
				return -1;
			}
		}
	}

	return 0;
}

int irecv_command(struct usb_dev_handle *handle, int argc, char* argv[]) {
/*
    int i = 0;
    char command[0x200];
    memset(command, 0, 0x200);
	for(i = 0; i < argc; i++) {
	    size_t size = strlen(command);
		if(i > 0 && (size + 1) < 0x200) {
			strncat(command, " ", 1);
		}
		strncat(command, argv[i], 0x200 - size);
	}
*/
    char* command = argv[0];
	size_t length = strlen(command);
	if(length >= 0x200) {
		printf("irecv_command: Command is too long!\n");
		return -1;
	}

	if(!usb_control_msg(handle, 0x40, 0, 0, 0, command, length+1, 1000)) {
		printf("irecv_command: Error sending command!\n");
		return -1;
	}

	return 0;
}

int irecv_exploit(struct usb_dev_handle* handle, char* payload) {
    if(handle == NULL) {
		printf("irecv_exploit: Device has not been initialized!\n");
		return -1;
	}
	
    if(payload != NULL) {
        if(irecv_upload(handle, payload) < 0) {
            printf("irecv_exploit: Error uploading payload!\n");
            return -1;
        }
    }
    
    if(!usb_control_msg(handle, 0x21, 2, 0, 0, 0, 0, 1000)) {
		printf("irecv_exploit: Error sending exploit!\n");
		return -1;
	}
	
    return 0;
}

int irecv_parse(struct usb_dev_handle* handle, char* command) {
    unsigned int status = 0;
	char* action = strtok(strdup(command), " ");
	if(!strcmp(action, "help")) {
	    printf("Commands:\n");
	    printf("\t/exit\t\t\texit from recovery console.\n");
	    printf("\t/upload <file>\t\tupload file to device.\n");
	    printf("\t/exploit [payload]\tsend usb exploit packet.\n");
	    
	} else if(!strcmp(action, "exit")) {
	    free(action);
	    return -1;
	    
	} else if(strcmp(action, "upload") == 0) {
		char* filename = strtok(NULL, " ");
		if(filename != NULL) {
			irecv_upload(handle, filename);
		}
		
	} else if(strcmp(action, "exploit") == 0) {
		char* payload = strtok(NULL, " ");
		if (payload != NULL) {
			irecv_exploit(handle, payload);
		} else {
		    irecv_exploit(handle, NULL);
		}
	}
	
	free(action);
	return 0;
}

int irecv_console(struct usb_dev_handle *handle, char* logfile) {
	if(usb_set_configuration(handle, 1) < 0) {
	    printf("irecv_console: Error setting configuration!\n");
		return -1;
	}

	if(usb_claim_interface(handle, 1) < 0) {
	    printf("irecv_console: Error claiming interface!\n");
		return -1;
	}

	if(usb_set_altinterface(handle, 1) < 0) {
	    printf("irecv_console: Error setting alt interface!\n");
		return -1;
	}

	char* buffer = malloc(BUF_SIZE);
	if(buffer == NULL) {
		printf("irecv_console: Error allocating memory!\n");
		return -1;
	}
	
	FILE* fd = NULL;
	if(logfile != NULL) {
	    fd = fopen(logfile, "w");
	    if(fd == NULL) {
	        printf("irecv_console: Unable to open log file!\n");
	        free(buffer);
	        return -1;
	    }
	}

	while(1) {
		int bytes = 0;
		while(bytes >= 0) {
			memset(buffer, 0, BUF_SIZE);
			bytes = usb_bulk_read(handle, 0x81, buffer, BUF_SIZE, 500);
			int i;
			for(i = 0; i < bytes; ++i)
			{
				fprintf(stdout, "%c", buffer[i]);
				if(fd) fprintf(fd, "%c", buffer[i]);
			}
		}

		char* command = readline("iRecovery> ");
		if(command && *command) {
			add_history(command);
		}

        if(command[0] == '/') {
            if(irecv_parse(handle, &command[1]) < 0) {
                free(command);
                break;
            }
            
        } else {
            if(irecv_command(handle, 1, &command) < 0) {
                free(command);
                break;
            }
        }

		free(command);
	}
	
	free(buffer);
	if(fd) fclose(fd);
	usb_release_interface(handle, 1);
}

int irecv_list(struct usb_dev_handle* handle, char* filename) {

	if (handle == NULL) {
		printf("irecv_list: Device has not been initialized!\n");
		return -1;
	}
	
	//max command length
	char line[0x200];
	FILE* script = fopen(filename, "rb");

	if (script == NULL) {
		printf("irecv_list: unable to find file!\n");
		return -1;
	}
	
	printf("\n");

	while (fgets(line, 0x200, script) != NULL) {
		char *command[1];
		command[0] = line;

		if (command[0][0] == '/') { //Command (exploit/upload etc)
			char *com = command[0] + 1;

			printf("irecv_list: command> %s", com); //broken need to remove char10
			irecv_parse(handle, com);
		} else {
			printf("irecv_list: sending> %s", line);
			irecv_command(handle, 1, command);
		}

	}

	fclose(script);
}

void irecv_usage(void) {
	printf("./irecovery [args]\n");
	printf("\t-f <file>\t\tupload file.\n");
	printf("\t-c <command>\t\tsend a single command.\n");
	printf("\t-k [payload]\t\tsend usb exploit and payload.\n");
	printf("\t-s [logfile]\t\tstarts a shell, and log output.\n");
	printf("\t-l <file> \t\tsends a set of commands from a file (one per line).\n");
	printf("\t-r\t\t\treset usb.\n\n");
}

int main(int argc, char *argv[]) {
	printf("iRecovery - Recovery Utility\n");
	printf("by westbaer\nThanks to pod2g, tom3q, planetbeing, geohot and posixninja.\n\n");
	if(argc < 2) {
		irecv_usage();
		return -1;
	}

	struct usb_dev_handle* handle = irecv_init(RECV_MODE);
	if (handle == NULL) {
		handle = irecv_init(WTF_MODE);
		if (handle == NULL) {
		    printf("No iPhone/iPod found.\n");
		    return -1;
		    
		} else {
		    printf("Found iPhone/iPod in DFU/WTF mode\n");
		}
	} else {
	    printf("Found iPhone/iPod in Recovery mode\n");
	}
	
	if(!strcmp(argv[1], "-f")) {
	    if(argc == 3) {
            irecv_upload(handle, argv[2]);
        }
            
	} else if(!strcmp(argv[1], "-c")) {
	   	if(argc >= 3) {
	        irecv_command(handle, argc-2, &argv[2]);
	    }

	} else if(!strcmp(argv[1], "-k")) {
	   	if(argc >= 3) {
	        irecv_exploit(handle, argv[2]);
	    } else {
	        irecv_exploit(handle, NULL);
	    }

	} else if(!strcmp(argv[1], "-s")) {
	   	if(argc >= 3) {
            irecv_console(handle, argv[2]);
	    } else {
	        irecv_console(handle, NULL);
	    }
	    
	} else if(!strcmp(argv[1], "-r")) {
        irecv_reset(handle);
        
	} else if (!strcmp(argv[1], "-l")) {
	    irecv_list(handle, argv[2]);
	}
	
	irecv_close(handle);
	return 0;
}

