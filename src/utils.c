#include "utils.h"

/**
 * @brief Make socket non-blocking
 * @details Makes socket non-blocking
 * 
 * @param sockfd Socket file descriptor
 * @return 0 if successful, < 0 on error
 */
int make_socket_nonblocking(int sockfd) {
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1) {
		perror("make_socket_nonblocking F_GETFL");
		return -1;
	}

	flags |= O_NONBLOCK;
	if (fcntl(sockfd, F_SETFL, flags) == -1) {
		perror("make_socket_nonblocking F_SETFL");
		return -1;
	}
	return 0;
}

/**
 * @brief Get current date/time string
 * @details Produces string with given strftime() format.
 *          Caller must free the string after use.
 * 
 * @param[out] str Non-allocated pointer where the result will be written
 * @param format strftime() format string
 * 
 * @return Length of \p str or < 0 on error
 */
int current_datetime_string2(char **str, const char *format) {
	if (format == NULL) return -1;
	time_t now = time(NULL);
	struct tm *now_gmt = gmtime(&now);
	int max_size = strlen(format) * 2;
	*str = calloc(max_size, sizeof(char));
	int len = strftime((*str), max_size-1, format, now_gmt);
	(*str) = realloc((*str), (len+1) * sizeof(char));
	(*str)[len] = '\0';
	return len;
}

/**
 * @brief Get current date/time string
 * @details Produces Date-header formatted date string.
 *          Caller must free the string after use.
 * 
 * @param[out] str Non-allocated pointer where the result will be written
 * @return Length of \p str or < 0 on error
 */
int current_datetime_string(char **str) {
	return current_datetime_string2(str, "%a, %d %b %Y %H:%M:%S %Z");
}

/**
 * @brief Split text by delimeter
 * @details Tokenizes text by given delimeter. Ignores subsequent delimeters.
 *          E.g. " foo   bar " splitted by ' ' becomes "foo", "bar".
 * 
 * @param in Text to split
 * @param in_len Length of \p in
 * @param delim Delimeter
 * @param[out] out Pointer to non-allocated memory where the result will be saved
 * @return Word count
 */
int split_line(const char *in, size_t in_len, char delim, char ***out) {

	size_t words_cap = 10;
	size_t words_count = 0;
	char **words = calloc(words_cap, sizeof(char *));
	size_t word_cap = 10;
	size_t word_pos = 0;
	char *word = calloc(word_cap, sizeof(char));

	int word_in_progress = 0;
	for (size_t i = 0; i < in_len; i++) {
		if (in[i] == delim) {
			// Skip delimeters
			if (word_in_progress == 1) word_in_progress = 0;
			continue;
		}
		if (word_in_progress == 0 && word_pos > 0) {
			// Save word
			word = realloc(word, (word_pos+1) * sizeof(char));
			word[word_pos] = '\0';
			if (words_count+1 > words_cap) {
				words_cap *= 2;
				words = realloc(words, words_cap * sizeof(char *));
			}
			words[words_count++] = word;
			
			word = NULL;
			word_pos = 0;
			word = calloc(word_cap, sizeof(char));
		}

		word_in_progress = 1;
		if (word_pos+1 > word_cap) {
			word_cap *= 2;
			word = realloc(word, word_cap * sizeof(char));
		}
		word[word_pos++] = in[i];
	}

	if (word_pos > 0) {
		// Remaining data
		word = realloc(word, (word_pos+1) * sizeof(char));
		word[word_pos] = '\0';
		words = realloc(words, (words_count+1) * sizeof(char *));
		words[words_count++] = word;
		word = NULL;
	}

	words = realloc(words, words_count * sizeof(char *));
	(*out) = words;
	return words_count;
}

/**
 * @brief Free splitted text
 * @details Frees text splitted by split_line()
 * 
 * @param words Array to free
 * @param len Length of \p words
 */
void split_line_free(char **words, size_t len) {
	for (size_t i = 0; i < len; i++) {
		free(words[i]);
	}
	free(words);
}
