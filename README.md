# zhttpd
_simple (?) HTTP server in C_

Works only with Linux >= 2.5.44.

Currently supports GET, POST and HEAD methods **AND** CGI. Supports simple caching by providing Last-Modified and handling If-Modified-Since.

POST method handling doesn't support _multipart/form-data_ at this moment.

CGI support works currently only with PHP (tested with php5-cgi). If you want to run a PHP script, just point your browser to a PHP file.

#### TODO:
* Pretty much everything

### Requirements
* cmake
* gcc
* libmagic-dev
* doxygen (optional, for docs)
* php-cgi ("optional", for CGI)

### Building & Running
```bash
$ mkdir build
$ cd build/
$ cmake ..
$ make
$ ./zhttpd
```

### Creating documentation
```bash
$ cd docs/
$ doxygen
```
After this, see _docs/html/index.html_.
