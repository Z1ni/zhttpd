#include "child.h"
#include "utils.h"
#include "http.h"
#include "http_request_parser.h"
#include "file_io.h"
#include "cgi.h"

volatile sig_atomic_t run_child_main_loop = 1;

static void sigint_handler(int signal) {
	// Parent died or someone wants this process to stop
	zhttpd_log(LOG_WARN, "Child received SIGINT, closing");
	run_child_main_loop = 0;
}

// Basically copied from http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html#sendall
static int sendall(int s, char *buf, int len) {
	int total = 0;
	int bytes_left = len;
	int n;

	while (total < len) {
		n = send(s, buf+total, bytes_left, 0);
		if (n == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
			// Send failed
			break;
		}
		total += n;
		bytes_left -= n;
		usleep(100);
	}

	return n == -1 ? -1 : total;
}

/**
 * @brief Send HTTP response with given status code
 * @details Sends HTTP response with given non-OK (200) status code
 * 
 * @param req Request
 * @param sock Socket
 * @param status HTTP status code
 * @return Sent byte count on success or < 0 on error
 */
static int send_error_response(http_request *req, int sock, int status) {
	http_response *resp = http_response_create(status);
	if (strcmp(req->method, "HEAD") == 0) resp->head_response = 1;
	if (req != NULL) {
		resp->keep_alive = req->keep_alive;
	} else {
		resp->keep_alive = 0;
	}
	char *resp_str;
	int len = http_response_string(resp, &resp_str);
	int write_res = 0;
	if (len >= 0) {
		write_res = sendall(sock, resp_str, len);
		free(resp_str);
	}
	http_response_free(resp);
	return write_res;
}

