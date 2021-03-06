#include "http_request_parser.h"

/**
 * @brief Parse HTTP request for header lines
 * @details Parses HTTP request and splits headers to lines. Doesn't touch request payload.
 * 
 * @param request Raw request string
 * @param len Length of \p request
 * @param[out] header_lines Pointer to non-allocated memory that will contain pointers to header line strings
 * @param[out] end_pos_out Pointer to the last character after header end
 * @return Count of header lines or < 0 on error
 */
int http_request_parse_header_lines(const char *request, size_t len, char ***header_lines, char **end_pos_out) {
	// Memory for lines
	size_t lines_cap = 1;
	size_t lines_count = 0;
	char **lines = calloc(lines_cap, sizeof(char *));

	// Memory for current line
	size_t line_cap = 100;
	size_t line_pos = 0;
	char *line = calloc(line_cap, sizeof(char));

	char *end_pos = NULL;

	// Parser state
	int status = PARSER_STATUS_CHAR;

	for (size_t i = 0; i < len; i++) {
		// Read line
		if (line_pos+1 > line_cap) {
			// Realloc if we need more space
			line_cap *= 2;
			line = realloc(line, line_cap * sizeof(char));
		}
		line[line_pos] = request[i];
		
		if (request[i] == '\r') status = PARSER_STATUS_LF;
		
		if (request[i] == '\n') {
			if (status == PARSER_STATUS_LF) {
				// End of line
				// Add to lines
				// Fit line
				if (line_pos-1 <= 0) {
					// Empty line
					line = realloc(line, sizeof(char));
					line[0] = '\0';
					line_pos = 0;
					end_pos = (char *)&request[i];
					status = PARSER_STATUS_HEADER_END;
				} else {
					// Normal line ending with \r\n
					line = realloc(line, line_pos * sizeof(char));
					line[line_pos-1] = '\0';
					status = PARSER_STATUS_LINE;
				}
			} else if (status == PARSER_STATUS_CHAR) {
				// Data violates HTTP specs and ends line with just \n
				// We'll manage
				if (line_pos == 0) {
					// Empty line
					line = realloc(line, sizeof(char));
					line[0] = '\0';
					line_pos = 0;
					end_pos = (char *)&request[i];
					status = PARSER_STATUS_HEADER_END;
				} else {
					// Normal line ending with \n
					line = realloc(line, line_pos+1 * sizeof(char));
					line[line_pos] = '\0';
					status = PARSER_STATUS_LINE;
				}
			}
		}

		if (status == PARSER_STATUS_LINE) {
			// If we have whole line
			if (lines_count+1 > lines_cap) {
				// Realloc line array
				lines_cap *= 2;
				lines = realloc(lines, lines_cap * sizeof(char *));
			}
			lines[lines_count++] = strdup(line);

			free(line);
			line = calloc(line_cap, sizeof(char));

			line_pos = 0;
			status = PARSER_STATUS_CHAR;
		
		} else if (status == PARSER_STATUS_HEADER_END) {
			// Headers ended, content begins
			break;
		
		} else {
			line_pos++;
		}
	}

	free(line);

	if (status != PARSER_STATUS_HEADER_END) {
		// It seems that we have no enough data
		// Request more
		split_line_free(lines, lines_count);
		zhttpd_log(LOG_WARN, "Possible request data exhaustion");
		return ERROR_PARSER_GET_MORE_DATA;
	}

	*header_lines = lines;
	if (end_pos_out != NULL) *end_pos_out = end_pos;

	return lines_count;
}

/**
 * @brief Parse lines to array of \ref http_header
 * @details Parses given array of lines to array of \ef http_header
 * 
 * @param lines Pointer to array produced by \ref http_request_parse_header_lines
 * @param line_count Count of lines in \p lines
 * @param[out] out_headers Pointer to non-allocated memory that will contain the array of \ref http_header
 * @return Count of parsed headers or < 0 on error
 */
