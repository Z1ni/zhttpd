#include "child.h"
#include "utils.h"
#include "http.h"
#include "http_request_parser.h"
#include "file_io.h"
#include "cgi.h"

volatile sig_atomic_t run_child_main_loop = 1;	// True (1) if the main loop should be running

static int sock;					// Socket to use
static int keep_conn_alive = 0;		// True if the connection is set to be kept alive
static time_t keepalive_timer = 0;	// Keepalive timer

static void sigint_handler(int signal) {
	// Parent died or someone wants this process to stop
	zhttpd_log(LOG_WARN, "Child received SIGINT, closing");
	run_child_main_loop = 0;
}

static void reset_keepalive_timer(void) {
	keepalive_timer = time(NULL);
}

/**
 * @brief Handle HTTP request
 * @details Handles given HTTP request and responds to it
 * 
 * @param ctx HTTP Context
 */
static void handle_http_request(http_context *ctx) {

	http_request *req = ctx->request;

	// Check for supported method
	char *m = req->method;
	if (strcmp(m, METHOD_GET) != 0 && strcmp(m, METHOD_POST) != 0 && strcmp(m, METHOD_HEAD) != 0) {
		// Not supported method
		// Send "501 Not Implemented"
		send_error_response(ctx, 501);
		return;
	}

	// Concatenate file path and prevent free filesystem access
	char *final_path;
	int rp_ret = create_real_path(WEBROOT, strlen(WEBROOT), req->path, strlen(req->path), &final_path);
	if (rp_ret < 0) {
		// Invalid path, send "400 Bad Request"
		send_error_response(ctx, 400);

	} else {
		// Valid path
		zhttpd_log(LOG_INFO, "Client requests file: \"%s\"", final_path);

		ctx->file_path = final_path;

		// Get file extension
		char *ext = NULL;
		char *ext_start = strstr(final_path, ".");
		if (ext_start != NULL) {
			ext = (ext_start + sizeof(char));
			zhttpd_log(LOG_DEBUG, "File extension: %s", ext);
		}

		if (ext != NULL && strcmp(ext, "php") == 0) {
			// Run PHP script
			zhttpd_log(LOG_INFO, "File is runnable PHP file!");

			cgi_parameters params = {
				.req = req,
				.script_filename = final_path
			};
			unsigned char *php_out;
			http_header **cgi_headers;
			size_t cgi_header_count;
			int cgi_ret = cgi_exec("/usr/bin/php5-cgi", &params, &php_out, &cgi_headers, &cgi_header_count);

			if (cgi_ret < 0 && cgi_ret != ERROR_CGI_STATUS_NONZERO && cgi_ret != ERROR_CGI_SCRIPT_PATH_INVALID) {
				// Failed
				zhttpd_log(LOG_ERROR, "PHP execution failed!");
				// Send "500 Internal Server Error"
				send_error_response(ctx, 500);

			} else if (cgi_ret == ERROR_CGI_SCRIPT_PATH_INVALID) {
				// Script path invalid, send "404 Not Found"
				send_error_response(ctx, 404);

			} else if (cgi_ret == ERROR_CGI_STATUS_NONZERO) {
				// TODO: Handle non-zero status code
				// For now just send "500 Internal Server Error" instead
				free(php_out);
				send_error_response(ctx, 500);

			} else {
				// Execution successful, send response
				// Set headers
				int flags = CONTENT_SET_CONTENT_TYPE;
				int status_code = -1;

				for (size_t i = 0; i < cgi_header_count; i++) {
					http_header *h = cgi_headers[i];
					char *h_name = string_to_lowercase(h->name);
					if (strcmp(h_name, "content-type") == 0) {
						flags = 0;	// Don't guess Content-Type when it's already provided
					}
					if (strcmp(h_name, "status") == 0) {
						// CGI script wants to set the status code
						// Get status code
						errno = 0;
						status_code = strtol(h->value, NULL, 0);
						if (errno != 0) {
							zhttpd_log(LOG_ERROR, "CGI status header parsing failed!");
							status_code = -1;
						}
					}
					free(h_name);
				}

				http_response *resp = http_response_create((status_code != -1 ? status_code : 200));
				resp->method = strdup(req->method);
				resp->keep_alive = keep_conn_alive;
				resp->fs_path = strdup(final_path);
				if (strcmp(req->method, METHOD_HEAD) == 0) resp->no_payload = 1;	// This is a HEAD response
				// Add headers to response
				for (size_t i = 0; i < cgi_header_count; i++) {
					http_header *h = cgi_headers[i];
					char *h_name = string_to_lowercase(h->name);
					if (strcmp(h_name, "status") != 0) {
						http_response_add_header(resp, h);
					}
					free(h_name);
					http_header_free(h);
				}
				free(cgi_headers);
				// Set content
				http_response_set_content2(resp, php_out, cgi_ret, flags);
				free(php_out);
				char *resp_str;
				int len = http_response_string(resp, &resp_str);
				if (len >= 0) {
					if (sendall(sock, resp_str, len) == -1) {
						zhttpd_log(LOG_ERROR, "Sendall failed!");
						perror("sendall");
					}
					free(resp_str);
				}
				http_response_free(resp);
			}

		} else {

			off_t file_size;
			int size_ret = get_file_size(final_path, &file_size);
			if (size_ret < 0) {
				free(final_path);
				// Error
				if (size_ret == ERROR_FILE_IO_NO_ACCESS) {
					// Respond with "403 Forbidden"
					send_error_response(ctx, 403);

				} else if (size_ret == ERROR_FILE_IO_NO_ENT || size_ret == ERROR_FILE_IS_DIR) {
					// File not found, respond with "404 File Not Found"
					send_error_response(ctx, 404);

				} else if (size_ret == ERROR_FILE_IO_GENERAL) {
					// I/O error, response with "500 Internal Server Error"
					send_error_response(ctx, 500);
				}
				return;
			}

			zhttpd_log(LOG_DEBUG, "File size: %lu bytes", file_size);

			http_response *resp = http_response_create(200);
			resp->method = strdup(req->method);
			resp->keep_alive = keep_conn_alive;
			resp->fs_path = strdup(final_path);
			if (strcmp(req->method, METHOD_HEAD) == 0) resp->no_payload = 1;	// This is a HEAD response

			// Set Content-Type
			// TODO: Check case-insensitively
			if (ext != NULL && (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)) {
				http_response_add_header2(resp, "Content-Type", "text/html");
			} else if (ext != NULL && strcmp(ext, "css") == 0) {
				http_response_add_header2(resp, "Content-Type", "text/css");
			} else {
				// Guess Content-Type
				char *cont_type;
				if (libmagic_get_mimetype2(final_path, &cont_type) == -1) {
					// Failed
					zhttpd_log(LOG_ERROR, "Content-Type guessing failed!");
					http_response_free(resp);
					// Send "500 Internal Server Error"
					send_error_response(ctx, 500);
					return;
				}
				// Set Content-Type
				http_response_add_header2(resp, "Content-Type", cont_type);
				free(cont_type);
			}

			ctx->response = resp;

			http_response_serve_file(ctx);

			http_response_free(resp);
		}

		free(final_path);
	}
}

