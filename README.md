# zhttpd
_simple (?) HTTP server in C_

Works only with Linux >= 2.5.44.

Currently reads requests and responds with "501 Not Implemented".

#### TODO:
* Pretty much everything

### Requirements
* cmake
* gcc
* doxygen (optional, for docs)
* libmagic-dev

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
