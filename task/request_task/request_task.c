#include "request_task.h"
#include "../task.h"
#include "../../fifo/fifo.h"
#include "../../timestamp/timestamp.h"

#include <stdio.h> 			/* printf, sprintf */
#include <stdint.h> 		/* data types */
#include <stdlib.h> 		/* size_t */
#include <sys/types.h>		/* ssize_t */
#include <unistd.h> 		/* read, write, close */
#include <string.h> 		/* memcpy, memset */
#include <sys/socket.h> 	/* socket, connect */
#include <netinet/in.h> 	/* struct sockaddr_in, struct sockaddr */
#include <netdb.h> 			/* struct hostent, gethostbyname */
#include <fcntl.h>			/* File (socket) control - used for setting async */
#include <errno.h>			/* Socket error reporting */


/* LOCALS *********************************************************************/

/* Current socket state */
static int8_t socket_state;

/* Socket file descriptor */
static int32_t sockfd;
/* Struct with addres and port */
static struct sockaddr_in serv_addr;

/* Hostname string  */
static char host[HOST_ADDR_BUF_SIZE];

/* Request including headers, body ... */
static char request_buf[REQUEST_BUF_SIZE];
/* Response including headers, body ... */
static char response_buf[RESPONSE_BUF_SIZE];

/* Read/write byte counters */
static ssize_t bytes_sent;
/* Read bytes amount, or 'response_buf' write ptr */
static ssize_t bytes_read;
/* Previous amount of bytes, that were read */
static ssize_t prev_read_result;


/* GLOBALS ********************************************************************/

/* Bears only the JSON data (request body) */
char request_data_buf[REQUEST_FIFO_STR_SIZE];

/* Fifo for data storage */
str_fifo_t request_fifo = {
	0,
	0,
	REQUEST_FIFO_BUF_SIZE,
	REQUEST_FIFO_STR_SIZE,
	NULL
};

/* Timestamp - gets written externally. Static to avoid linkage conflicts. */
static char timestamp[TIMESTAMP_RAW_STRING_SIZE];

/* Used to measure time in single state */
static long int state_change_time = 0;

/* Used to measure time in single state */
static long int retry_time = 0;


/* PROTOTYPES *****************************************************************/

int8_t (*_get_socket_state_funciton(void))(void) ;

int8_t _idle_socket(void);
int8_t _create_socket(void);
int8_t _connect_socket(void);
int8_t _add_request_data(void);
int8_t _write_socket(void);
int8_t _read_socket(void);
int8_t _evaluate_socket(void);
int8_t _close_socket(void);
int8_t _clear_request_buffers(void);

int8_t _check_fifo_for_new_data (void);

void _report_socket_errno(void);

int8_t _has_max_state_timer_ended(void);
int8_t _has_retry_timer_ended(void);
void _state_timer_reset_all(void);
void _state_timer_reset_max(void);
void _timer_reset_retry(void);
void _report_max_state_timer_ended (void);

void _reset_static_vars(void);


/* FUNCTIONS ******************************************************************/

/*  Point outer pointer to local fifo struct and init storage for fifo.
 */
int8_t request_task_init_fifo (str_fifo_t **_fifo) {
    /* Set outer pointer to point to fifo local struct */
    *_fifo = &request_fifo;
    /* Set up memory for fifo struct and return success/error */
    int8_t fifo_status = setup_str_fifo(
        &request_fifo, REQUEST_FIFO_BUF_SIZE, REQUEST_FIFO_STR_SIZE);

#if(DEBUG_REQUEST==1)
	printf("*\tREQUEST FIFO INITIATED\n");
#endif

    return fifo_status;
}


/*  Init socket using host address and port.s
 */
int8_t request_task_init_socket(char *_host, int16_t portno) {

    /* Check hostname length */
    if (strlen(_host) > HOST_ADDR_BUF_SIZE-1) {
    	/* Refresh local timestamp variable and report error */
		get_timestamp_raw(timestamp);
		printf("SOCKET FATAL: HOST ADDRESS TOO LONG | %s\n", timestamp);
        return -1;
    }

	/* Copy hostname to local string (including '/0') */
    memcpy(host, _host, strlen(_host)+1);

	/* Server data */
    struct hostent *server;
    /* Get server data by domain name, or IPv4 */
    server = gethostbyname(host);

	if (server == NULL) {
		socket_state = SOCKET_STATE_UNKNOWN_HOST;
    	/* Refresh local timestamp variable and report error */
		get_timestamp_raw(timestamp);
		printf("SOCKET FATAL: UNKNOWN HOST | %s\n", timestamp);
		return -1;
	}

    /* Initialize server */
    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    _state_timer_reset_max();

    /* Set socket state variable */
	socket_state = SOCKET_STATE_IDLE;

#if(DEBUG_REQUEST==1)
	printf("*\tSOCKET INITIATED\n");
	printf("Host: %s\n", _host);
	printf("Port: %d\n", portno);
#endif

    return 0;
}


