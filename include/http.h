#ifndef __HTTP_H__
#define __HTTP_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "errors.h"
#include "file_io.h"

#define METHOD_GET "GET"
#define METHOD_HEAD "HEAD"
#define METHOD_POST "POST"
#define METHOD_PUT "PUT"
#define METHOD_DELETE "DELETE"
#define METHOD_CONNECT "CONNECT"
#define METHOD_OPTIONS "OPTIONS"
#define METHOD_TRACE "TRACE"

/**
 * Flags for http_response_set_content2()
 */
enum SET_CONTENT_FLAGS {
	CONTENT_SET_CONTENT_TYPE = 1	/**< Automatically set Content-Type */
};

/**
 * HTTP Header
 */
typedef struct {
	char *name;		/**< Header name/key */
	char *value;	/**< Header value */
} http_header;

/**
 * HTTP Request
 */
typedef struct {
	char *method;				/**< Method (e.g. GET, POST, PUT, ...) */
	char *path;					/**< Path (e.g. "/", "index.html", ...) */
	http_header **headers;		/**< List of headers */
	size_t header_count;		/**< Header count */
	int keep_alive;				/**< Is the Connection header value "keep-alive" */
	char *query_str;			/**< Query string */
	char *payload;				/**< Possible payload data */
	size_t payload_len;			/**< Payload data size */

	size_t _header_cap;			/**< Header list capacity ("private") */
} http_request;

/**
 * HTTP Response
 */
typedef struct {
	char *method;				/**< Request method */
	char *fs_path;				/**< Possible requested file absolute filesystem path */
	unsigned int status;		/**< Numeric status code (e.g. 200, 404, 500, ...) */
	http_header **headers;		/**< List of headers */
	size_t header_count;		/**< Header count */
	size_t content_length;		/**< Content length in bytes */
	unsigned char *content;		/**< Response content */
	int keep_alive;				/**< Should the Connection header value be "keep-alive" */
	int no_payload;				/**< Should the response contain payload (0: yes, 1: no) */
	time_t if_mod_since_time;	/**< Timestamp provided by possible If-Modified-Since header */

	size_t _header_cap;			/**< Header list capacity ("private") */
} http_response;

typedef struct {
	char *file_path;			/**< Requested file path in filesystem */
	http_request *request;		/**< HTTP Request */
	http_response *response;	/**< HTTP Response */

	int _sock;					/**< Socket */
} http_context;

/**
 * HTTP status list entry
 */
typedef struct {
	unsigned int status;	/**< Status code */
	char *reason;			/**< Textual reason */
	char *err_msg;			/**< Textual error message for error page */
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

http_request * http_request_create2(char *method, char *path, char *query);

int http_request_add_header(http_request *req, http_header *header);
int http_request_add_header2(http_request *req, char *header_name, char *header_value);

http_header * http_request_get_header(http_request *req, char *header_name);

int http_request_header_exists(http_request *req, char *header_name);

int http_request_remove_header(http_request *req, char *header_name);

void http_request_free(http_request *req);

// HTTP Response ==============================================================
http_response * http_response_create(unsigned int status);

int http_response_add_header(http_response *resp, http_header *header);
int http_response_add_header2(http_response *resp, char *header_name, char *header_value);

http_header * http_response_get_header(http_response *resp, char *header_name);

int http_response_header_exists(http_response *resp, char *header_name);

int http_response_remove_header(http_response *resp, char *header_name);

int http_response_set_content(http_response *resp, unsigned const char *content, size_t content_len);
int http_response_set_content2(http_response *resp, unsigned const char *content, size_t content_len, int flags);

void http_response_free(http_response *resp);

int http_response_get_start_string(http_response *resp, char **out);
int http_response_string(http_response *resp, char **out);
int http_response_serve_file(http_context *ctx);

int send_error_response(http_context *ctx, int status);

#endif