/**
 * @brief Child process main loop
 * @details Reads data from given socket and handles and responds to requests.
 * 
 * @param in_sock Socket to monitor
 * @param parent_pid Process ID of the parent process
 * @param addr_str Textual representation of client address (IPv4 or IPv6)
 */
void child_main_loop(int in_sock, pid_t parent_pid, const char *addr_str) {

	sock = in_sock;	// Set global socket variable

	zhttpd_log(LOG_INFO, "Child process started to handle the connection");

	// Return SIGCHLD handler to default for the child process
	struct sigaction sigchdl_sigaction = {
		.sa_handler = SIG_DFL
	};
	if (sigaction(SIGCHLD, &sigchdl_sigaction, NULL) == -1) {
		zhttpd_log(LOG_CRIT, "Child SIGCHLD signal handler restoring failed!");
		perror("sigaction");
		abort();
	}

	// Make kernel notify with SIGINT if the parent dies
	if (prctl(PR_SET_PDEATHSIG, SIGINT) == -1) {
		zhttpd_log(LOG_CRIT, "Child prctl failed!");
		perror("prctl");
		abort();
	}
	if (getppid() != parent_pid) {
		// Detect possible race condition, see http://stackoverflow.com/a/36945270
		// TODO: Handle this better?
		zhttpd_log(LOG_CRIT, "Child prctl race condition!");
		abort();
	}

	// Set SIGINT handler
	struct sigaction sigint_sigaction = {
		.sa_handler = sigint_handler
	};
	if (sigaction(SIGINT, &sigint_sigaction, NULL) == -1) {
		zhttpd_log(LOG_CRIT, "Child SIGINT signal handler registering failed!");
		perror("sigaction");
		abort();
	}

	// Get current time for recv timeout
	time_t recv_start = time(NULL);

	// Make given socket nonblocking
	if (make_socket_nonblocking(sock) == -1) abort();

	struct epoll_event event = {0};
	struct epoll_event *events = calloc(MAX_EPOLL_EVENTS, sizeof(event));
	int efd = epoll_create1(0);
	if (efd == -1) {
		zhttpd_log(LOG_CRIT, "Epoll init failed!");
		perror("child epoll_create1");
		abort();
	}

	event.data.fd = sock;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event) == -1) {
		zhttpd_log(LOG_CRIT, "Epoll control failed!");
		perror("child epoll_ctl");
		abort();
	}

	int handled = 0;			// False
	int request_num = 1;
	int recv_timer_started = 1;	// True

	// Main event loop
	zhttpd_log(LOG_DEBUG, "Child event loop starting");

	unsigned int buf_size = 1024;
	unsigned int got_bytes = 0;
	unsigned int recv_buf_size = buf_size;

	char *received = calloc(recv_buf_size, sizeof(char));

	// Create HTTP Context
	http_context context;
	memset(&context, 0, sizeof(http_context));
	context._sock = sock;

	while (run_child_main_loop) {

		int n = epoll_wait(efd, events, MAX_EPOLL_EVENTS, 0);
		for (int i = 0; i < n; i++) {

			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
				// Error
				zhttpd_log(LOG_ERROR, "Child Epoll wait failed!");
				close(events[i].data.fd);
				run_child_main_loop = 0;	// Stop main child loop
				break;
			
			} else {
				// We have data to be read!
				zhttpd_log(LOG_DEBUG, "Incoming data");
				int done = 0;
				char *buf = NULL;

				// Start recv timer if this isn't the first request
				if (keep_conn_alive && !recv_timer_started) {
					recv_start = time(NULL);
					recv_timer_started = 1;
				}

				while (1) {
					ssize_t count = 0;

					buf = calloc(buf_size, sizeof(char));
					count = read(events[i].data.fd, buf, buf_size);

					if (count == -1) {
						// Error
						if (errno != EAGAIN) {
							zhttpd_log(LOG_ERROR, "Data reading failed!");
							perror("child read");
							done = 1;
						}
						break;
					} else if (count == 0) {
						// Remote closed
						zhttpd_log(LOG_INFO, "Remote end closed the connection");
						done = 1;
						break;
					}

					// Handle received data
					// Check if the received data fits into the final buffer
					while (recv_buf_size < got_bytes + count) {
						// Doesn't fit, resize buffer
						recv_buf_size *= 2;
						received = realloc(received, recv_buf_size * sizeof(char));
					}
					// Copy received data to the final buffer
					memcpy(&received[got_bytes], buf, count);
					free(buf);	// Free temp recv buffer
					buf = NULL;
					got_bytes += count;
				}
				if (buf != NULL) free(buf);

				// Add zero byte
				// Resize the final recv data to fit
				received = realloc(received, (got_bytes + 1) * sizeof(char));
				recv_buf_size = got_bytes+1;
				received[got_bytes] = '\0';

				if (done) {
					close(events[i].data.fd);
					run_child_main_loop = 0;	// Stop the main child loop
				}

				// Receiving ends
				// Handle final received data here ================================================

				reset_keepalive_timer();

				http_request *req;
				int ret = http_request_parse(received, got_bytes, &req);
				if (ret < 0) {
					// Request parsing failed, do something about that

					if (ret == ERROR_PARSER_GET_MORE_DATA) {
						// Need more data
						zhttpd_log(LOG_DEBUG, "Need more data to parse the request");
						continue;
					} else {
						zhttpd_log(LOG_ERROR, "Request parsing failed with error code %d", ret);
						if (ret == ERROR_PARSER_MALFORMED_REQUEST || ret == ERROR_PARSER_NO_HOST_HEADER) {
							// Malformed request or HTTP/1.1 request without Host header
							send_error_response(&context, 400);

						} else if (ret == ERROR_PARSER_INVALID_METHOD) {
							// Unsupported method
							send_error_response(&context, 405);

						} else if (ret == ERROR_PARSER_UNSUPPORTED_FORM_ENCODING) {
							// Unsupported form encoding
							http_header *cont_type_h = http_request_get_header(req, "Content-Type");
							char *form_encoding = cont_type_h->value;
							zhttpd_log(LOG_WARN, "Request is using unsupported form encoding \"%s\"!", form_encoding);
							http_request_free(req);	// Request is still set in this case
							// Respond with "501 Not Implemented" for now
							send_error_response(&context, 501);
						}
					}

				} else {
					// Request parsing successful!
					context.request = req;	// Set context request

					zhttpd_log(LOG_DEBUG, "New HTTP request (No. %d):", request_num);
					zhttpd_log(LOG_DEBUG, "  Method: %s", req->method);
					zhttpd_log(LOG_DEBUG, "  Path: %s", req->path);
					if (req->query_str != NULL) zhttpd_log(LOG_DEBUG, "  Query: %s", req->query_str);
					zhttpd_log(LOG_DEBUG, "  %u header(s):", req->header_count);
					for (size_t i = 0; i < req->header_count; i++) {
						http_header *h = req->headers[i];
						zhttpd_log(LOG_DEBUG, "    %s: \"%s\"", h->name, h->value);
					}

					// Search for Connection header to possibly set keep-alive
					http_header *conn_h = http_request_get_header(req, "Connection");
					if (conn_h != NULL) {
						char *value = string_to_lowercase(conn_h->value);
						if (strcmp(value, "keep-alive") == 0) {
							keep_conn_alive = 1;	// Set to true
							req->keep_alive = 1;
						}
						free(value);
						if (keep_conn_alive) {
							zhttpd_log(LOG_DEBUG, "Client wants to keep connection alive");
							reset_keepalive_timer();
						}
					}

					// Handle the request and respond to it
					handle_http_request(&context);

					// We're done with the request, free it
					http_request_free(req);
				}

				// Handling the data ends =========================================================
				free(received);
				received = NULL;
				got_bytes = 0;
				zhttpd_log(LOG_DEBUG, "Received data handled");

				handled = 1;
				if (keep_conn_alive) {
					zhttpd_log(LOG_DEBUG, "Starting keepalive timer");
					reset_keepalive_timer();
					request_num++;
					recv_buf_size = buf_size;
					received = calloc(recv_buf_size, sizeof(char));
				} else {
					run_child_main_loop = 0;
				}
			}
		}
		usleep(5000);

		if (handled == 0 && time(NULL) - recv_start >= REQUEST_TIMEOUT_SECONDS) {
			// Receive timeout
			// Send "408 Request Timeout"
			zhttpd_log(LOG_INFO, "Client request timeout");

			send_error_response(&context, 408);
			run_child_main_loop = 0;
		}

		if (keep_conn_alive && time(NULL) - keepalive_timer >= REQUEST_KEEPALIVE_TIMEOUT_SECONDS) {
			// Keep-alive timeout
			// Just close connection for now
			/* TODO: Have some kind of "processing" variable that indicates if
			 *       data processing is in progress and reset the timer in that case.
			 */
			zhttpd_log(LOG_INFO, "Client connection keep-alive timeout");

			run_child_main_loop = 0;
		}

	}
	zhttpd_log(LOG_INFO, "Child request handler process closing");

	if (received != NULL) free(received);
	free(events);

	// Close socket
	shutdown(sock, SHUT_RDWR);
	close(sock);

	// We're done!
}