int http_request_parse_headers(char **lines, size_t line_count, http_header ***out_headers) {

	size_t header_cap = 1;
	size_t header_count = 0;
	http_header **headers = calloc(header_cap, sizeof(http_header *));

	for (size_t i = 0; i < line_count; i++) {

		if (i > 0 && (lines[i][0] == ' ' || lines[i][0] == '\t')) {
			/* The parser has stumbled upon a folded header value
			 * This has been obsoleted and must be responded with 400 Bad Request
			 * For more information, see RFC 7230 Section 3.2.4.
			 */
			zhttpd_log(LOG_WARN, "Request contains folded headers, disallowed");
			return ERROR_PARSER_MALFORMED_REQUEST;
		}

		// Split header line
		char **header_words;
		size_t header_words_count = split_line2(lines[i], strlen(lines[i]), ' ', &header_words, 1);

		if (header_words_count != 2) {
			// Invalid header
			// TODO: Maybe ignore invalid headers?
			zhttpd_log(LOG_WARN, "Invalid request header: %s", lines[i]);
			split_line_free(header_words, header_words_count);
			return ERROR_PARSER_MALFORMED_REQUEST;
		}

		char *header_name = header_words[0];
		char *header_value = header_words[1];

		if (header_name[strlen(header_name)-1] != ':') {
			// Not valid header, header name must end with :
			zhttpd_log(LOG_WARN, "Invalid request header name: \"%s\"", header_words[0]);
			split_line_free(header_words, header_words_count);
			return ERROR_PARSER_MALFORMED_REQUEST;
		}
		header_name[strlen(header_name)-1] = '\0';	// Remove trailing ':'

		http_header *h = http_header_create(header_name, header_value);
		if (header_cap < header_count+1) {
			// Realloc
			header_cap *= 2;
			headers = realloc(headers, header_cap * sizeof(http_header *));
		}
		headers[header_count++] = h;

		split_line_free(header_words, header_words_count);
	}

	*out_headers = headers;
	return header_count;
}

/**
 * @brief HTTP request parser
 * @details Parses raw text and produces \ref http_request
 * 
 * @param request Raw request string
 * @param len Length of \p request
 * @param[out] out Unallocated memory for created request
 * @return 0 if successful, < 0 on error
 */
