#include "http.h"

/**
 * HTTP status codes and reason strings
 */
http_status_entry status_entries[] = {
	{200, "OK",                    NULL},
	{500, "Internal Server Error", "Unknown server error."},
	{501, "Not Implemented",       "Sorry, the server doesn't know how to handle the request."},
	{400, "Bad Request",           "Received request was malformed."},
	{403, "Forbidden",             "File access forbidden."},
	{404, "Not Found",             "Requested file not found."},
	{405, "Method Not Allowed",    "Request contained unknown method."},
	{408, "Request Time-out",      "No enough data received in a reasonable timeframe."},
	{0, NULL, NULL}	// Guard entry, must be last
};

/**
 * @brief Get HTTP status entry
 * @details Gets corresponding status entry for status code
 *
 * @param status Status code
 * @return Status entry or NULL on error
 */
http_status_entry * http_status_get_entry(unsigned int status) {
	int pos = 0;
	while (1) {
		http_status_entry entry = status_entries[pos];
		if (entry.status == 0 && entry.reason == NULL && entry.err_msg == NULL) {
			break;	// Found last (guard) entry
		}
		if (entry.status == status) {
			// Found correct entry
			return &(status_entries[pos]);
		}
		pos++;
	}
	return NULL;	// Not found
}

/**
 * @brief Create HTTP header
 * @details Creates new \ref http_header with given name and value
 * 
 * @param name Header name
 * @param value Header value
 * 
 * @return New HTTP header or NULL on error
 */
http_header * http_header_create(char *name, char *value) {
	if (name == NULL || value == NULL) return NULL;

	http_header *h = calloc(1, sizeof(http_header));
	h->name = strdup(name);
	h->value = strdup(value);

	return h;
}

/**
 * @brief Free HTTP header
 * @details Frees \ref http_header
 * 
 * @param header HTTP header to free
 */
void http_header_free(http_header *header) {
	free(header->name);
	free(header->value);
	free(header);
}

/**
 * @brief Create new HTTP request
 * @details Creates new \ref http_request
 * @return New \ref http_request or NULL on error
 */
http_request * http_request_create() {
	http_request *req = calloc(1, sizeof(http_request));
	if (req == NULL) return NULL;
	req->method = NULL;
	req->path = NULL;
	req->query_str = NULL;
	req->payload = NULL;
	req->header_count = 0;
	req->payload_len = 0;
	req->_header_cap = 1;
	req->headers = calloc(req->_header_cap, sizeof(http_header*));
	if (req->headers == NULL) {
		free(req);
		return NULL;
	}

	return req;
}

/**
 * @brief Create new HTTP request
 * @details Creates new \ref http_request with method and path
 * 
 * @param method Request method (e.g. GET, POST, PUT, ...)
 * @param path Request path (e.g. "/", "/foo/bar.html")
 * @param query Request query string (e.g. "foo=bar&val=baz&c=3")
 * 
 * @return New \ref http_request or NULL on error
 */
http_request * http_request_create2(char *method, char *path, char *query) {
	if (method == NULL || path == NULL) return NULL;
	
	http_request *req = calloc(1, sizeof(http_request));
	req->method = strdup(method);
	req->path = strdup(path);
	if (query != NULL) {
		req->query_str = strdup(query);
	} else {
		req->query_str = NULL;
	}
	req->payload = NULL;
	req->header_count = 0;
	req->payload_len = 0;
	req->_header_cap = 1;
	req->headers = calloc(req->_header_cap, sizeof(http_header*));

	return req;
}

/**
 * @brief Add header to HTTP request
 * @details Adds given \ref http_header to the given \ref http_request
 * 
 * @param req Request to use
 * @param header Header to add
 * 
 * @return 0 on success, < 0 on error
 */
int http_request_add_header(http_request *req, http_header *header) {
	// Copy header
	http_header *new_header = http_header_create(header->name, header->value);
	if (new_header == NULL) {
		return ERROR_HEADER_CREATE_FAILED;
	}
	if (req->header_count + 1 > req->_header_cap) {
		// Realloc headers
		req->_header_cap *= 2;
		req->headers = realloc(req->headers, req->_header_cap * sizeof(http_header*));
	}
	req->headers[req->header_count++] = new_header;

	return 0;
}

/**
 * @brief Add header to HTTP request
 * @details Adds a new \ref http_header to the given \ref http_request
 * 
 * @param req Request to use
 * @param header_name New header name
 * @param header_value New header value
 * @return 0 on success, < 0 on error
 */
