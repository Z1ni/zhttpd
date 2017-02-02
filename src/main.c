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
#include <sys/wait.h>
#include <arpa/inet.h>

#include "child.h"
#include "utils.h"

volatile sig_atomic_t run_main_loop = 0;

static void sigint_handler(int signal) {
	run_main_loop = 1;
}

static void sigchld_handler(int signal, siginfo_t *siginfo, void *context) {
	pid_t chld_pid = siginfo->si_pid;
	int exit_status = siginfo->si_status;
	zhttpd_log(LOG_DEBUG, "Child process %d exited with status code %d, reaping", chld_pid, exit_status);
	if (waitpid(chld_pid, NULL, 0) == -1) {
		zhttpd_log(LOG_ERROR, "Waitpid failed!");
		perror("waitpid");
	}
	zhttpd_log(LOG_DEBUG, "SIGCHLD handled");
}

int main(int argc, char *argv[]) {

	zhttpd_log(LOG_INFO, "zhttpd starting on port %d", LISTEN_PORT);

	zhttpd_log(LOG_DEBUG, "Registering signal handler for SIGINT");
	struct sigaction sigint_sigaction = {
		.sa_handler = sigint_handler
	};

	if (sigaction(SIGINT, &sigint_sigaction, NULL) == -1) {
		zhttpd_log(LOG_CRIT, "SIGINT signal handler registering failed!");
		perror("sigaction");
		exit(1);
	}

	zhttpd_log(LOG_DEBUG, "Registering signal handler for SIGCHLD");
	struct sigaction sigchld_sigaction = {
		.sa_sigaction = sigchld_handler,
		.sa_flags = SA_SIGINFO
	};

	if (sigaction(SIGCHLD, &sigchld_sigaction, NULL) == -1) {
		zhttpd_log(LOG_CRIT, "SIGCHLD signal handler registering failed!");
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

	// Get possible addresses to bind to
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_PASSIVE
	};
	struct addrinfo *serv_info;

	// Port number to string
	char port_str[6];
	snprintf(port_str, 6, "%d", LISTEN_PORT);

	int gai_ret = getaddrinfo(NULL, port_str, &hints, &serv_info);
	if (gai_ret != 0) {
		zhttpd_log(LOG_CRIT, "Getting address info for localhost failed: %s", gai_strerror(gai_ret));
		exit(1);
	}

	// Loop through found addresses
	struct addrinfo *p;
	int bind_ok = 0;
	for (p = serv_info; p != NULL; p = p->ai_next) {
		if (bind(server_sock, p->ai_addr, p->ai_addrlen) == -1) {
			zhttpd_log(LOG_WARN, "Server socket binding failed!");
			perror("Listen socket bind");
			continue;
		}
		bind_ok = 1;
		break;
	}
	if (bind_ok == 0) {
		zhttpd_log(LOG_CRIT, "Couldn't bind server socket!");
		exit(1);
	}

	freeaddrinfo(serv_info);

	if (make_socket_nonblocking(server_sock) == -1) {
		zhttpd_log(LOG_CRIT, "Setting server socket non-blocking failed!");
		exit(1);
	}

	if (listen(server_sock, LISTEN_LIMIT) == -1) {
		zhttpd_log(LOG_CRIT, "Connection listening failed!");
		perror("Server listen");
		exit(1);
	}

	zhttpd_log(LOG_INFO, "zhttpd ready, waiting for connections");

	pid_t pid = 1234;

	while (run_main_loop == 0) {

		struct sockaddr_storage in_addr = {0};
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

			// Get address info
			char str_addr[INET6_ADDRSTRLEN];
			void *sin_addr;
			if (in_addr.ss_family == AF_INET) {
				// IPv4
				sin_addr = &(((struct sockaddr_in *)&in_addr)->sin_addr);
			} else if (in_addr.ss_family == AF_INET6) {
				// IPv6
				sin_addr = &(((struct sockaddr_in6 *)&in_addr)->sin6_addr);
			} else {
				zhttpd_log(LOG_ERROR, "Unknown socket family %d!", in_addr.ss_family);
				shutdown(cli_sock, SHUT_RDWR);
				close(cli_sock);
				continue;
			}
			if (inet_ntop(in_addr.ss_family, sin_addr, str_addr, sizeof(str_addr)) == NULL) {
				zhttpd_log(LOG_ERROR, "Getting address string failed!");
				perror("inet_ntop");
				shutdown(cli_sock, SHUT_RDWR);
				close(cli_sock);
				continue;
			}
			zhttpd_log(LOG_DEBUG, "Client address: %s", str_addr);

			// Fork!
			pid_t parent_pid = getpid();
			pid = fork();
			if (pid == 0) {
				close(server_sock);
				child_main_loop(cli_sock, parent_pid, str_addr);
				break;
			} else {
				close(cli_sock);
			}
		}
		usleep(5000);	// TODO: Use epoll instead
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
