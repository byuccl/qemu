#ifndef __SOCKETS_H
#define __SOCKETS_H

/*
 * sockets.h
 */

#include <stdint.h>

int sockets_init(uint16_t hostport, char* hostname);
int sockets_exit(void);
int sockets_send(const void* buf, size_t len);
char* sockets_recv(void);
const char* sockets_get_err(void);


#endif  /* __SOCKETS_H */
