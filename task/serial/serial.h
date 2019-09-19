#ifndef SERIAL_H
#define SERIAL_H

#include "../../fifo/fifo.h"
#include "../../timestamp/timestamp.h"
#include <stdint.h>                 /* Data types */
#include <errno.h>			/* Socket error reporting */

#define PORTNAME_STRING_LEN                 (64)
#define PORT_PATHNAME                       "/dev/ttyACM0"

//#define SERIAL_FIFO_BUFFER_SIZE             (64)
/* For async - in case each byte gets received seperately */
//#define SERIAL_FIFO_BUFFER_SIZE             (FIFO_STRING_SIZE)

/* No need for large buffer - tasks run one after the onther cyclically */
#define SERIAL_FIFO_BUFFER_SIZE             (32)

/* In pooling based task, reserve space for more incoming data */
#define SERIAL_FIFO_STRING_SIZE				(1024)


/* Include space for later added timestamp */
#define RAW_FIFO_STRING_SIZE                \
    (FIFO_STRING_SIZE - TIMESTAMP_JSON_STRING_SIZE)


/*  Init raw serial data fifo.
 *   p1: pointer to fifo struct pointer
 *  return: 0 on success, -1 on error
 */
int8_t serial_init_fifo(str_fifo_t **fifo);

/*  Init serial port.
 *   p1: pointer to fifo struct pointer
 *  return: 0 on success, -1 on error
 */
int8_t serial_init_port (char *_portname);

/*  Open serial port.
 *
 *  return: 0 on success, -1 on error
 */
int8_t serial_open_port (void);

/*	Check for data in serial buffer (pooling based).
 *
 *	return: 0 on success, -1 on error
 */
int8_t serial_task_run (void);


#endif
