/*
 *  Useful links:
 *   POSIX UART: https://www.cmrr.umn.edu/~strupp/serial.html
 *   File/port open: http://man7.org/linux/man-pages/man2/open.2.html
 *
 *
 */



#include "fifo/fifo.h"
//#include "serial/serial.h"
#include "task/serial/serial.h"
#include "task/buffer_task/buffer_task.h"
#include "task/storage_task/storage_task.h"
#include "task/request_task/request_task.h"

#include <stdio.h>      /* Standard input/output definitions */
#include <unistd.h>     /* Sleep */


/* Relative path to measurement directory within base dir. */
#define MEASUREMENTS_FILENAME               "/measurement/data.json"

#define SERIAL_PORTNAME                     "/dev/ttyACM0"
//#define SERVER_HOSTNAME                     "127.0.0.1"
#define SERVER_HOSTNAME                     "10.0.0.51"
//#define SERVER_HOSTNAME                     "www.node.anemo.si"
#define SERVER_PORT                         (5760)
//#define SERVER_PORT                         (8080)

#define SHORT_SLEEP_TIME_US					10000		/* 10 ms */
#define LONG_SLEEP_TIME_US					1000000		/* 1 s */



/* Pooling based tasks */
int8_t (*task_ptrs[]) (void) = 
    {&serial_task_run, &buffer_task_run, &request_task_run, &storage_task_run};
    //{&serial_task_run, &buffer_task_run, &request_task_run};
	//{&buffer_task_run};
/* Get number of tasks */
int8_t num_of_tasks = (sizeof(task_ptrs) / sizeof(task_ptrs[0]));


/* Pointer to three fifo buffers
 *  1: Raw incoming UART data
 *  2: Data storage buffer
 *  3: Requests buffer
 */
str_fifo_t *fifo_buffers[3];


/*
 * 	p2: (if give) serial port path name
 */
int main (int argc, char* argv[]) {

	char _time[32] = {0};
	get_timestamp_raw(_time);
	printf("\nStarted at: %s\n\n", _time);

	char serial_portname [PORTNAME_STRING_LEN] = {0};
	if (argc == 1) {
		sprintf(serial_portname, "%s", SERIAL_PORTNAME);
	} else if (argc == 2) {
		sprintf(serial_portname, "%s", argv[1]);
	} else {
		printf("Incompatible number of arguments (%d)\n", argc);
		return -1;
	}


    /* Init serial fifo */
    if (serial_init_fifo(&fifo_buffers[0]) != 0) {
        printf("Error: serial_init_fifo");
        return -1;
    }
    /* Init serial port */
    //if (serial_init_port(SERIAL_PORTNAME) != 0) {
    if (serial_init_port(serial_portname) != 0) {
        printf("Error: serial_init_port");
        return -1;
    }
    /* Open serial port */
    if (serial_open_port() != 0) {
        printf("Error: serial_open_port");
        return -1;
    }


    /* Init data storage fifo */
    if (storage_task_init_fifo(&fifo_buffers[1]) != 0) {
        printf("Error: serial_init_fifo");
        return -1;
    }

    /* Init data storage file */
    if (storage_task_init_file(MEASUREMENTS_FILENAME) != 0) {
        printf("Error: serial_init_data_storage");
        return -1;
    }

    /* Init requests fifo */
    if (request_task_init_fifo(&fifo_buffers[2]) != 0) {
        printf("Error: request_task_init_fifo");
        return -1;
    }
    /* Init requests socket */
    if (request_task_init_socket(SERVER_HOSTNAME, SERVER_PORT) != 0) {
        printf("Error: request_task_init_host_and_port");
        return -1;
    }

    /* Last of all! */
    buffer_task_init(fifo_buffers);


    printf("\n*\tInit successful:\n");

    printf("Number of tasks: %d\n", num_of_tasks);
    printf("Sleep ampunt [us]:  %d and %d\n",
		SHORT_SLEEP_TIME_US, LONG_SLEEP_TIME_US);

	printf("Fifo addresses (for later error handling):\n"
			"0: \t%p\n"
			"1: \t%p\n"
			"2: \t%p\n",
			(void *)fifo_buffers[0],
			(void *)fifo_buffers[1],
			(void *)fifo_buffers[2]);


    printf("\n*\tBegin main loop\n\n");


    /* Task status returned as each task function's output */
    int8_t tmp_task_status;

    /* Task status accumulator, keeps track of busy tasks */
    int8_t is_sys_idle;

    /* Sleep time on each loop to slow down execution */
    uint64_t sleep_time_us;

    /* Task pointer index */
    int task_idx;


    while (1) {
    	/* Reset each time before tasks loop */
    	is_sys_idle = 0;

    	//serial_task_run();

    	/* Iterate and run tasks */
    	for (task_idx=0; task_idx < num_of_tasks; task_idx++) {

	        /* Call i-th task using function pointer. */
			tmp_task_status = task_ptrs[task_idx]();

			/* Check for fatal error within task. */
			if (tmp_task_status == -1) {
				printf("FATAL ERROR\n");
				return -1;
			}

			/* Check for busy tasks, which need to avoid system sleep. */
			is_sys_idle += tmp_task_status;
    	}

		//printf("is_sys_idle: %d \n", is_sys_idle);

		/* If no busy tasks, go to (interruptable) sleep */
		if (is_sys_idle == 0) {
			sleep_time_us = LONG_SLEEP_TIME_US;
		} else {
			sleep_time_us = SHORT_SLEEP_TIME_US;
			//sleep_time_us = LONG_SLEEP_TIME_US;
		}

		/* Go to (interruptable) sleep */
		usleep(sleep_time_us);
		//printf("AROUND\n");


        /*printf( "***FIFO POINTERS: \n"
        		"\t%d, %d\n"
        		"\t%d, %d\n"
        		"\t%d, %d\n",
        		fifo_buffers[0]->write_idx, fifo_buffers[0]->read_idx,
        		fifo_buffers[1]->write_idx, fifo_buffers[1]->read_idx,
        		fifo_buffers[2]->write_idx, fifo_buffers[2]->read_idx);*/
    }

    return 0;
}
