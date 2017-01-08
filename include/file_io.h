#ifndef __FILE_IO_H__
#define __FILE_IO_H__

#include <unistd.h>
#include "utils.h"

ssize_t read_file(const char *path, unsigned char **out);

#endif
