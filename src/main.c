#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>

#include "child.h"
#include "utils.h"

volatile sig_atomic_t run_main_loop = 0;

static void signal_handler(int signal) {
	run_main_loop = 1;
}

int main(int argc, char *argv[]) {

	zhttpd_log(LOG_INFO, "zhttpd starting on port %d", LISTEN_PORT);

	zhttpd_log(LOG_DEBUG, "Registering signal handler for SIGINT");
	struct sigaction sigint_sigaction = {
		.sa_handler = signal_handler
	};

	if (sigaction(SIGINT, &sigint_sigaction, NULL) == -1) {
		zhttpd_log(LOG_CRIT, "Signal handler registering failed!");
		perror("sigaction");
		exit(1);
	}

	zhttpd_log(LOG_DEBUG, "Creating server socket");
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == -1) {
		zhttpd_log(LOG_CRIT, "Server socket init failed!");
		perror("Listen socket init");
		exit(1);
	}

	int enable = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		zhttpd_log(LOG_CRIT, "Server socket option setting failed!");
		perror("setsockopt");
		exit(1);
	}

	zhttpd_log(LOG_DEBUG, "Binding server socket");
	struct sockaddr_in bind_addr;
	memset(&bind_addr, 0, sizeof(bind_addr));

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	bind_addr.sin_port = htons(LISTEN_PORT);

	if (bind(server_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == -1) {
		zhttpd_log(LOG_CRIT, "Server socket binding failed!");
		perror("Listen socket bind");
		exit(1);
	}

	if (make_socket_nonblocking(server_sock) == -1) {
		zhttpd_log(LOG_CRIT, "Setting server socket non-blocking failed!");
		exit(1);
	}

	if (listen(server_sock, 5) == -1) {
		zhttpd_log(LOG_CRIT, "Connection listening failed!");
		perror("Server listen");
		exit(1);
	}

	zhttpd_log(LOG_INFO, "zhttpd ready, waiting for connections");

	pid_t pid = 1234;

	while (run_main_loop == 0) {

		struct sockaddr_in in_addr = {0};
		socklen_t in_len = sizeof(in_addr);

		int cli_sock = accept(server_sock, (struct sockaddr *)&in_addr, &in_len);
		if (cli_sock == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				zhttpd_log(LOG_ERROR, "Connection accepting failed!");
				perror("accept");
				continue;
			}
		} else {
			zhttpd_log(LOG_INFO, "New connection accepted");
			// Fork!
			pid = fork();
			if (pid == 0) {
				close(server_sock);
				child_main_loop(cli_sock);
				break;
			} else {
				close(cli_sock);
			}
		}
		usleep(100);
	}

	shutdown(server_sock, SHUT_RDWR);
	close(server_sock);

	if (pid == 0) {
		zhttpd_log(LOG_DEBUG, "Child process shutdown");
	} else {
		zhttpd_log(LOG_INFO, "zhttpd exiting");
	}
	return 0;
}
