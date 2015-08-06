CC = gcc
CFLAGS_OSX = -lusb-1.0 -lobjc -framework CoreFoundation -framework IOKit -lreadline
CFLAGS_LNX = -lusb-1.0 -lreadline
CFLAGS_WIN = -lusb-1.0 -lreadline

all:
		@echo 'ERROR: no platform defined.'
		@echo 'LINUX USERS: make linux'
		@echo 'MAC OS X USERS: make macosx'
	 	@echo 'WINDOWS USERS: make win'
macosx:	
		@echo 'Buildling iRecovery (Mac Os X)'
		@$(CC) irecovery.c -o irecovery $(CFLAGS_OSX)
		@echo 'Successfully built iRecovery'
linux:
		@echo 'Buildling iRecovery (Linux)'
		@$(CC) irecovery.c -o irecovery $(CFLAGS_LNX)
		@echo 'Successfully built iRecovery'
win:
		@echo 'Buildling iRecovery (Windows)'
		@$(CC) irecovery.c -o irecovery -I "C:\MinGW\include" -L "C:\MinGW\lib" $(CFLAGS_WIN)
		@echo 'Successfully built iRecovery'
clean:
		@echo 'Cleaning...'
		@rm -rf *.o irecovery
		@echo 'Cleaning finished.'

