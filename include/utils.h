#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>

#include <magic.h>

#include "errors.h"

#define SERVER_IDENT "zhttpd/0.1-alpha"
#define LISTEN_PORT 8080
#define MAX_EPOLL_EVENTS 64
#define REQUEST_TIMEOUT_SECONDS 60	// For testing, normal value should be something like 10
#define REQUEST_KEEPALIVE_TIMEOUT_SECONDS 10
#define WEBROOT "/var/www-zhttpd/"

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define COLOR_LOG_OUTPUT	/**< If defined, log output will be colored */

// Utility ====================================================================

/**
 * \enum Log levels
 */
typedef enum {
	LOG_CRIT,	/**< Critical, program can't recover */
	LOG_ERROR,	/**< Error, program can recover */
	LOG_WARN,	/**< Warning, higher priority notification */
	LOG_INFO,	/**< Information, status messages etc. */
	LOG_DEBUG	/**< Debug, detailed debugging information */
} LOG_LEVEL;

#define DEBUG_MIN_LEVEL LOG_DEBUG	/**< Minimum level to show in logs */

void zhttpd_log(LOG_LEVEL level, const char *format, ...);

int make_socket_nonblocking(int sockfd);

int current_datetime_string(char **out);
int current_datetime_string2(char **out, const char *format);

int split_line(const char *in, size_t in_len, char delim, char ***out);
int split_line2(const char *in, size_t in_len, char delim, char ***out, int limit);
void split_line_free(char **words, size_t len);

char * string_to_lowercase(char *str);

int create_real_path(const char *webroot, size_t webroot_len, const char *path, size_t path_len, char **out);

int libmagic_get_mimetype(const unsigned char *buf, size_t buf_len, char **out);

int url_decode(const char *in, size_t in_len, char **out);
int url_encode(const char *in, size_t in_len, char **out);

#endif