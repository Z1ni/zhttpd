#include "utils.h"

/**
 * @brief Make socket non-blocking
 * @details Makes socket non-blocking
 * 
 * @param sockfd Socket file descriptor
 * @return 0 if successful, < 0 on error
 */
int make_socket_nonblocking(int sockfd) {
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1) {
		perror("make_socket_nonblocking F_GETFL");
		return -1;
	}

	flags |= O_NONBLOCK;
	if (fcntl(sockfd, F_SETFL, flags) == -1) {
		perror("make_socket_nonblocking F_SETFL");
		return -1;
	}
	return 0;
}

/**
 * @brief Get current date/time string
 * @details Produces string with given strftime() format.
 *          Caller must free the string after use.
 * 
 * @param[out] str Non-allocated pointer where the result will be written
 * @param format strftime() format string
 * 
 * @return Length of \p str or < 0 on error
 */
int current_datetime_string2(char **str, const char *format) {
	if (format == NULL) return -1;
	time_t now = time(NULL);
	struct tm *now_gmt = gmtime(&now);
	int max_size = strlen(format) * 2;
	*str = calloc(max_size, sizeof(char));
	int len = strftime((*str), max_size-1, format, now_gmt);
	(*str) = realloc((*str), (len+1) * sizeof(char));
	(*str)[len] = '\0';
	return len;
}

/**
 * @brief Get current date/time string
 * @details Produces Date-header formatted date string.
 *          Caller must free the string after use.
 * 
 * @param[out] str Non-allocated pointer where the result will be written
 * @return Length of \p str or < 0 on error
 */
int current_datetime_string(char **str) {
	return current_datetime_string2(str, "%a, %d %b %Y %H:%M:%S %Z");
}
