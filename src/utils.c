#include "utils.h"

/**
 * @brief Log strings to stdout
 * @details Logs formatted and possibly colored strings to stdout.
 * 			Maximum message length is 1023 characters
 * 			Output is colored if COLOR_LOG_OUTPUT is defined.
 * 
 * @param level Log level, see \ref LOG_LEVEL
 * @param format printf format string
 */
void zhttpd_log(LOG_LEVEL level, const char *format, ...) {
	if (level > DEBUG_MIN_LEVEL) return;
	const char *final_format = "%s [%5d] [%s] - %s\n";
	char final_format_str[1024] = {0};
	char final_str[1024] = {0};
	char *level_str = NULL;

	char *date_str;
	if (current_datetime_string2(&date_str, "%Y-%m-%d %H:%M:%S") < 0) return;

	if (level == LOG_CRIT) {
		#ifdef COLOR_LOG_OUTPUT
		level_str = ANSI_COLOR_RED "CRIT " ANSI_COLOR_RESET;
		#else
		level_str = "CRIT ";
		#endif

	} else if (level == LOG_ERROR) {
		#ifdef COLOR_LOG_OUTPUT
		level_str = ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET;
		#else
		level_str = "ERROR";
		#endif

	} else if (level == LOG_WARN) {
		#ifdef COLOR_LOG_OUTPUT
		level_str = ANSI_COLOR_YELLOW "WARN " ANSI_COLOR_RESET;
		#else
		level_str = "WARN ";
		#endif

	} else if (level == LOG_INFO) {
		#ifdef COLOR_LOG_OUTPUT
		level_str = ANSI_COLOR_CYAN "INFO " ANSI_COLOR_RESET;
		#else
		level_str = "INFO ";
		#endif

	} else if (level == LOG_DEBUG) {
		#ifdef COLOR_LOG_OUTPUT
		level_str = ANSI_COLOR_GREEN "DEBUG" ANSI_COLOR_RESET;
		#else
		level_str = "DEBUG";
		#endif

	} else {
		level_str = "-----";
	}

	if (snprintf(final_format_str, 1023, final_format, date_str, getpid(), level_str, format) < 0) return;
	free(date_str);
	va_list args;
	va_start(args, format);
	int r = vsnprintf(final_str, 1023, final_format_str, args);
	va_end(args);
	if (r < 0) return;

	// final_str contains final string
	FILE *dest = stdout;
	if (level <= LOG_ERROR) dest = stderr;	// Log errors and critical messages to stderr
	fprintf(dest, "%s", final_str);
}

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
 * @brief Split text by delimiter
 * @details Tokenizes text by given delimiter. Ignores subsequent delimiters.
 *          E.g. " foo   bar " splitted by ' ' becomes "foo", "bar".
 * 
 * @param in Text to split
 * @param in_len Length of \p in
 * @param delim Delimiter
 * @param[out] out Pointer to non-allocated memory where the result will be saved
 * @param limit How many splits to do. -1 means no limit, split all.
 * @return Word count
 */
