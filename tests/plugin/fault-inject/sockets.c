/*
 * sockets.c
 * 
 * https://forums.xilinx.com/t5/Embedded-Linux/Why-doesn-t-setting-TCP-NODELAY-work-on-socket/td-p/920902
 * http://wiki.linuxquestions.org/wiki/Connecting_a_socket_in_C
 * https://www.binarytides.com/socket-programming-c-linux-tutorial/
 * http://man7.org/linux/man-pages/man2/socket.2.html
 */

/********************************** includes **********************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <glib.h>

#include "sockets.h"


/******************************** definitions *********************************/
#define MAX_ERR_BUF_SIZE 256
// #define MAX_RECV_BUF_SIZE 1024


/********************************** globals ***********************************/
// for communication with the supervisor
static int sockfd;
static struct sockaddr_in sock_info;
// error messages
static char errMsg[MAX_ERR_BUF_SIZE];
// static char recvBuf[MAX_RECV_BUF_SIZE];


/**************************** function prototypes *****************************/
int send_raw(const void* data, size_t size);
int send_len(uint32_t len);
int read_raw(void* data, size_t len);
int read_len(uint32_t* len);


/********************************* functions **********************************/
int sockets_init(uint16_t hostport, char* hostname)
{
    // connect the socket
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        return -1;
    }
    sock_info.sin_addr.s_addr = inet_addr(hostname);
    sock_info.sin_family = AF_INET;
    sock_info.sin_port = htons(hostport);

    if (connect(sockfd, &sock_info, sizeof(sock_info)) < 0) {
        snprintf(errMsg, MAX_ERR_BUF_SIZE, "ERROR: connecting to socket for sending!\n");
        return -1;
    }
    int yes = 1;
    // http://www.unixguide.net/network/socketfaq/2.16.shtml
    if (setsockopt(sockfd,           /* socket affected */
                    IPPROTO_TCP,     /* set option at TCP level */
                    TCP_NODELAY,     /* name of option */
                    (char *) &yes,   /* the cast is historical cruft */
                    sizeof(yes))     /* length of option value */
    ) {
        // requires C11
        // size_t errmsglen = strerrorlen_s(errno) + 1;
        // char errmsg[errmsglen];
        // strerror_s(errmsg, errmsglen, errno);
        char* errno_msg = strerror(errno);
        snprintf(errMsg, MAX_ERR_BUF_SIZE, 
                "ERROR: changing socket properties!\n%s\n", errno_msg);
        return -2;
    }

    return 0;
}

int sockets_exit(void)
{
    if (shutdown(sockfd, SHUT_RDWR)) {
        char* errno_msg = strerror(errno);
        snprintf(errMsg, MAX_ERR_BUF_SIZE, 
                "ERROR: shutting down socket connection!\n%s\n", errno_msg);
        return -4;
    }

    close(sockfd);

    return 0;
}

// https://stackoverflow.com/a/58019967/12940429
int send_raw(const void* data, size_t size)
{
    ssize_t sent;
    const char* buffer = (const char*) data;

    // loop keeps trying until all bytes are sent
    while (size > 0) {
        sent = send(sockfd, buffer, size, 0);
        if (sent < 0) {
            snprintf(errMsg, MAX_ERR_BUF_SIZE, "ERROR: sending message!\n");
            return -1;
        }
        size -= sent;
    }
    return 0;
}

int send_len(uint32_t len)
{
    // convert byte-ordering as needed
    len = htonl(len);
    return send_raw(&len, sizeof(len));
}

int sockets_send(const void* buf, size_t len)
{
    // first send the message length, then the message
    if (send_len(len)) {
        return -1;
    }
    return send_raw(buf, len);
}

int read_raw(void* data, size_t len)
{
    ssize_t recvd;
    char* buffer = (char*) data;

    // keep trying until we get the target length
    while (len > 0) {
        recvd = recv(sockfd, buffer, len, 0);
        if (recvd < 0) {
            char* errno_msg = strerror(errno);
            snprintf(errMsg, MAX_ERR_BUF_SIZE, 
                    "ERROR: receiving message!\n%s\n", errno_msg);
            return -1;
        }
        if (recvd == 0) {
            return 0;
        }
        buffer += recvd;
        len -= recvd;
    }
    return 1;
}

int read_len(uint32_t* len)
{
    int ret = read_raw(len, sizeof(uint32_t));
    if (ret == 1) {
        // convert fron network byte-ordering
        *len = ntohl(*len);
    }
    return ret;
}

// things calling this should free return value if not NULL
char* sockets_recv(void) {
    uint32_t len = 0;

    // we look for a header that is 4 bytes big, which says how big the
    //  rest of the data will be
    if (read_len(&len) <= 0) {
        snprintf(errMsg, MAX_ERR_BUF_SIZE, "ERROR: reading message length!\n");
        return NULL;
    }

    // allocate space for the incoming message
    char* ret = (char*) malloc(len+1);
    if (!ret) {
        snprintf(errMsg, MAX_ERR_BUF_SIZE, "ERROR: allocating memory for message!\n");
        return NULL;
    }

    // read the incoming message
    if (read_raw(ret, len) <= 0) {
        free(ret);
        snprintf(errMsg, MAX_ERR_BUF_SIZE, "ERROR: reading message!\n");
        return NULL;
    }

    // set a null terminator
    ret[len] = '\0';
    return ret;
}

const char* sockets_get_err(void)
{
    return errMsg;
}
