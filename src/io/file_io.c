#include "file_io.h"

/**
 * @brief Read file to buffer
 * @details Reads file to given non-allocated buffer
 * 
 * @param path File path
 * @param[out] out Pointer to non-allocated buffer for file contents
 * 
 * @return Number of bytes read or < 0 on error
 */
ssize_t read_file(const char *path, unsigned char **out) {

	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		if (errno == EACCES) {
			// Requested access isn't allowed
			return ERROR_FILE_IO_NO_ACCESS;
		} else if (errno == ENOENT) {
			// Requested file doesn't exist
			return ERROR_FILE_IO_NO_ENT;
		}
	}
	size_t buf_size = 1024;
	size_t final_cap = 1024;
	size_t final_pos = 0;
	unsigned char *buf = calloc(buf_size, sizeof(unsigned char));
	unsigned char *final = calloc(final_cap, sizeof(unsigned char));

	while (1) {
		ssize_t count = read(fd, buf, buf_size);

		if (count == -1) {
			// Read error
			perror("file read");
			free(buf);
			free(final);
			return ERROR_FILE_IO_GENERAL;
		} else if (count == 0) {
			// End of file
			final = realloc(final, final_pos * sizeof(unsigned char));
			break;
		}

		while (final_cap < final_pos + count) {
			final_cap *= 2;
			final = realloc(final, final_cap * sizeof(unsigned char));
		}

		memcpy(&final[final_pos], buf, count);
		final_pos += count;
	}
	free(buf);
	if (close(fd) == -1) {
		free(final);
		return ERROR_FILE_IO_GENERAL;
	}

	*out = final;

	return final_pos;
}
