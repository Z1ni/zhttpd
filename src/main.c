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
	//printf("Signal hander, sig: %d\n", signal);
	run_main_loop = 1;
}

int main(int argc, char *argv[]) {

	const int port = 8080;

	printf("Registering signal handler\n");
	signal(SIGINT, signal_handler);

	printf("Creating server socket\n");
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == -1) {
		perror("Listen socket init");
		exit(1);
	}

	printf("Binding socket\n");
	struct sockaddr_in bind_addr;
	memset(&bind_addr, 0, sizeof(bind_addr));

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	bind_addr.sin_port = htons(port);

	if (bind(server_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == -1) {
		perror("Listen socket bind");
		exit(1);
	}

	printf("Making socket nonblocking\n");
	if (make_socket_nonblocking(server_sock) == -1) {
		abort();
	}

	printf("Listen\n");
	if (listen(server_sock, 5) == -1) {
		perror("Server listen");
		exit(1);
	}

	printf("Starting main loop, run_main_loop = %d\n", run_main_loop);
	while (run_main_loop == 0) {

		struct sockaddr_in in_addr = {0};
		socklen_t in_len = sizeof(in_addr);

		int cli_sock = accept(server_sock, (struct sockaddr *)&in_addr, &in_len);
		if (cli_sock == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				perror("accept");
				break;
			}
		} else {
			printf("Connection accepted!\n");
			// Fork!
			pid_t pid = fork();
			if (pid == 0) {
				printf("Forking\n");
				child_main_loop(cli_sock);
			}			
		}
		usleep(100);
	}

	printf("Main loop ended\n");

	shutdown(server_sock, SHUT_RDWR);
	close(server_sock);

	printf("Bye\n");
	return 0;

}
