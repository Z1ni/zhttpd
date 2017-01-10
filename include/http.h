#ifndef __HTTP_H__
#define __HTTP_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "errors.h"

#define METHOD_GET "GET"
#define METHOD_POST "POST"

/**
 * \enum Flags for http_response_set_content2()
 */
enum SET_CONTENT_FLAGS {
	CONTENT_SET_CONTENT_TYPE = 1	/**< Automatically set Content-Type */
};

/**
 * \struct HTTP Header
 */
typedef struct {
	char *name;		/**< Header name/key */
	char *value;	/**< Header value */
} http_header;

/**
 * \struct HTTP Request
 */
typedef struct {
	char *method;				/**< Method (e.g. GET, POST, PUT, ...) */
	char *path;					/**< Path (e.g. "/", "index.html", ...) */
	http_header **headers;		/**< List of headers */
	size_t header_count;		/**< Header count */
	int keep_alive;				/**< Is the Connection header value "keep-alive" */

	size_t _header_cap;			/**< Header list capacity ("private") */
} http_request;

/**
 * \struct HTTP Response
 */
typedef struct {
	unsigned int status;	/**< Numeric status code (e.g. 200, 404, 500, ...) */
	http_header **headers;	/**< List of headers */
	size_t header_count;	/**< Header count */
	size_t content_length;	/**< Content length in bytes */
	unsigned char *content;	/**< Response content */
	int keep_alive;			/**< Should the Connection header value be "keep-alive" */

	size_t _header_cap;		/**< Header list capacity ("private") */
} http_response;

/**
 * \struct HTTP status list entry
 */
typedef struct {
	unsigned int status;	/**< Status code */
	char *reason;			/**< Textual reason */
	size_t reason_length;	/**< Length of reason string */
	char *err_msg;			/**< Textual error message for error page */
	size_t err_msg_length;	/**< Length of error message */
} http_status_entry;

/**
 * HTTP status codes and reason strings
 */
extern http_status_entry status_entries[];

// Status entries =============================================================
http_status_entry * http_status_get_entry(unsigned int status);

// HTTP Header ================================================================
http_header * http_header_create(char *name, char *value);
void http_header_free(http_header *header);

// HTTP Request ===============================================================
http_request * http_request_create(void);

http_request * http_request_create2(char *method, char *path);

int http_request_add_header(http_request *req, http_header *header);
int http_request_add_header2(http_request *req, char *header_name, char *header_value);

int http_request_header_exists(http_request *req, char *header_name);

int http_request_remove_header(http_request *req, char *header_name);

void http_request_free(http_request *req);

// HTTP Response ==============================================================
http_response * http_response_create(unsigned int status);

int http_response_add_header(http_response *resp, http_header *header);
int http_response_add_header2(http_response *resp, char *header_name, char *header_value);

int http_response_header_exists(http_response *resp, char *header_name);

int http_response_remove_header(http_response *resp, char *header_name);

int http_response_set_content(http_response *resp, unsigned const char *content, size_t content_len);
int http_response_set_content2(http_response *resp, unsigned const char *content, size_t content_len, int flags);

void http_response_free(http_response *resp);

int http_response_string(http_response *resp, char **out);


#endif