int split_line2(const char *in, size_t in_len, char delim, char ***out, int limit) {

	size_t words_cap = 10;
	size_t words_count = 0;
	char **words = calloc(words_cap, sizeof(char *));
	size_t word_cap = 10;
	size_t word_pos = 0;
	char *word = calloc(word_cap, sizeof(char));

	int word_in_progress = 0;
	for (size_t i = 0; i < in_len; i++) {
		if (limit == -1 || (limit != -1 && words_count < limit)) {
			if (in[i] == delim) {
				// Skip delimiters
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
 * @brief Split text by delimiter
 * @details Tokenizes text by given delimiter. Ignores subsequent delimiters.
 *          E.g. " foo   bar " splitted by ' ' becomes "foo", "bar".
 *          Calls split_line2() with \p limit -1
 * 
 * @param in Text to split
 * @param in_len Length of \p in
 * @param delim Delimiter
 * @param[out] out Pointer to non-allocated memory where the result will be saved
 * @return Word count
 */
int split_line(const char *in, size_t in_len, char delim, char ***out) {
	return split_line2(in, in_len, delim, out, -1);
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

/**
 * @brief Convert string to lower case
 * @details Converts upper/mixed case string to lower case.
 * 
 * @param str String to convert
 * @return Dynamically allocated string in lower case. User is responsible for freeing the memory.
 */
char * string_to_lowercase(char *str) {
	char *out = calloc(strlen(str)+1, sizeof(char));
	for (int i = 0; i < strlen(str); i++) {
		out[i] = tolower(str[i]);
	}
	return out;
}

/**
 * @brief Convert string to upper case
 * @details Converts lower/mixed case string to upper case.
 * 
 * @param str String to convert
 * @return Dynamically allocated string in upper case. User is responsible for freeing the memory.
 */
char * string_to_uppercase(char *str) {
	char *out = calloc(strlen(str)+1, sizeof(char));
	for (int i = 0; i < strlen(str); i++) {
		out[i] = toupper(str[i]);
	}
	return out;
}

/**
 * @brief Create real filesystem path from webroot and request paths
 * @details Concatenates webroot and request paths securely
 * 
 * @param webroot Webroot path
 * @param webroot_len Length of \p webroot
 * @param path Request path
 * @param path_len Length of \p path
 * @param[out] out Pointer to non-allocated memory that will contain the concatenated path
 * @return Length of the concatenated path or < 0 on error
 */
int create_real_path(const char *webroot, size_t webroot_len, const char *path, size_t path_len, char **out) {
	size_t real_path_cap = webroot_len + path_len;
	char *real_path = calloc(webroot_len + path_len + 1, sizeof(char));
	size_t real_path_pos = 0;
	memcpy(real_path, webroot, webroot_len);
	real_path_pos += webroot_len;

	if (real_path[real_path_pos-1] != '/') {
		// Add trailing slash
		real_path[real_path_pos++] = '/';
	}

	size_t path_start = (path[0] == '/' ? 1 : 0);	// Ignore prefixing slash if needed

	char prev = (path_start ? '/' : real_path[real_path_pos-1]);
	for (size_t i = path_start; i < path_len; i++) {
		char c = path[i];
		if (c == '.' && prev == '.') {
			// Not allowed, the user tries to traverse the filesystem (e.g. "/../../../../etc/passwd")
			free(real_path);
			return ERROR_PATH_EXPLOITING;
		}
		if ((c == '/' && prev == '/') || (c == '.' && prev == '/')) {
			// Invalid path, two slashes "//" or '.' following '/' ("/.")
			free(real_path);
			return ERROR_PATH_INVALID;
		}
		if ((c >= '-' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_')) {
			// Allowed character
			real_path[real_path_pos++] = c;
		} else {
			// Not allowed character
			free(real_path);
			return ERROR_PATH_INVALID;
		}

		prev = c;
	}

	// Check if real_path is pointing to a directory
	struct stat path_stat;
	stat(real_path, &path_stat);
	if (stat(real_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
		// Is directory, append '/'
		real_path[real_path_pos++] = '/';
	}

	// If the path ends with '/', add "index.html"
	// TODO: Select between "index.htm" and "index.html", etc.
	if (real_path[real_path_pos-1] == '/') {
		if (real_path_cap < real_path_pos + 10) {
			real_path = realloc(real_path, (real_path_pos + 10) * sizeof(char));
		}
		memcpy(&real_path[real_path_pos], "index.html", 10 * sizeof(char));
		real_path_pos += 10;
	}

	// Realloc
	real_path = realloc(real_path, (real_path_pos+1) * sizeof(char));
	real_path[real_path_pos] = '\0';

	*out = real_path;
	return real_path_pos;
}

/**
 * @brief Get mimetype & charset string
 * @details Uses libmagic to obtain mimetype and charset for given buffer
 * 
 * @param buf Buffer to check
 * @param buf_len Length of \p buf
 * @param[out] out Pointer to non-allocated memory that will contain the mimetype & charset string
 * 
 * @return 0 on success, < 0 on error
 */
int libmagic_get_mimetype(const unsigned char *buf, size_t buf_len, char **out) {

	char *desc_out;

	magic_t lm = magic_open(MAGIC_MIME_TYPE | MAGIC_MIME_ENCODING | MAGIC_NO_CHECK_COMPRESS | MAGIC_NO_CHECK_TAR | MAGIC_NO_CHECK_ELF | MAGIC_NO_CHECK_TOKENS | MAGIC_NO_CHECK_TROFF);
	if (lm == NULL) {
		zhttpd_log(LOG_ERROR, "Libmagic open failed: %s", magic_error(lm));
		return -1;
	}
	if (magic_load(lm, NULL) == 1) {
		zhttpd_log(LOG_ERROR, "Libmagic load failed: %s", magic_error(lm));
		magic_close(lm);
		return -1;
	}

	const char *desc = magic_buffer(lm, buf, buf_len);
	if (desc == NULL) {
		zhttpd_log(LOG_ERROR, "Libmagic buffer detect failed: %s", magic_error(lm));
		magic_close(lm);
		return -1;
	}
	desc_out = strdup(desc);

	magic_close(lm);

	// Remove "charset=binary" if needed
	char *p = strstr(desc_out, "; charset=binary");
	if (p != NULL) {
		*p = '\0';
		// "Realloc"
		char *tmp = strdup(desc_out);
		free(desc_out);
		desc_out = tmp;
	}

	*out = desc_out;
	return 0;
}

/**
 * @brief URL decode
 * @details Decode URL-encoded text
 * 
 * @param in String to decode
 * @param in_len Length of \p in
 * @param[out] out Pointer to non-allocated memory where the result will be placed
 * @return Length of \p out in bytes or < 0 on error
 */
int url_decode(const char *in, size_t in_len, char **out) {
	char *out_tmp = calloc(in_len, sizeof(char));
	char hex[2] = {0};
	int get_hex = 0;
	size_t hex_pos = 0;
	size_t out_pos = 0;

	for (size_t i = 0; i < in_len; i++) {
		if (get_hex == 0 && in[i] == '%') {
			// Decode
			get_hex = 1;
		} else if (get_hex == 1) {
			// Read hex
			hex[hex_pos++] = in[i];
			if (hex_pos > 1) {
				get_hex = 0;
				hex_pos = 0;
				// Decode hex to char
				errno = 0;
				long ret = strtol(hex, NULL, 16);
				// TODO: Handle null bytes (if ret == 0)
				if (errno != 0) {
					// Failed
					perror("strtol");
					free(out_tmp);
					return -1;
				}
				char c = (char)ret;
				out_tmp[out_pos++] = c;
			}
		} else if (get_hex == 0) {
			char out_c = in[i];
			if (in[i] == '+') out_c = ' ';
			out_tmp[out_pos++] = out_c;
		}
	}
	if (hex_pos != 0) {
		// Malformed input string
		free(out_tmp);
		return -1;
	}

	out_tmp = realloc(out_tmp, (out_pos+1) * sizeof(char));
	out_tmp[out_pos] = '\0';

	*out = out_tmp;
	return out_pos;
}

/**
 * @brief URL encode
 * @details Encode text with "URL encoding"
 * 
 * @param in String to encode
 * @param in_len Length of \p in
 * @param out Pointer to non-allocated memory where the result will be placed
 * @return Length of \p out in bytes or < 0 on error
 */
int url_encode(const char *in, size_t in_len, char **out) {

	char *out_tmp = calloc(in_len, sizeof(char));
	size_t out_pos = 0;
	size_t out_cap = in_len;

	for (size_t i = 0; i < in_len; i++) {
		char c = in[i];
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
			// Normal ASCII char
			if (out_cap < out_pos+1) {
				// Realloc
				out_cap *= 2;
				out_tmp = realloc(out_tmp, out_cap * sizeof(char));
			}
			out_tmp[out_pos++] = c;
		} else if (c == ' ') {
			out_tmp[out_pos++] = '+';	// TODO: Let space be encoded as "%20" instead?
		} else {
			// Char needs to be encoded
			char hex[4] = {0};
			if (snprintf(hex, 4, "%%%X", c) != 3) {
				// snprintf failed
				free(out_tmp);
				return -1;
			}
			if (out_cap < out_pos+3) {
				// Realloc
				out_cap *= 2;
				out_tmp = realloc(out_tmp, out_cap * sizeof(char));
			}
			memcpy(&out_tmp[out_pos], hex, 3);
			out_pos += 3;
		}
	}

	out_tmp = realloc(out_tmp, (out_pos+1) * sizeof(char));
	out_tmp[out_pos] = '\0';

	*out = out_tmp;

	return out_pos;
}
