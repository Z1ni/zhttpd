#include "child.h"
#include "utils.h"
#include "http.h"
#include "http_request_parser.h"
#include "file_io.h"

void child_main_loop(int sock) {

	printf("child_main_loop\n");

	// Get current time for recv timeout
	time_t recv_start = time(NULL);

	time_t keepalive_timer = 0;

	// Make given socket nonblocking
	if (make_socket_nonblocking(sock) == -1) abort();

	struct epoll_event event = {0};
	struct epoll_event *events = calloc(MAX_EPOLL_EVENTS, sizeof(event));
	int efd = epoll_create1(0);
	if (efd == -1) {
		perror("child epoll_create1");
		abort();
	}

	event.data.fd = sock;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event) == -1) {
		perror("child epoll_ctl");
		abort();
	}

	int handled = 0;			// False
	int request_num = 1;		// True
	int keep_conn_alive = 0;	// False
	int recv_timer_started = 1;	// True

	// Main event loop
	int run = 1;
	printf("child_main_loop event loop start\n");

	unsigned int buf_size = 1024;
	unsigned int got_bytes = 0;
	unsigned int recv_buf_size = buf_size;

	char *received = calloc(recv_buf_size, sizeof(char));

	while (run) {

		int n = epoll_wait(efd, events, MAX_EPOLL_EVENTS, 0);
		for (int i = 0; i < n; i++) {

			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
				// Error
				fprintf(stderr, "child epoll error\n");
				close(events[i].data.fd);
				run = 1;	// Stop main child loop
				break;
			
			} else {
				// We have data to be read!
				printf("Child got data for reading\n");
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
							perror("child read");
							done = 1;
						}
						break;
					} else if (count == 0) {
						// Remote closed
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
					printf("Connection closed, fd: %d\n", events[i].data.fd);
					close(events[i].data.fd);
					run = 1;	// Stop the main child loop
				}

				// Receiving ends
				// Handle final received data here ================================================
				
				/*printf("%u bytes\n", got_bytes);
				printf("%s\n", received);*/
				
				http_request *req;
				int ret = http_request_parse(received, got_bytes, &req);
				if (ret < 0) {

					if (ret == ERROR_PARSER_GET_MORE_DATA) {
						// Need more data
						printf("http_request_parse needs more data\n");
						continue;
					} else {
						fprintf(stderr, "http_request_parse failed with error code: %d\n", ret);
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
					printf("\nNew http_request:\n");
					printf("  Method: %s\n", req->method);
					printf("  Path: %s\n", req->path);
					printf("  %u headers:\n", req->header_count);
					for (size_t i = 0; i < req->header_count; i++) {
						http_header *h = req->headers[i];
						printf("    %s: \"%s\"\n", h->name, h->value);
					}
					printf("\n");

					for (size_t i = 0; i < req->header_count; i++) {
						http_header *h = req->headers[i];
						if (strcmp(h->name, "Connection") == 0) {
							char *value = string_to_lowercase(h->value);
							if (strcmp(value, "keep-alive") == 0) keep_conn_alive = 1;	// Set to true
							free(value);
							if (keep_conn_alive) printf("Client wants to keep connection alive\n");
							break;
						}
					}

					if (strcmp(req->method, "GET") == 0) {
						// TODO: Concatenate file path and prevent free filesystem access
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
							printf("Client requests file \"%s\"\n", final_path);
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

					/*printf("Creating response\n");
					http_response *resp = http_response_create(501);
					resp->keep_alive = keep_conn_alive;

					printf("Creating response string\n");
					char *resp_str;
					int len = http_response_string(resp, &resp_str);
					if (len >= 0) {
						// Send response
						printf("Sending response\n");
						write(sock, resp_str, len);
						printf("Response sent\n");
						free(resp_str);
					}

					http_response_free(resp);*/
				}


				// Handling the data ends =========================================================
				free(received);
				received = NULL;
				got_bytes = 0;
				printf("\nReceived data handled\n");

				handled = 1;
				if (keep_conn_alive) {
					printf("Starting keepalive timer\n");
					keepalive_timer = time(NULL);
					request_num++;
					recv_buf_size = buf_size;
					received = calloc(recv_buf_size, sizeof(char));
				} else {
					run = 0;
				}
			}
		}
		usleep(10);

		if (handled == 0 && time(NULL) - recv_start >= REQUEST_TIMEOUT_SECONDS) {
			// Receive timeout
			// Send "408 Request Timeout"
			http_response *resp = http_response_create(408);
			char *resp_str;
			int len = http_response_string(resp, &resp_str);
			if (len >= 0) {
				write(sock, resp_str, len);
				free(resp_str);
			}
			http_response_free(resp);
			run = 0;
		}

		if (keep_conn_alive && time(NULL) - keepalive_timer >= REQUEST_KEEPALIVE_TIMEOUT_SECONDS) {
			// Keep-alive timeout
			// Just close connection for now
			run = 0;
		}

	}
	printf("child_main_loop event loop end\n");

	if (received != NULL) free(received);
	free(events);

	// Close socket
	shutdown(sock, SHUT_RDWR);
	close(sock);

	// We're done!
}