int http_request_add_header2(http_request *req, char *header_name, char *header_value) {
	http_header *header = http_header_create(header_name, header_value);
	if (header == NULL) return ERROR_HEADER_CREATE_FAILED;
	// This wastes memory (because http_request_add_header copies the http_header), but works for now
	// TODO: Redo
	int ret = http_request_add_header(req, header);
	http_header_free(header);
	return ret;
}

/**
 * @brief Check if request contains header
 * @details Checks existence of header with given name
 * 
 * @param req Request to use
 * @param header_name Header name
 * 
 * @return 1 if header exists, 0 otherwise
 */
int http_request_header_exists(http_request *req, char *header_name) {
	char *name = string_to_lowercase(header_name);
	for (size_t i = 0; i < req->header_count; i++) {
		http_header *h = req->headers[i];
		char *cur_name = string_to_lowercase(h->name);
		if (strcmp(cur_name, name) == 0) {
			free(cur_name);
			free(name);
			return 1;	// True
		}
		free(cur_name);
	}
	free(name);
	return 0;	// False
}

/**
 * @brief Remove header(s) by name from HTTP request
 * @details Removes headers by name from the given \ref http_request
 * 
 * @param req Request to use
 * @param header_name Name of the header to remove
 * 
 * @return Count of removed headers or < 0 on error
 */
int http_request_remove_header(http_request *req, char *header_name) {
	// Create new header list
	http_header **list = calloc(req->_header_cap, sizeof(http_header*));	// TODO: Decrement _header_cap if needed to save memory
	// Go through the header list and find header with given name
	int found_count = 0;
	size_t a = 0;
	for (size_t i = 0; i < req->header_count; i++) {
		http_header *h = req->headers[i];
		if (strcmp(h->name, header_name) == 0) {
			// Match!
			http_header_free(h);
			found_count++;
		} else {
			// Move the pointer to the list
			list[a++] = h;
		}
	}
	if (found_count > 0) {
		// Free old list
		free(req->headers);
		req->header_count = a;
		// Change reponse headers to point to the created list
		req->headers = list;
		return found_count;
	} else {
		// Not found, free created list
		free(list);
		return -1;
	}
}

/**
 * @brief Free HTTP request
 * @details Frees \ref http_request
 * 
 * @param req Request to free
 */
void http_request_free(http_request *req) {
	if (req == NULL) return;
	if (req->method != NULL) free(req->method);
	if (req->path != NULL) free(req->path);
	if (req->query_str != NULL) free(req->query_str);
	if (req->payload != NULL) free(req->payload);
	// Free headers
	for (size_t i = 0; i < req->header_count; i++) {
		http_header_free(req->headers[i]);
	}
	free(req->headers);
	free(req);
}

/**
 * @brief Create HTTP response
 * @details Creates new \ref http_response with given status
 * 
 * @param status HTTP status code
 * @return New \ref http_response or NULL on error
 */
http_response * http_response_create(unsigned int status) {
	http_response *resp = calloc(1, sizeof(http_response));
	resp->status = status;
	resp->content_length = 0;
	resp->content = NULL;
	resp->_header_cap = 1;
	resp->header_count = 0;
	resp->keep_alive = 0;
	resp->headers = calloc(resp->_header_cap, sizeof(http_header*));

	return resp;
}

/**
 * @brief Add header to HTTP response
 * @details Adds given \ref http_header to the given \ref http_response
 * 
 * @param resp Response to use
 * @param header Header to add
 * 
 * @return 0 on success, < 0 on error
 */
int http_response_add_header(http_response *resp, http_header *header) {
	// Copy header
	http_header *new_header = http_header_create(header->name, header->value);
	if (new_header == NULL) {
		return ERROR_HEADER_CREATE_FAILED;
	}
	if (resp->header_count + 1 > resp->_header_cap) {
		// Realloc headers
		resp->_header_cap *= 2;
		resp->headers = realloc(resp->headers, resp->_header_cap * sizeof(http_header*));
	}
	resp->headers[resp->header_count++] = new_header;

	return 0;
}

/**
 * @brief Add header to HTTP response
 * @details Adds a new \ref http_header to the given \ref http_response
 * 
 * @param resp Response to use
 * @param header_name New header name
 * @param header_value New header value
 * @return 0 on success, < 0 on error
 */
int http_response_add_header2(http_response *resp, char *header_name, char *header_value) {
	http_header *header = http_header_create(header_name, header_value);
	if (header == NULL) return ERROR_HEADER_CREATE_FAILED;
	// This wastes memory (because http_request_add_header copies the http_header), but works for now
	// TODO: Redo
	int ret = http_response_add_header(resp, header);
	http_header_free(header);
	return ret;
}