/*  Check for data, create and enable socket, write, read and evaluate.
 */
int8_t request_task_run(void) {

	if (_has_retry_timer_ended() != 0) {
		return TASK_STATUS_BUSY;
	}

#if(DEBUG_REQUEST==1)
	printf("REQUEST TASK, state: %d\n", socket_state);
#endif


	/* Declare function pointer, that is: int8_t myFun (void) {...} */
    int8_t (*state_fun_ptr) (void);

    /* Get pointer with regard to current socket state, accepts no arguments */
    state_fun_ptr = _get_socket_state_funciton();

    /* Call socket state function and save status output */
    int8_t status = state_fun_ptr();

    switch (status) {
    case SOCKET_ERROR:
    	return TASK_STATUS_ERROR;
    	break;
    case SOCKET_CHANGE_STATE:
    	_state_timer_reset_max();		/* Reset timer on change */
    	return TASK_STATUS_BUSY;
    	break;
    case SOCKET_NO_CHANGE:
    	/* Close socket if timer has elepsed */
		if (_has_max_state_timer_ended() == 0) {
			_report_max_state_timer_ended();
			socket_state = SOCKET_STATE_CLOSE;
			return TASK_STATUS_BUSY;
		}
    	return TASK_STATUS_BUSY;
    	break;
    case SOCKET_IDLE:
    	_state_timer_reset_max();		/* Idle is not time bound */
    	return TASK_STATUS_IDLE;
    	break;
    default:
    	printf("Unknown socket status\n");
    	return TASK_STATUS_ERROR;
    	break;
    }

}


/* 	Get socket state function pointer with regard to current socket state.
 *  Second '(void)' means it takes no arguments.
 *
 *	return:
 *		function pointer of type:	int8_t myFun (void) {...}
 */
int8_t (*_get_socket_state_funciton(void))(void) {
    int8_t (*state_fun_ptr) (void);

#if(DEBUG_REQUEST==1)
    printf("Socket state: %d\n", socket_state);
#endif

    switch (socket_state) {
    case SOCKET_STATE_IDLE:
    	state_fun_ptr = &_idle_socket;
        break;
    case SOCKET_STATE_CREATE:
    	state_fun_ptr = &_create_socket;
    	break;
	case SOCKET_STATE_CONNECT:
    	state_fun_ptr = &_connect_socket;
		break;
	case SOCKET_STATE_ADD_DATA:
    	state_fun_ptr = &_add_request_data;
		break;
    case SOCKET_STATE_WRITE:
    	state_fun_ptr = &_write_socket;
        break;
    case SOCKET_STATE_READ:
    	state_fun_ptr = &_read_socket;
        break;
    case SOCKET_STATE_EVAL_RESPONSE:
    	state_fun_ptr = &_evaluate_socket;
        break;
    case SOCKET_STATE_CLOSE:
    	state_fun_ptr = &_close_socket;
        break;
    default:
    	state_fun_ptr = &_close_socket;
        break;
    }

    return state_fun_ptr;
}


/* 	Check, if fifo contains new request data.
 *
 *  Next state:
 *  	SOCKET_STATE_CREATE
 *
 *  return:
 *  	-1: error
 *		 0: new data available
 *		 2: idle
 */
int8_t _idle_socket(void) {
	if (_check_fifo_for_new_data() == 0) {
#if(DEBUG_REQUEST==1)
		printf("*\tSOCKET FIFO DATA DETECTED\n");
#endif
        socket_state = SOCKET_STATE_CREATE;
        return 0;
	}
	return 2;
}


/* Select socket protocol and stream, create socket.
 *
 *  Next state:
 *  	SOCKET_STATE_CONNECT
 *
 *  returns:
 *  	-1: error creating
 *		 0: successfully creted
 */
