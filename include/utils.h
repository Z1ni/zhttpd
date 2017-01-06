#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "errors.h"

#define SERVER_IDENT "zhttpd/0.1-alpha"
#define MAX_EPOLL_EVENTS 64

// Utility ====================================================================
int make_socket_nonblocking(int sockfd);

int current_datetime_string(char **out);
int current_datetime_string2(char **out, const char *format);

int split_line(const char *in, size_t in_len, char delim, char ***out);
int split_line2(const char *in, size_t in_len, char delim, char ***out, int limit);
void split_line_free(char **words, size_t len);

#endif