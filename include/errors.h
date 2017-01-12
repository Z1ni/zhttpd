#ifndef __ERRORS_H__
#define __ERRORS_H__

/**
 * Errors for \ref http_header
 */
#define ERROR_HEADER_CREATE_FAILED -1

/**
 * Errors for \ref http_response
 */
#define ERROR_RESPONSE_STRING_CREATE_FAILED -1		/**< Creating response string failed */
#define ERROR_RESPONSE_ARGUMENT -2					/**< Invalid argument */
#define ERROR_RESPONSE_SET_CONTENT_TYPE_FAILED -3	/**< Content-Type setting failed */

/**
 * Errors for \ref http_request_parse
 */
#define ERROR_PARSER_MALFORMED_REQUEST -1		/**< Request is malformed */
#define ERROR_PARSER_INVALID_METHOD -2			/**< Unknown method */
#define ERROR_PARSER_URI_TOO_LONG -3			/**< URI is longer than 8000 characters */
#define ERROR_PARSER_UNSUPPORTED_PROTOCOL -4	/**< Protocol is not HTTP/1.1 */
#define ERROR_PARSER_NO_HOST_HEADER -5			/**< Missing Host header */
#define ERROR_PARSER_GET_MORE_DATA -6			/**< Missing some data */

/**
 * Errors for \ref read_file
 */
#define ERROR_FILE_IO_NO_ACCESS -1	/**< File access denied */
#define ERROR_FILE_IO_NO_ENT -2		/**< File doesn't exist */
#define ERROR_FILE_IO_GENERAL -3	/**< General I/O error */

/**
 * Errors for \ref create_real_path
 */
#define ERROR_PATH_EXPLOITING -1	/**< User is trying to exploit file paths */
#define ERROR_PATH_INVALID -2		/**< Invalid path */

/**
 * Errors for \ref exec_cgi
 */
#define ERROR_CGI_EXEC_FAILED -1		/**< CGI program execution failed */
#define ERROR_CGI_STATUS_NONZERO -2		/**< CGI program executed, but with non-zero status. Output is provided */

#endif
