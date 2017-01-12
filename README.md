# zhttpd
_simple (?) HTTP server in C_

Works only with Linux >= 2.5.44.

Currently supports GET and POST requests **AND** CGI.

CGI support works currently only with PHP5 (php5-cgi). If you want to run a PHP script, just point your browser to a PHP file.

#### TODO:
* Pretty much everything

### Requirements
* cmake
* gcc
* libmagic-dev
* doxygen (optional, for docs)
* php5-cgi ("optional", for CGI)

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
