#ifndef __ERRORS_H__
#define __ERRORS_H__

// Errors for http_header()
#define ERROR_HEADER_CREATE_FAILED -1

// Errors for http_response()
#define ERROR_RESPONSE_STRING_CREATE_FAILED -1		/**< Creating response string failed */
#define ERROR_RESPONSE_ARGUMENT -2					/**< Invalid argument */
#define ERROR_RESPONSE_SET_CONTENT_TYPE_FAILED -3	/**< Content-Type setting failed */

// Errors for http_request_parse()
#define ERROR_PARSER_MALFORMED_REQUEST -1			/**< Request is malformed */
#define ERROR_PARSER_INVALID_METHOD -2				/**< Unknown method */
#define ERROR_PARSER_URI_TOO_LONG -3				/**< URI is longer than 8000 characters */
#define ERROR_PARSER_UNSUPPORTED_PROTOCOL -4		/**< Protocol is not HTTP/1.1 */
#define ERROR_PARSER_NO_HOST_HEADER -5				/**< Missing Host header */
#define ERROR_PARSER_GET_MORE_DATA -6				/**< Missing some data */
#define ERROR_PARSER_UNSUPPORTED_FORM_ENCODING -7	/**< Unsupported form encoding, request is still returned */

// Errors for read_file()
#define ERROR_FILE_IO_NO_ACCESS -1	/**< File access denied */
#define ERROR_FILE_IO_NO_ENT -2		/**< File doesn't exist */
#define ERROR_FILE_IO_GENERAL -3	/**< General I/O error */

// Errors for create_real_path()
#define ERROR_PATH_EXPLOITING -1	/**< User is trying to exploit file paths */
#define ERROR_PATH_INVALID -2		/**< Invalid path */

// Errors for exec_cgi()
#define ERROR_CGI_EXEC_FAILED -1			/**< CGI program execution failed */
#define ERROR_CGI_STATUS_NONZERO -2			/**< CGI program executed, but with non-zero status. Output is provided */
#define ERROR_CGI_PROG_PATH_INVALID -3		/**< CGI program path is invalid (file not found or path points to a directory) */
#define ERROR_CGI_SCRIPT_PATH_INVALID -4	/**< CGI script path is invalid (file not found or path points to a directory) */

#endif