int8_t _create_socket(void) {

    /* Like 'open()' for files
     *  AF_INET - IPv4
     *  SOCK_STREAM - Provides sequenced, reliable, two-way streams
     *  0 - default protocol selector
     */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

#if(DEBUG_REQUEST==1)
	printf("socket(): %d\n", sockfd);
    printf("errno: %d | %s\n", errno, strerror(errno));
#endif

    /* Catch error, which is not EINPROGRESS.
     * Normal: EINPROGRESS is thrown in non blocking operations
     */
    //if (sockfd == -1 && errno != EINPROGRESS) {
    if (sockfd == -1) {
    	if (errno != EINPROGRESS) {
			_report_socket_errno();
			socket_state = SOCKET_STATE_CLOSE;
    	}
        return 0;
    }

    /* Catch refused error, because socket() returns positive on refused. */
    /*if (errno == ECONNREFUSED) {
		_report_socket_errno();
        socket_state = SOCKET_STATE_CLOSE;
        return 0;
    }*/

    /* Set to non blocking */
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    //fcntl(sockfd, F_SETFL, flags);

    /* Set socket state variable */
    socket_state = SOCKET_STATE_CONNECT;

#if(DEBUG_REQUEST==1)
	printf("*\tSOCKET CREATED\n");
#endif

    return 0;
}


/*	Connect socket to defined address.
 *
 *  Next state:
 *  	SOCKET_STATE_ADD_DATA
 *
 *  returns:
 *  	-1: error connecting
 *		 0: successfully connected
 */
int8_t _connect_socket(void){

    /* Connect the socket to the previously defined address */
	int connected =
		connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

#if(DEBUG_REQUEST==1)
	printf("connect(): %d\n", connected);
    printf("errno: %d | %s\n", errno, strerror(errno));
#endif

    /* Catch error, which is not EINPROGRESS. */
    //if (connected == -1 && errno != EINPROGRESS) {
    if (connected == -1) {
    	if (errno != EINPROGRESS && errno != EALREADY) {
			_report_socket_errno();
			socket_state = SOCKET_STATE_CLOSE;
    	}
        return 0;
    }

    /* Set socket state variable */
	socket_state = SOCKET_STATE_ADD_DATA;

#if(DEBUG_REQUEST==1)
	printf("*\tSOCKET CONNECTED\n");
#endif

	return 0;
}


/*	Take data from fifo and copy to request buffer.
 *
 *  Next state:
 *  	SOCKET_STATE_WRITE
 *
 *  returns:
 *  	-1: error
 *		 0: success
 */
int8_t _add_request_data(void) {
	/* Reset read/write byte counters */
	_reset_static_vars();
	/* Clear all buffers */
    _clear_request_buffers();
    /* Get row of data from fifo buffer */
    str_fifo_read(&request_fifo, request_data_buf);
    /* Add request data to request buffer */
    sprintf(request_buf, REQUEST_FMT,
		host, (long unsigned int)strlen(request_data_buf), request_data_buf);
    /* Set socket state variable */
    socket_state = SOCKET_STATE_WRITE;

#if(DEBUG_REQUEST==1)
	printf("\tADDED REQUEST DATA (%lu):\n%s\n",
		(long unsigned int)strlen(request_buf), request_buf);
#endif

    return 0;
}


/*	Take request buffer and write it to the socket.
 *
 *  Next state:
 *  	SOCKET_STATE_READ
 *
 *  returns:
 *  	-1: error
 *		 0: finished
 *		 1: still writing
 */
int8_t _write_socket(void) {

    /* Only write the bytes containing data (string) */
    ssize_t request_len = strlen(request_buf);

    /* Write and get amount of bytes, that were written
     * 	-1: can't write
     * 	 0: nothing to write
     * 	>0: number of bytes written
     */
    size_t result =
		write(sockfd, request_buf + bytes_sent, request_len - bytes_sent);

#if(DEBUG_REQUEST==1)
	printf("write(): %ld, %ld, %ld\n", (long int)result, (long int)bytes_sent, (long int)request_len);
    printf("errno: %d | %s\n", errno, strerror(errno));
#endif

    /* Couldn't write, busy (non-blocking) */
    if (result == -1) {
    	/* Normal: EINPROGRESS, EAGAIN is thrown in non blocking operations */
    	if (errno != EINPROGRESS && errno != EAGAIN) {
			_report_socket_errno();
			socket_state = SOCKET_STATE_CLOSE;
    	}
		return 0;
	}

    /* Increment bytes_read ('request_buf' idx pointer) */
	bytes_sent += result;

    /* Finished writing (writen everything, nonthing else left) */
    if (request_len == bytes_sent || result == 0) {
#if(DEBUG_REQUEST==1)
    	printf("*\tREQUEST WRITTEN (%lu): \n%s\n",
			(long unsigned int)request_len, request_buf);
#endif
        /* Set socket state variable */
        socket_state = SOCKET_STATE_READ;

        return 0;
    }

    return 1;
}


/*	Read from socket and write to response buffer.
 * 	Wait for a full response.
 *
 *  Next state:
 *  	SOCKET_STATE_EVAL_RESPONSE
 *
 * 	return:
 *  	-1: error
 *		 0: finished
 *		 1: still reading
 */
