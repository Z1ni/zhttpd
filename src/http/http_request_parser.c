#include "http_request_parser.h"

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

	// Memory for lines
	size_t lines_cap = 1;
	size_t lines_count = 0;
	char **lines = calloc(lines_cap, sizeof(char *));

	// Memory for current line
	size_t line_cap = 100;
	size_t line_pos = 0;
	char *line = calloc(line_cap, sizeof(char));
	
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
		// The request is malformed or we don't have enough data
		split_line_free(lines, lines_count);
		fprintf(stderr, "Request is malformed or no enough data\n");
		return ERROR_PARSER_MALFORMED_REQUEST;
	}

	if (lines_count < 1) {
		// No HTTP status line, malformed request
		free(lines);
		fprintf(stderr, "Request is malformed or no enough data (no enough lines)\n");
		return ERROR_PARSER_MALFORMED_REQUEST;
	}

	// We have now parsed first lines of the response containing (hopefully)
	// status line and headers. Checking the status line is the second step.
	
	// Split the status line
	char **words;
	int word_count = split_line(lines[0], strlen(lines[0]), ' ', &words);
	if (word_count != 3) {
		// Malformed request
		fprintf(stderr, "Status line contains != 3 words\n");
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_MALFORMED_REQUEST;
	}

	char *method = words[0];
	char *path = words[1];
	char *protocol = words[2];

	// First the request must contain the method

	// Check if the given method is valid
	if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 && strcmp(method, "POST") != 0 && strcmp(method, "PUT") != 0 &&
		strcmp(method, "DELETE") != 0 && strcmp(method, "CONNECT") != 0 && strcmp(method, "OPTIONS") != 0 && strcmp(method, "TRACE") != 0) {
		// Not valid method
		fprintf(stderr, "Invalid method %s\n", method);
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_INVALID_METHOD;
	}

	// TODO: Check & decode path
	if (strlen(path) > 8000) {
		// 414 URI Too Long
		// return ERROR_PARSER_URI_TOO_LONG;
		fprintf(stderr, "URI too long: %d characters, max: 8000\n", strlen(path));
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_URI_TOO_LONG;
	}

	// Check HTTP version
	if (strcmp(protocol, "HTTP/1.1") != 0) {
		// Not supported protocol/protocol version
		fprintf(stderr, "Unsupported protocol %s\n", protocol);
		split_line_free(words, word_count);
		split_line_free(lines, lines_count);
		return ERROR_PARSER_UNSUPPORTED_PROTOCOL;
	}

	// Create http_request
	http_request *req = http_request_create2(method, path);
	
	split_line_free(words, word_count);

	// Parse headers
	size_t header_count = lines_count - 1;
	if (header_count == 0) {
		// No headers, malformed request
		fprintf(stderr, "No headers\n");
		split_line_free(lines, lines_count);
		return ERROR_PARSER_MALFORMED_REQUEST;
	}

	for (size_t i = 1; i < header_count+1; i++) {
		// Split header line
		char **header_words;
		size_t header_words_count = split_line(lines[i], strlen(lines[i]), ' ', &header_words);
		
		char *header_name = header_words[0];

		if (header_name[strlen(header_name)-1] != ':') {
			// Not valid header, header name must end with :
			fprintf(stderr, "Invalid header name: \"%s\"\n", header_words[0]);
			split_line_free(header_words, header_words_count);
			split_line_free(lines, lines_count);
			return ERROR_PARSER_MALFORMED_REQUEST;
		}
		header_name[strlen(header_name)-1] = '\0';	// Remove trailing ':'

		printf("Valid header name: %s\n", header_name);

		split_line_free(header_words, header_words_count);
	}

	// Cleanup
	split_line_free(lines, lines_count);
	
	*out = req;

	return 0;
}