/**
 * @brief Check if response contains header
 * @details Checks existence of header with given name
 * 
 * @param resp Response to use
 * @param header_name Header name
 * 
 * @return 1 if header exists, 0 otherwise
 */
int http_response_header_exists(http_response *resp, char *header_name) {
	char *name = string_to_lowercase(header_name);
	for (size_t i = 0; i < resp->header_count; i++) {
		http_header *h = resp->headers[i];
		char *cur_name = string_to_lowercase(h->name);
		if (strcmp(cur_name, name) == 0) {
			free(cur_name);
			free(name);
			return 1;	// True
		}
		free(cur_name);
	}
	free(name);
	return 0;	// False
}

/**
 * @brief Remove header(s) by name from HTTP response
 * @details Removes headers by name from the given \ref http_response
 * 
 * @param resp Response to use
 * @param header_name Name of the header to remove
 * 
 * @return Count of removed headers or < 0 on error
 */
int http_response_remove_header(http_response *resp, char *header_name) {
	// Create new header list
	http_header **list = calloc(resp->_header_cap, sizeof(http_header*));	// TODO: Decrement _header_cap if needed to save memory
	// Go through the header list and find header with given name
	int found_count = 0;
	size_t a = 0;
	for (size_t i = 0; i < resp->header_count; i++) {
		http_header *h = resp->headers[i];
		if (strcmp(h->name, header_name) == 0) {
			// Match!
			http_header_free(h);
			found_count++;
		} else {
			// Move the pointer to the list
			list[a++] = h;
		}
	}
	if (found_count > 0) {
		// Free old list
		free(resp->headers);
		resp->header_count = a;
		// Change reponse headers to point to the created list
		resp->headers = list;
		return found_count;
	} else {
		// Not found, free created list
		free(list);
		return -1;
	}
}

/**
 * @brief Set HTTP response content
 * @details Copies data from \p content to the response content
 * 
 * @param resp Response to use
 * @param content Content to copy
 * @param content_len Size of \p content
 * @param flags #SET_CONTENT_FLAGS
 * @return Content length if successful, < 0 otherwise
 */
int http_response_set_content2(http_response *resp, unsigned const char *content, size_t content_len, int flags) {
	// If response is NULL or content is NULL when content_len isn't 0, return error
	if (resp == NULL || (content == NULL && content_len > 0)) return ERROR_RESPONSE_ARGUMENT;

	// content can be NULL if the given length is 0
	// If the content_len is 0, reset content
	if (content_len == 0) {
		if (resp->content != NULL) free(resp->content);
		resp->content = NULL;
		resp->content_length = 0;
		return 0;
	}
	// Copy to the response content
	resp->content = calloc(content_len, sizeof(char));
	resp->content_length = content_len;

	memcpy(resp->content, content, content_len);

	// Handle flags
	if ((flags & CONTENT_SET_CONTENT_TYPE) == CONTENT_SET_CONTENT_TYPE) {
		// We should also set Content-Type -header
		// Remove header if it already exists
		http_response_remove_header(resp, "Content-Type");

		// Get Content-Type string (which may contain charset)
		char *content_type;
		if (libmagic_get_mimetype(content, content_len, &content_type) < 0) {
			return ERROR_RESPONSE_SET_CONTENT_TYPE_FAILED;
		}
		zhttpd_log(LOG_DEBUG, "Detected Content-Type: %s", content_type);
		if (http_response_add_header2(resp, "Content-Type", content_type) < 0) {
			free(content_type);
			return ERROR_RESPONSE_SET_CONTENT_TYPE_FAILED;
		}
		free(content_type);
	}

	return resp->content_length;
}

/**
 * @brief Set HTTP response content
 * @details Copies data from \p content to the response content.
 *          Actually calls http_response_set_content2() with \p flags set to 0.
 * 
 * @param resp Response to use
 * @param content Content to copy
 * @param content_len Size of \p content
 * @return Content length if successful, < 0 otherwise
 */
int http_response_set_content(http_response *resp, unsigned const char *content, size_t content_len) {
	return http_response_set_content2(resp, content, content_len, 0);
}

/**
 * @brief Free \ref http_response
 * @details Frees \ref http_response and its members.
 * 
 * @param resp Response to free
 */
void http_response_free(http_response *resp) {
	if (resp == NULL) return;
	if (resp->content != NULL) free(resp->content);
	// Free headers
	for (size_t i = 0; i < resp->header_count; i++) {
		http_header_free(resp->headers[i]);
	}
	free(resp->headers);
	free(resp);
}