void child_main_loop(int sock, pid_t parent_pid) {

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

	time_t keepalive_timer = 0;

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
	int request_num = 1;		// True
	int keep_conn_alive = 0;	// False
	int recv_timer_started = 1;	// True

	// Main event loop
	zhttpd_log(LOG_DEBUG, "Child event loop starting");

	unsigned int buf_size = 1024;
	unsigned int got_bytes = 0;
	unsigned int recv_buf_size = buf_size;

	char *received = calloc(recv_buf_size, sizeof(char));

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
					//printf("read: %u\n", count);
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
				
				http_request *req;
				int ret = http_request_parse(received, got_bytes, &req);
				if (ret < 0) {

					if (ret == ERROR_PARSER_GET_MORE_DATA) {
						// Need more data
						zhttpd_log(LOG_DEBUG, "Need more data to parse the request");
						continue;
					} else {
						zhttpd_log(LOG_ERROR, "Request parsing failed with error code %d", ret);
						//fprintf(stderr, "http_request_parse failed with error code: %d\n", ret);
						if (ret == ERROR_PARSER_MALFORMED_REQUEST || ret == ERROR_PARSER_NO_HOST_HEADER) {
							// Malformed request or HTTP/1.1 request without Host header
							send_error_response(NULL, sock, 400);

						} else if (ret == ERROR_PARSER_INVALID_METHOD) {
							// Unsupported method
							send_error_response(NULL, sock, 405);

						} else if (ret == ERROR_PARSER_UNSUPPORTED_FORM_ENCODING) {
							// Unsupported form encoding
							http_request_free(req);	// Request is still set in this case
							// Respond with "501 Not Implemented" for now
							send_error_response(NULL, sock, 501);
						}
					}
				} else {
					zhttpd_log(LOG_DEBUG, "New HTTP request:");
					zhttpd_log(LOG_DEBUG, "  Method: %s", req->method);
					zhttpd_log(LOG_DEBUG, "  Path: %s", req->path);
					if (req->query_str != NULL) zhttpd_log(LOG_DEBUG, "  Query: %s", req->query_str);
					zhttpd_log(LOG_DEBUG, "  %u header(s):", req->header_count);
					for (size_t i = 0; i < req->header_count; i++) {
						http_header *h = req->headers[i];
						zhttpd_log(LOG_DEBUG, "    %s: \"%s\"", h->name, h->value);
					}

					for (size_t i = 0; i < req->header_count; i++) {
						http_header *h = req->headers[i];
						if (strcmp(h->name, "Connection") == 0) {
							char *value = string_to_lowercase(h->value);
							if (strcmp(value, "keep-alive") == 0) {
								keep_conn_alive = 1;	// Set to true
								req->keep_alive = 1;
							}
							free(value);
							if (keep_conn_alive) zhttpd_log(LOG_DEBUG, "Client wants to keep connection alive");
							break;
						}
					}

					if (strcmp(req->method, "GET") == 0 || strcmp(req->method, "POST") == 0 || strcmp(req->method, "HEAD") == 0) {
						// Concatenate file path and prevent free filesystem access
						char *final_path;
						int rp_ret = create_real_path(WEBROOT, strlen(WEBROOT), req->path, strlen(req->path), &final_path);
						if (rp_ret < 0) {
							// Invalid path, send "400 Bad Request"
							send_error_response(req, sock, 400);

						} else {
							// Valid path
							zhttpd_log(LOG_INFO, "Client requests file: \"%s\"", final_path);

							// Get file extension
							char *ext = NULL;
							char *ext_start = strstr(final_path, ".");
							if (ext_start != NULL) {
								ext = (ext_start + sizeof(char));
								zhttpd_log(LOG_DEBUG, "File extension: %s", ext);
							}

							if (ext != NULL && strcmp(ext, "php") == 0) {
								// Run PHP script
								// TODO: Check if the file really exists
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
									send_error_response(req, sock, 500);

								} else if (cgi_ret == ERROR_CGI_SCRIPT_PATH_INVALID) {
									// Script path invalid, send "404 Not Found"
									send_error_response(req, sock, 404);

								} else if (cgi_ret == ERROR_CGI_STATUS_NONZERO) {
									// TODO: Handle non-zero status code
									// For now just send "500 Internal Server Error" instead
									free(php_out);
									send_error_response(req, sock, 500);

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
									resp->keep_alive = keep_conn_alive;
									if (strcmp(req->method, "HEAD") == 0) resp->head_response = 1;	// This is a HEAD response
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

								unsigned char *file_data;
								ssize_t file_bytes = read_file(final_path, &file_data);
								if (file_bytes < 0) {
									if (file_bytes == ERROR_FILE_IO_NO_ACCESS) {
										// Respond with "403 Forbidden"
										send_error_response(req, sock, 403);

									} else if (file_bytes == ERROR_FILE_IO_NO_ENT) {
										// File not found, respond with "404 File Not Found"
										send_error_response(req, sock, 404);

									} else if (file_bytes == ERROR_FILE_IO_GENERAL) {
										// I/O error, response with "500 Internal Server Error"
										send_error_response(req, sock, 500);
									}
								} else {
									// Got file, send response
									keepalive_timer = time(NULL);	// TODO: Reset timer somewhere else?
									http_response *resp = http_response_create(200);
									resp->keep_alive = keep_conn_alive;
									if (strcmp(req->method, "HEAD") == 0) resp->head_response = 1;	// This is a HEAD response

									// Set content
									// TODO: Do this somewhere else?
									// TODO: Check case-insensitively
									if (ext != NULL && strcmp(ext, "css") == 0) {
										http_response_add_header2(resp, "Content-Type", "text/css");
										http_response_set_content(resp, file_data, file_bytes);
									} else {
										// Guess Content-Type
										http_response_set_content2(resp, file_data, file_bytes, CONTENT_SET_CONTENT_TYPE);
									}
									char *resp_str;
									int len = http_response_string(resp, &resp_str);
									if (len >= 0) {
										if (sendall(sock, resp_str, len) == -1) {
											zhttpd_log(LOG_ERROR, "Response sending failed!");
											perror("sendall");
										}
										free(resp_str);
									}
									http_response_free(resp);
									free(file_data);
								}
							}

							free(final_path);
						}

					} else {
						// Only GET is supported at this moment
						// Send "501 Not Implemented"
						send_error_response(req, sock, 501);
					}

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
					keepalive_timer = time(NULL);
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

			send_error_response(NULL, sock, 408);
			run_child_main_loop = 0;
		}

		if (keep_conn_alive && time(NULL) - keepalive_timer >= REQUEST_KEEPALIVE_TIMEOUT_SECONDS) {
			// Keep-alive timeout
			// Just close connection for now
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