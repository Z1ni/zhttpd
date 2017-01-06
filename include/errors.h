#ifndef __ERRORS_H__
#define __ERRORS_H__

/**
 * Errors for \ref http_header
 */
#define ERROR_HEADER_CREATE_FAILED -1

/**
 * Errors for \ref http_response
 */
#define ERROR_RESPONSE_STRING_CREATE_FAILED -1		// Creating response string failed
#define ERROR_RESPONSE_ARGUMENT -2					// Invalid argument
#define ERROR_RESPONSE_SET_CONTENT_TYPE_FAILED -3	// Content-Type setting failed

/**
 * Errors for \ref http_request_parse
 */
#define ERROR_PARSER_MALFORMED_REQUEST -1
#define ERROR_PARSER_INVALID_METHOD -2
#define ERROR_PARSER_URI_TOO_LONG -3
#define ERROR_PARSER_UNSUPPORTED_PROTOCOL -4

#endif