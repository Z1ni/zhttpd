#include "cgi.h"

static int parse_headers(const char *in, size_t in_len, http_header ***out_headers, char **out_end_pos) {

	char **header_lines;
	char *end_pos;
	int header_lines_count = 0;
	header_lines_count = http_request_parse_header_lines(in, in_len, &header_lines, &end_pos);
	if (header_lines_count < 0) {
		return header_lines_count;
	}

	http_header **headers;
	int header_count = 0;
	header_count = http_request_parse_headers(header_lines, header_lines_count, &headers);
	if (header_count < 0) {
		split_line_free(header_lines, header_lines_count);
		return header_count;
	}

	split_line_free(header_lines, header_lines_count);

	*out_headers = headers;
	*out_end_pos = end_pos;
	return header_count;
}

/**
 * @brief Execute CGI program
 * @details Executes CGI program and returns its output.
 *          NOTE: \p out is allocated also when the function returns ERROR_CGI_STATUS_NONZERO.
 *          Parts of this code are from https://jineshkj.wordpress.com/2006/12/22/how-to-capture-stdin-stdout-and-stderr-of-child-program/
 * 
 * @param path Path to the program
 * @param params CGI parameters
 * @param[out] out Pointer to non-allocated memory where the result will be stored
 * @param[out] out_headers Pointer to non-allocated memory where the headers set by the CGI program will be stored
 * @param[out] out_header_count Will contain count of \p out_headers
 * @return Length of \p out or < 0 on error
 */
