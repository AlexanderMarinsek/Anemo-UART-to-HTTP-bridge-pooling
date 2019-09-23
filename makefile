
.PHONY = all clean

CC = gcc
CFLAGS = -g -Wall -I.

#Get current directory, convert to string and pass to C code
CFLAGS += -DCURDIR=\"${CURDIR}\"

# -- list of dependencies -> header files
DEPS = 	fifo/fifo.h								\
		timestamp/timestamp.h					\
	    task/serial/serial.h					\
	    task/buffer_task/buffer_task.h			\
	    task/task/task.h						\
		task/storage_task/storage_task.h		\
		task/request_task/request_task.h

# -- list of objet files
OBJ = 	main.o									\
		fifo/fifo.o								\
		timestamp/timestamp.o					\
		task/serial/serial.o					\
		task/buffer_task/buffer_task.o			\
		task/storage_task/storage_task.o		\
		task/request_task/request_task.o

# -- list of phony targets
.PHONY: clean

# -- default command
all: main

# -- make main module
main: $(OBJ)
	$(CC) $(CFLAGS) $^ -o bin/$@

# -- object files assembly rule
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@
	

# -- delete .o files in main directory and sub-directories
clean:
	rm *.o */*.o */*/*.o
