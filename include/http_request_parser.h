#ifndef __HTTP_REQUEST_PARSER_H__
#define __HTTP_REQUEST_PARSER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "http.h"

/**
 * \enum HTTP Parser status
 */
typedef enum {
	PARSER_STATUS_CHAR,			/**< Current position contains character */
	PARSER_STATUS_CR,			/**< Current position contains Carriage Return */
	PARSER_STATUS_LF,			/**< Current position contains Line Feed */
	PARSER_STATUS_LINE,			/**< One line complete */
	PARSER_STATUS_HEADER_END	/**< Headers read */
} PARSER_STATUS;

int http_request_parse(const char *request, size_t len, http_request **out);

#endif