int cgi_exec(const char *path, cgi_parameters *params, unsigned char **out, http_header ***out_headers, size_t *out_header_count) {

	// TODO: Provide parameters in cgi_parameters
	// TODO: Check if path points to existing file

	// Convert numeric port to string
	char port_str[6] = {0};
	snprintf(port_str, 6, "%d", LISTEN_PORT);

	// Setup environment variables
	setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
	setenv("SCRIPT_FILENAME", params->script_filename, 1);
	setenv("SCRIPT_NAME", params->req->path, 1);
	setenv("DOCUMENT_ROOT", WEBROOT, 1);
	if (params->req->query_str != NULL) {
		// Set QUERY_STRING if it's provided
		setenv("QUERY_STRING", params->req->query_str, 1);
	}
	setenv("REQUEST_METHOD", params->req->method, 1);
	setenv("SERVER_SOFTWARE", SERVER_IDENT, 1);
	setenv("SERVER_PORT", port_str, 1);
	setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
	/* This needs to be set if PHP has cgi.force_redirect enabled.
	 * This is to prevent directly executing PHP code if user knows the path.
	 * Supports really only Apache, but we'll pretend.
	 * See http://php.net/manual/en/security.cgi-bin.force-redirect.php
	 */
	setenv("REDIRECT_STATUS", "true", 1);

	// Set HTTP headers as environment variables starting with "HTTP_"
	for (size_t i = 0; i < params->req->header_count; i++) {
		http_header *h = params->req->headers[i];
		char *name_upper = string_to_uppercase(h->name);
		char *env_name = calloc(strlen(name_upper) + 6, sizeof(char));
		snprintf(env_name, strlen(name_upper)+6, "HTTP_%s", name_upper);
		free(name_upper);
		// Replace '-' with '_'
		char *dash_p = NULL;
		while ((dash_p = strstr(env_name, "-")) != NULL) {
			*dash_p = '_';
		}
		setenv(env_name, h->value, 1);
		free(env_name);
	}

	/* Fork CGI program
	 * We can't use popen, because it supports only one-way pipes
	 * (so no writing and reading at the same time)
	 */

	// TODO: Process execution timeout (e.g. 60 seconds)

	int pipes[2][2];

	int status = 0;
	unsigned char *output = NULL;
	size_t out_pos = 0;
	
	int pipe1_s = pipe(pipes[PARENT_READ_PIPE]);
	int pipe2_s = pipe(pipes[PARENT_WRITE_PIPE]);
	if (pipe1_s == -1 || pipe2_s == -1) {
		zhttpd_log(LOG_ERROR, "CGI pipe creation failed!");
		perror("pipe");
		return ERROR_CGI_EXEC_FAILED;
	}
	pid_t pid = fork();

	if (pid == 0) {
		// Child
		// Duplicate file descriptors
		// TODO: Check for errors
		dup2(CHILD_READ_FD, STDIN_FILENO);
		dup2(CHILD_WRITE_FD, STDOUT_FILENO);
		dup2(CHILD_WRITE_FD, STDERR_FILENO);

		close(CHILD_READ_FD);
		close(CHILD_WRITE_FD);
		close(PARENT_READ_FD);
		close(PARENT_WRITE_FD);

		char *argv[] = {
			(char *)path, NULL
		};

		if (execv(argv[0], argv) == -1) {
			zhttpd_log(LOG_ERROR, "CGI execl failed!");
			perror("execl");
		}
		exit(1);
	} else if (pid < 0) {
		// Forking failed
		zhttpd_log(LOG_ERROR, "CGI fork failed!");
		perror("fork");
		return ERROR_CGI_EXEC_FAILED;
	} else {
		// Parent

		close(CHILD_READ_FD);
		close(CHILD_WRITE_FD);

		// TODO: Write possible (POST) parameters here
		// write(PARENT_WRITE_FD, buf, buf_len);

		// Read stdout / stderr
		char buf[2048] = {0};
		int read_bytes = 0;
		out_pos = 0;
		size_t out_cap = 2048;
		output = calloc(out_cap, sizeof(char));
		
		errno = 0;
		while ((read_bytes = read(PARENT_READ_FD, buf, sizeof(buf))) > 0) {
			while (out_cap < out_pos + read_bytes) {
				out_cap *= 2;
				output = realloc(output, out_cap * sizeof(char));
			}
			memcpy(&output[out_pos], buf, read_bytes);
			out_pos += read_bytes;
		}
		close(PARENT_READ_FD);
		close(PARENT_WRITE_FD);

		if (errno != 0) {
			// Read failed
			zhttpd_log(LOG_ERROR, "CGI program output read failed!");
			perror("read");
			free(output);
			return ERROR_CGI_EXEC_FAILED;
		}
		output = realloc(output, (out_pos+1) * sizeof(char));
		output[out_pos] = '\0';

		zhttpd_log(LOG_DEBUG, "CGI program outputted %d bytes", out_pos);

		// Output read, wait for program exit (probably has already)
		while (!WIFEXITED(status) && !WIFSIGNALED(status)) {
			waitpid(pid, &status, WUNTRACED);
		}
		zhttpd_log(LOG_INFO, "CGI program exited with status code %d", status);
	}

	if (output == NULL) return ERROR_CGI_EXEC_FAILED;	// CGI program must output something

	// Parse headers
	http_header **headers;
	char *end_pos;
	int header_count = parse_headers((char *)output, out_pos, &headers, &end_pos);
	if (header_count < 0) {
		zhttpd_log(LOG_ERROR, "CGI response HTTP header parsing failed!");
		free(output);
		return header_count;	// Pass the error code
	}

	// Calculate new content length without headers
	int content_length = (int)(&(output[out_pos]) - (unsigned char *)end_pos+1)-2;
	// Ignore headers and copy response
	unsigned char *new_out = calloc(content_length, sizeof(char));
	memcpy(new_out, (unsigned char *)(end_pos+1), content_length);
	free(output);
	output = new_out;

	// Just some logging
	zhttpd_log(LOG_DEBUG, "CGI response contains %d header(s):", header_count);
	for (size_t i = 0; i < header_count; i++) {
		http_header *h = headers[i];
		zhttpd_log(LOG_DEBUG, "  - %s: \"%s\"", h->name, h->value);
	}

	*out_headers = headers;
	*out_header_count = header_count;
	*out = output;

	// CGI program has exited
	if (status != 0) {
		// Return output, but with a different error code
		return ERROR_CGI_STATUS_NONZERO;
	}
	return content_length;
}