/**
 * @brief Create response string
 * @details Creates raw response string for sending to the socket
 * 
 * @param resp Response to use
 * @param[out] out Non-allocated pointer where the result will be written
 * 
 * @return Length of \p out or < 0 if an error occurred
 */
int http_response_string(http_response *resp, char **out) {
	if (resp == NULL) return ERROR_RESPONSE_ARGUMENT;
	
	// Handle status code
	int code = resp->status;

	char *err_msg = NULL;
	char *reason = NULL;

	// Get reason string & error message
	http_status_entry *status_entry;
	status_entry = http_status_get_entry(code);
	if (status_entry == NULL) {
		// No entry, create 501 Not Implemented
		code = 501;
		reason = "Not Implemented";
		err_msg = "Sorry, the server doesn't know how to handle the request.";
	} else {
		err_msg = status_entry->err_msg;
		reason = status_entry->reason;
	}

	// On status != 200, add default error response content
	if (code != 200) {
		char *resp_html;
		int c_len = asprintf(&resp_html,
			"<html><head>\n \
			<title>%d %s</title>\n \
			</head></body>\n \
			<h1>%s</h1>\n \
			<p>%s<br />\n</p>\n \
			<hr>\n \
			<address>%s on port %d</address>\r\n</body></html>\n",
			code, reason, reason, err_msg, SERVER_IDENT, LISTEN_PORT
		);
		http_response_set_content(resp, (unsigned char *)resp_html, c_len);
		free(resp_html);

	} else {
		// 200 OK
		reason = "OK";
	}

	// Add Content-Length
	char *len_str = calloc(10, sizeof(char));
	snprintf(len_str, 10, "%d", resp->content_length);
	int cl_r = http_response_add_header2(resp, "Content-Length", len_str);
	free(len_str);
	if (cl_r < 0) return ERROR_RESPONSE_STRING_CREATE_FAILED;

	// Add Server
	if (http_response_add_header2(resp, "Server", SERVER_IDENT) < 0) return ERROR_RESPONSE_STRING_CREATE_FAILED;

	// Add Date
	char *date_str;
	if (current_datetime_string(&date_str) < 0) return ERROR_RESPONSE_STRING_CREATE_FAILED;
	if (http_response_add_header2(resp, "Date", date_str) < 0) return ERROR_RESPONSE_STRING_CREATE_FAILED;
	free(date_str);

	// Add Connection header
	if (resp->keep_alive) {
		if (http_response_add_header2(resp, "Connection", "keep-alive") < 0) return ERROR_RESPONSE_STRING_CREATE_FAILED;
	} else {
		if (http_response_add_header2(resp, "Connection", "close") < 0) return ERROR_RESPONSE_STRING_CREATE_FAILED;
	}

	// Add Content-Type if needed
	if (http_response_header_exists(resp, "Content-Type") == 0) {
		// No header, add
		char *content_type;
		if (libmagic_get_mimetype(resp->content, resp->content_length, &content_type) < 0) {
			return ERROR_RESPONSE_STRING_CREATE_FAILED;
		}
		if (http_response_add_header2(resp, "Content-Type", content_type) < 0) {
			free(content_type);
			return ERROR_RESPONSE_STRING_CREATE_FAILED;
		}
		free(content_type);
	}

	size_t used = 0;
	size_t cap = 512;
	*out = calloc(cap, sizeof(char));
	used += snprintf(*out, cap, "HTTP/1.1 %d %s\r\n", code, reason);	// Status line

	// Add headers
	for (size_t i = 0; i < resp->header_count; i++) {
		http_header *h = resp->headers[i];
		size_t row_len = strlen(h->name) + strlen(h->value) + 5;	// +5: ": " (2), "\r\n" (2), \0 (1)
		while (used + row_len > cap) {
			// Realloc
			cap *= 2;
			*out = realloc(*out, cap * sizeof(char));
		}
		used += snprintf(&((*out)[used]), cap - used, "%s: %s\r\n", h->name, h->value);
	}

	while (used + 2 + resp->content_length > cap) {	// +2: "\r\n"
		// Realloc
		cap *= 2;
		*out = realloc(*out, cap * sizeof(char));
	}
	used += snprintf(&((*out)[used]), cap - used, "\r\n");

	// Message body
	if (resp->content_length > 0 && resp->content != NULL) {
		memcpy(&((*out)[used]), resp->content, resp->content_length);
		used += resp->content_length;
	}
	
	// Realloc to fit
	*out = realloc((*out), (used+1) * sizeof(char));
	(*out)[used] = '\0';	// Null byte

	return used;
}
