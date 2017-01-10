#include "child.h"
#include "utils.h"
#include "http.h"
#include "http_request_parser.h"
#include "file_io.h"

volatile sig_atomic_t run_child_main_loop = 1;

static void sigint_handler(int signal) {
	// Parent died or someone wants this process to stop
	zhttpd_log(LOG_WARN, "Child received SIGINT, closing");
	run_child_main_loop = 0;
}

void child_main_loop(int sock, pid_t parent_pid) {

	zhttpd_log(LOG_INFO, "Child process started to handle the connection");

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
							http_response *resp = http_response_create(400);
							char *resp_str;
							int len = http_response_string(resp, &resp_str);
							if (len >= 0) {
								write(sock, resp_str, len);
								free(resp_str);
							}
							http_response_free(resp);

						} else if (ret == ERROR_PARSER_INVALID_METHOD) {
							// Unsupported method
							http_response *resp = http_response_create(405);
							char *resp_str;
							int len = http_response_string(resp, &resp_str);
							if (len >= 0) {
								write(sock, resp_str, len);
								free(resp_str);
							}
							http_response_free(resp);
						}
					}
				} else {
					zhttpd_log(LOG_DEBUG, "New HTTP request:");
					zhttpd_log(LOG_DEBUG, "  Method: %s", req->method);
					zhttpd_log(LOG_DEBUG, "  Path: %s", req->path);
					zhttpd_log(LOG_DEBUG, "  %u header(s):", req->header_count);
					for (size_t i = 0; i < req->header_count; i++) {
						http_header *h = req->headers[i];
						zhttpd_log(LOG_DEBUG, "    %s: \"%s\"", h->name, h->value);
					}

					for (size_t i = 0; i < req->header_count; i++) {
						http_header *h = req->headers[i];
						if (strcmp(h->name, "Connection") == 0) {
							char *value = string_to_lowercase(h->value);
							if (strcmp(value, "keep-alive") == 0) keep_conn_alive = 1;	// Set to true
							free(value);
							if (keep_conn_alive) zhttpd_log(LOG_DEBUG, "Client wants to keep connection alive");
							break;
						}
					}

					if (strcmp(req->method, "GET") == 0) {
						// Concatenate file path and prevent free filesystem access
						char *final_path;
						int rp_ret = create_real_path(WEBROOT, strlen(WEBROOT), req->path, strlen(req->path), &final_path);
						if (rp_ret < 0) {
							// Invalid path, send "400 Bad Request"
							http_response *resp = http_response_create(400);
							resp->keep_alive = keep_conn_alive;
							char *resp_str;
							int len = http_response_string(resp, &resp_str);
							if (len >= 0) {
								write(sock, resp_str, len);
								free(resp_str);
							}
							http_response_free(resp);

						} else {
							// Valid path
							zhttpd_log(LOG_INFO, "Client requests file: \"%s\"", final_path);
							unsigned char *file_data;
							ssize_t file_bytes = read_file(final_path, &file_data);
							if (file_bytes < 0) {
								if (file_bytes == ERROR_FILE_IO_NO_ACCESS) {
									// Respond with "403 Forbidden"
									http_response *resp = http_response_create(403);
									resp->keep_alive = keep_conn_alive;
									char *resp_str;
									int len = http_response_string(resp, &resp_str);
									if (len >= 0) {
										write(sock, resp_str, len);
										free(resp_str);
									}
									http_response_free(resp);

								} else if (file_bytes == ERROR_FILE_IO_NO_ENT) {
									// File not found, respond with "404 File Not Found"
									http_response *resp = http_response_create(404);
									resp->keep_alive = keep_conn_alive;
									char *resp_str;
									int len = http_response_string(resp, &resp_str);
									if (len >= 0) {
										write(sock, resp_str, len);
										free(resp_str);
									}
									http_response_free(resp);

								} else if (file_bytes == ERROR_FILE_IO_GENERAL) {
									// I/O error, response with "500 Internal Server Error"
									http_response *resp = http_response_create(500);
									resp->keep_alive = keep_conn_alive;
									char *resp_str;
									int len = http_response_string(resp, &resp_str);
									if (len >= 0) {
										write(sock, resp_str, len);
										free(resp_str);
									}
									http_response_free(resp);
								}
							} else {
								// Got file, send response
								http_response *resp = http_response_create(200);
								resp->keep_alive = keep_conn_alive;
								http_response_set_content2(resp, file_data, file_bytes, CONTENT_SET_CONTENT_TYPE);
								char *resp_str;
								int len = http_response_string(resp, &resp_str);
								if (len >= 0) {
									write(sock, resp_str, len);
									free(resp_str);
								}
								http_response_free(resp);
								free(file_data);
							}

							free(final_path);
						}

					} else {
						// Only GET is supported at this moment
						http_response *resp = http_response_create(501);
						resp->keep_alive = keep_conn_alive;
						char *resp_str;
						int len = http_response_string(resp, &resp_str);
						if (len >= 0) {
							write(sock, resp_str, len);
							free(resp_str);
						}
						http_response_free(resp);
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
			http_response *resp = http_response_create(408);
			char *resp_str;
			int len = http_response_string(resp, &resp_str);
			if (len >= 0) {
				write(sock, resp_str, len);
				free(resp_str);
			}
			http_response_free(resp);
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