#include "x11_server.h"
#include <pthread.h>
#include <unistd.h>

static pthread_t server_thread;
static int running = 0;

static void* server_loop(void*) {
    while (running) {
        // TODO: X11 protocol handling
        usleep(16000);
    }
    return 0;
}

void x11_server_start(void) {
    running = 1;
    pthread_create(&server_thread, 0, server_loop, 0);
}

void x11_server_pause(void) {
    running = 0;
}

void x11_server_resume(void) {
    if (!running) {
        x11_server_start();
    }
}
