#include "child.h"
#include "utils.h"

void child_main_loop(int sock) {

	printf("child_main_loop\n");

	// Make given socket nonblocking
	if (make_socket_nonblocking(sock) == -1) abort();

	struct epoll_event event;
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

	// TODO: Close connection after n seconds of inactivity (10 seconds or something like that)

	// Main event loop
	int run = 0;
	printf("child_main_loop event loop start\n");
	while (run == 0) {

		//printf("wait\n");
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
				unsigned int buf_size = 1024;
				unsigned int got_bytes = 0;
				unsigned int recv_buf_size = buf_size;
				
				char *received = calloc(recv_buf_size, sizeof(char));
				char *buf = calloc(buf_size, sizeof(char));
				
				while (1) {
					ssize_t count = 0;

					count = read(events[i].data.fd, buf, sizeof(buf));
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
					//printf("Handle recv\n");
					// Check if the received data fits into the final buffer
					if (recv_buf_size < got_bytes + count) {
						// Doesn't fit, resize buffer
						recv_buf_size *= 2;
						received = realloc(received, recv_buf_size * sizeof(char));
					}
					// Copy received data to the final buffer
					memcpy(&received[got_bytes], buf, count);
					got_bytes += count;
					//printf("Total bytes: %u\n", got_bytes);
				}
				free(buf);	// Free temp recv buffer

				// Add zero byte
				// Resize the final recv data to fit
				received = realloc(received, (got_bytes + 1) * sizeof(char));
				received[got_bytes] = '\0';

				if (done) {
					printf("Connection closed, fd: %d\n", events[i].data.fd);
					close(events[i].data.fd);
					run = 1;	// Stop the main child loop
				}

				// Receiving ends
				// Handle final received data here ================================================
				
				//printf("%s\n", received);
				
				printf("Creating response\n");
				http_response *resp = http_response_create(501);
				char *resp_html;
				int c_len = asprintf(&resp_html,
					"<html><head>\r\n \
					<title>501 Not Implemented</title>\r\n \
					</head></body>\r\n \
					<h1>Not Implemented</h1>\r\n \
					<p>Sorry, the server doesn't know how to handle the request.<br />\r\n \
					</p>\r\n \
					<hr>\r\n \
					<address>%s on port %d</address>\r\n</body></html>\r\n",
					SERVER_IDENT, 8080
				);
				http_response_set_content(resp, (unsigned char *)resp_html, c_len);
				free(resp_html);

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

				http_response_free(resp);


				// Handling the data ends =========================================================
				free(received);
				printf("\nReceived data handled\n");

				run = 1;
			}

			usleep(10);

		}
	}
	printf("child_main_loop event loop end\n");

	free(events);

	// Close socket
	shutdown(sock, SHUT_RDWR);
	close(sock);

	// We're done!
}