int8_t _read_socket(void) {

    /* Read and get amount of bytes, that were read
     * 	-1: nothing new
     * 	 0: can't access read data
     * 	>0: number of bytes read
     */
    ssize_t result =
    		read(sockfd, response_buf + bytes_read, RESPONSE_BUF_SIZE - bytes_read);

#if(DEBUG_REQUEST==1)
	printf("read(): %ld, %ld, %d\n", (long int)result, (long int)bytes_read, RESPONSE_BUF_SIZE);
	printf("read(): \n%s\n", response_buf);
    printf("errno: %d | %s\n", errno, strerror(errno));
//	printf("%d | %d | %d | %d | %d | %d | %d | %d \n",
//		EAGAIN, EWOULDBLOCK, EBADF, EFAULT, EINTR, EINVAL, EIO, EISDIR);
#endif

    /* Check for socket error */
    //if (errno == ENOTCONN || errno == EBADF)
    //if (result == 0) {
    if (result == 0 && errno != EINPROGRESS) {
		_report_socket_errno();
        socket_state = SOCKET_STATE_CLOSE;
        return 0;
    }

    /* If read successfull, increment bytes_read ('response_buf' idx pointer) */
    if (result > 0) {
        bytes_read += result;
    }

    /* Reached end of buffer */
    if (bytes_read == RESPONSE_BUF_SIZE-1) {
    	/* Refresh local timestamp variable and report error */
		get_timestamp_raw(timestamp);
		printf("SOCKET FATAL: RESPONSE BUFFER TOO LONG | %s\n", timestamp);
        //socket_state = SOCKET_STATE_CLOSE;
	    return -1;
	}

#if(DEBUG_REQUEST==1)
    printf("Previous, current, total read(): %ld, %ld, %ld\n",
		(long int)prev_read_result, (long int)result, (long int)bytes_read);
#endif

	/* Check for end of response */
    if (prev_read_result > 0) {		/* Previously read something */
    	if (result == -1) {		/* Nothing new was read */
#if(DEBUG_REQUEST==1)
			printf("*\tRESPONSE RECEIVED (%ld):\n%s\n",
				(long int)bytes_read, response_buf);
#endif
            socket_state = SOCKET_STATE_EVAL_RESPONSE;

    		return 0;
    	}
    }

    /* Set for next function call */
	prev_read_result = result;

    return 1;
}


/*	Read response buffer and compare with sent data.
 *  On successful evaluation, increment fifo read pointer.
 *
 *  Next state:
 *  	SOCKET_STATE_ADD_DATA - fifo contains more data
 *  	SOCKET_STATE_CLOSE - fifo empty
 *
 * 	return:
 *  	-1: error
 *		 0: data OK
 *		 1: still evaluating
 */
int8_t _evaluate_socket(void) {

    /* Pointer to 'request_data_buf' substring inside of 'response_buf' */
    char *request_ok = strstr(response_buf, request_data_buf);
    char *request_200 = strstr(response_buf, "200 OK");
    char *request_400 = strstr(response_buf, "400 Bad Request");

#if(DEBUG_REQUEST==1)
		printf("*\tEVALUATING\n%s\n", response_buf);
		printf("*\tWITH\n%s\n", request_data_buf);
#endif



	if (request_200 != NULL) {
		printf( "\nReceived response code 200, continue with next request.\n\n");
	} else {
    	/* Check if JSON syntax s correct.
		 * The first request after starting the app may contain missing chars.
		 * The missing chars are usually in the region 40-80 (hash-error) */
		if (request_400 != NULL) {
			printf( "\nReceived response code 400, "
					"skip and continue with next request.\n\n");
		} else {
			printf("\nReceived non-200, non-400 response code - retry write.\n\n");
		}
		printf(
			"\tOriginal request:\n%s\n"
			"\tResponse:\n%s\n",
			request_buf, response_buf);
    }

    /* Check, if match in string comparison exists */
	if (request_ok != NULL){
#if(DEBUG_REQUEST==1)
		printf("*\tRESPONSE OK\n");
#endif
	}

    /* Check for response */
	if (request_ok != NULL || request_400 != NULL){
		/* Increment read pointer, means next data row can be sent */
		if (fifo_increment_read_idx(&request_fifo) != 0) {
			/* Is this error possible (?) */
	    	/* Refresh local timestamp variable and report error */
			get_timestamp_raw(timestamp);
			printf("SOCKET FATAL: INCREMENT EMPTY FIFO | %s\n", timestamp);
			//return 0;
			return -1;
		}
		if (_check_fifo_for_new_data() == 0) {
	        socket_state = SOCKET_STATE_ADD_DATA;
		} else {
	        socket_state = SOCKET_STATE_CLOSE;
		}

		return 0;
	}

    //return 1;

	/* If no match was found in response, close and try reconnecting */
#if(DEBUG_REQUEST==1)
		printf("SOCKET EVALUATION FAILED\n");
#endif
    socket_state = SOCKET_STATE_CLOSE;
	return 0;
}