int http_request_parse(const char *request, size_t len, http_request **out) {

	char *header_end_pos;
	char **lines;
	int lines_count = 0;
	lines_count = http_request_parse_header_lines(request, len, &lines, &header_end_pos);

	if (lines_count < 0) {
		return lines_count;	// Pass the error code
	}

	if (lines_count < 1) {
		// No HTTP status line, possible data exhaustion
		free(lines);
		zhttpd_log(LOG_WARN, "Possible request data exhaustion (no enough lines)");
		return ERROR_PARSER_GET_MORE_DATA;
	}

	// We have now parsed first lines of the response containing (hopefully)
	// status line and headers. Checking the status line is the second step.
	
	// Split the status line
	char **words;
	int word_count = split_line(lines[0], strlen(lines[0]), ' ', &words);
	if (word_count != 3) {
		// Malformed request
		zhttpd_log(LOG_WARN, "Malformed request, status line size wrong");
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_MALFORMED_REQUEST;
	}

	char *method = words[0];
	char *path = words[1];
	char *protocol = words[2];

	// First the request must contain the method

	// Check if the given method is valid
	if (strcmp(method, METHOD_GET) != 0 && strcmp(method, METHOD_HEAD) != 0 && strcmp(method, METHOD_POST) != 0 && strcmp(method, METHOD_PUT) != 0 &&
		strcmp(method, METHOD_DELETE) != 0 && strcmp(method, METHOD_CONNECT) != 0 && strcmp(method, METHOD_OPTIONS) != 0 && strcmp(method, METHOD_TRACE) != 0) {
		// Not valid method
		zhttpd_log(LOG_WARN, "Invalid request method %s", method);
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_INVALID_METHOD;
	}

	// TODO: Check & decode path
	if (strlen(path) > 8000) {
		// 414 URI Too Long
		// return ERROR_PARSER_URI_TOO_LONG;
		zhttpd_log(LOG_WARN, "Request URI too long: %d characters, max: 8000", strlen(path));
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_URI_TOO_LONG;
	}

	// Check HTTP version
	if (strcmp(protocol, "HTTP/1.1") != 0) {
		// Not supported protocol/protocol version
		zhttpd_log(LOG_WARN, "Request has unsupported protocol %s", protocol);
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_UNSUPPORTED_PROTOCOL;
	}

	// Extract possible query string
	char *query_str = NULL;
	int query_str_len = 0;

	char *query_start = strstr(path, "?");
	if (query_start != NULL) {
		zhttpd_log(LOG_DEBUG, "Request contains a query string");
		// TODO: Do this without pointer arithmetic?
		query_start += sizeof(char);	// Advance one char
		query_str_len = url_decode(query_start, strlen(query_start), &query_str);
		if (query_str_len < 0) {
			// url_decode failed
			zhttpd_log(LOG_ERROR, "URL query string decoding failed!");
			split_line_free(words, word_count);
			split_line_free(lines, lines_count);
			return ERROR_PARSER_MALFORMED_REQUEST;	// TODO: Return better error depending on return value
		} else {
			// Decoding successful
			*(query_start-sizeof(char)) = '\0';	// Replace '?' with '\0' to end path string here
			// We won't realloc path here, because it would mess up the split_line memory management
		}
	}

	// Create http_request
	http_request *req = http_request_create2(method, path, query_str);

	if (query_str != NULL) free(query_str);	// Because http_request_create2 uses strdup

	split_line_free(words, word_count);

	// Parse headers
	size_t header_count = lines_count - 1;
	if (header_count == 0) {
		// No headers, malformed request
		zhttpd_log(LOG_WARN, "Request contains no headers");
		split_line_free(lines, lines_count);
		http_request_free(req);
		return ERROR_PARSER_MALFORMED_REQUEST;
	}

	http_header **headers;
	int got_header_count = http_request_parse_headers(&lines[1], header_count, &headers);
	if (got_header_count < 0) {
		split_line_free(lines, lines_count);
		http_request_free(req);
		return got_header_count;
	}

	// Check for Host header
	int got_host_header = 0;
	for (size_t i = 0; i < header_count; i++) {
		http_header *h = headers[i];
		http_request_add_header(req, h);
		if (strcmp(h->name, "Host") == 0) {
			got_host_header = 1;
		}
		http_header_free(h);
	}
	free(headers);

	// Cleanup
	split_line_free(lines, lines_count);

	// Check that the request has all necessary headers
	if (got_host_header == 0) {
		// HTTP 1.1 requires Host header
		zhttpd_log(LOG_WARN, "Request is missing Host header");
		http_request_free(req);
		return ERROR_PARSER_NO_HOST_HEADER;
	}
	
	// TODO: Check if there's leftover data

	// Parse possible payload (in POST, etc.)
	int data_len = (int)(&request[len] - header_end_pos)-1;
	if (data_len > 0) {
		// header_end_pos points to '\r' or '\n', advance by one to get to the next
		header_end_pos += 1;
		zhttpd_log(LOG_DEBUG, "Request has leftover data (%d bytes)", data_len);

		if (strcmp(req->method, METHOD_POST) == 0) {
			// POST, get data
			int is_form_urlencoded = 0;
			for (size_t i = 0; i < req->header_count; i++) {
				http_header *h = req->headers[i];
				if (strcmp(h->name, "Content-Type") == 0) {
					char *v = string_to_lowercase(h->value);
					if (strcmp(v, "application/x-www-form-urlencoded") == 0) {
						is_form_urlencoded = 1;
						free(v);
						break;
					}
					free(v);
				}
			}
			if (is_form_urlencoded == 0) {
				// Other form encodings not supported at this moment
				*out = req;
				return ERROR_PARSER_UNSUPPORTED_FORM_ENCODING;
			}
			// Decode data
			char *decoded_data;
			int decoded_data_len = url_decode(header_end_pos, data_len, &decoded_data);
			if (decoded_data_len < 0) {
				zhttpd_log(LOG_ERROR, "Decoding supplied form data failed!");
			} else {
				req->payload = decoded_data;
				req->payload_len = decoded_data_len;
			}

		}
	}

	*out = req;

	return 0;
}
