#ifndef CLIENT_H
#define CLIENT_H

#include <gio/gio.h>

int run_client(GSocket* sock, char** commands, int argc, char** argv);

#endif