/*	Close the socket and destroy file descriptor.
 *
 *  Next state:
 *  	SOCKET_STATE_IDLE
 *
 * 	return:
 *  	-1: error
 *		 0: success
 *		 1: retry
 */
int8_t _close_socket(void) {

	/* Every time when closing, reset retry timer.
	 *  In normal operation 60 s will pass between incoming packages, so the
	 * wait amount is negegible. If there is data in the FIFO for syncing,
	 * close() will only get called at the end of the process.
	 *  In the case of an error (disconnect), the all states are redirected
	 * to CLOSE, so the execution will get slowed down, as desired. */
	_timer_reset_retry();

    if (close(sockfd) != 0) {
		_report_socket_errno();
        /* Common error when trying to close unopened socket */
		if (errno == EBADF) {
			socket_state = SOCKET_STATE_IDLE;
	        return 0;
		}
        return 1;
    }
    socket_state = SOCKET_STATE_IDLE;
#if(DEBUG_REQUEST==1)
		printf("*\tSOCKET CLOSED\n");
#endif
    return 0;
}


/*	Check if fifo has any pending data.
 *
 *  return:
 *  	-1: error
 *		 0: new data available
 *		 1: nothing new
 */
int8_t _check_fifo_for_new_data (void) {

	/* Check for pending data */
    if (str_fifo_read(&request_fifo, request_data_buf) == 0) {
        return 0;
    }

	return 1;
}


/* Reset read/write byte counters (on error, or timer elapsed)
 */
void _reset_static_vars(void){
	 bytes_sent = 0;
	 bytes_read = 0;
	 prev_read_result = 0;
	 return;
}


/* 	Write zeroes to request, response and request data buffers.
 */
int8_t _clear_request_buffers(void) {
    memset(request_buf, 0, REQUEST_BUF_SIZE);
    memset(request_data_buf, 0, REQUEST_FIFO_STR_SIZE);
    memset(response_buf, 0, RESPONSE_BUF_SIZE);
    return 0;
}


/*	Prints socket state, error #, verbose and timestamp.
 */
void _report_socket_errno(void) {
    get_timestamp_raw(timestamp);
    printf("Socket internal error: \n"
		"\t State: %d \n"
		"\t Error: (%d) %s \n"
		"\t Time: %s\n",
		socket_state, errno, strerror(errno), timestamp);
    return;
}


/*	Check if max allowed time in socket state has ended and reset timer.
 *
 * 	return:
 * 		0: max allowed time reached
 * 		1: timer still running
 */
int8_t _has_max_state_timer_ended(void) {
	long int timer_now;
	get_timestamp_epoch(&timer_now);
	if (timer_now - state_change_time > SOCKET_MAX_STATE_TIME_S) {
//      get_timestamp_raw(timestamp);
//		printf("MAX ALLOWED SOCKET TIME REACHED: %d | %s\n",
//				socket_state, timestamp);
		/* Reset timer */
    	_state_timer_reset_max();
		return 0;
	}
	return 1;
}


/*	Check if max allowed time in socket state has ended and reset timer.
 *
 * 	return:
 * 		0: max allowed time reached
 * 		1: timer still running
 */
int8_t _has_retry_timer_ended(void) {
	long int timer_now;
	get_timestamp_epoch(&timer_now);
	if (timer_now - retry_time > SOCKET_RETRY_STATE_TIME_S) {
		/* Reset timer */
    	//_timer_reset_retry();
		return 0;
	}
	return 1;
}


/*	Reset all state timers.
 */
void _state_timer_reset_all(void) {
	_state_timer_reset_max();
	_timer_reset_retry();
	return;
}


/*	Reset max state timer.
 */
void _state_timer_reset_max(void) {
	get_timestamp_epoch(&state_change_time);
	return;
}


/*	Reset retry state timer.
 */
void _timer_reset_retry(void) {
	get_timestamp_epoch(&retry_time);
	return;
}


/*	Prints max timer elapsed error.
 */
void _report_max_state_timer_ended (void) {
	printf("SOCKET TIME ELAPSED\n");
	_report_socket_errno();
	return;
}

