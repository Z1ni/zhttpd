#ifndef __CHILD_H__
#define __CHILD_H__

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>

void child_main_loop(int sock, pid_t parent_pid);

#endif