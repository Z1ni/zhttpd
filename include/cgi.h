#ifndef __CGI_H__
#define __CGI_H__

#include <sys/types.h>
#include <sys/wait.h>

#include "utils.h"
#include "http.h"
#include "errors.h"
#include "http_request_parser.h"

#define PARENT_WRITE_PIPE  0
#define PARENT_READ_PIPE   1

#define READ_FD  0
#define WRITE_FD 1

#define PARENT_READ_FD  ( pipes[PARENT_READ_PIPE][READ_FD]   )
#define PARENT_WRITE_FD ( pipes[PARENT_WRITE_PIPE][WRITE_FD] )

#define CHILD_READ_FD   ( pipes[PARENT_WRITE_PIPE][READ_FD]  )
#define CHILD_WRITE_FD  ( pipes[PARENT_READ_PIPE][WRITE_FD]  )

typedef struct {
	http_request *req;		/**< HTTP Request that performs the CGI call */
	char *script_filename;	/**< Script full path (e.g. "/var/www/script.php") */
} cgi_parameters;

int cgi_exec(const char *path, cgi_parameters *params, unsigned char **out, http_header ***out_headers, size_t *out_header_count);

#endif
