#include "x11_server.h"
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#define X11_SOCKET_DIR_SUFFIX "/tmp/.X11-unix"
#define X11_SOCKET_FILE "/X0"
#define TERMUX_PREFIX "/data/data/com.termux/files/usr"
#define X11_COOKIE_ENV "X11_COOKIE"

static int hex_to_byte(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static int parse_cookie_hex(const char* hex, uint8_t* out, size_t out_len) {
    size_t hex_len;
    size_t i;

    if (!hex || !out) {
        return -1;
    }

    hex_len = strlen(hex);
    if (hex_len != out_len * 2) {
        return -1;
    }

    for (i = 0; i < out_len; i++) {
        int hi = hex_to_byte(hex[i * 2]);
        int lo = hex_to_byte(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return 0;
}

static int read_exact(int fd, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t got = read(fd, out + off, len - off);
        if (got == 0) {
            return 0;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)got;
    }
    return 1;
}

static int write_exact(int fd, const void* buf, size_t len) {
    const uint8_t* in = (const uint8_t*)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t sent = write(fd, in + off, len - off);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)sent;
    }
    return 1;
}

static uint16_t read_u16(const uint8_t* buf, int little_endian) {
    if (little_endian) {
        return (uint16_t)(buf[0] | (buf[1] << 8));
    }
    return (uint16_t)(buf[1] | (buf[0] << 8));
}

static uint32_t read_u32(const uint8_t* buf, int little_endian) {
    if (little_endian) {
        return (uint32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
    }
    return (uint32_t)(buf[3] | (buf[2] << 8) | (buf[1] << 16) | (buf[0] << 24));
}

static void write_u16(uint8_t* buf, uint16_t value, int little_endian) {
    if (little_endian) {
        buf[0] = (uint8_t)(value & 0xff);
        buf[1] = (uint8_t)((value >> 8) & 0xff);
    } else {
        buf[1] = (uint8_t)(value & 0xff);
        buf[0] = (uint8_t)((value >> 8) & 0xff);
    }
}

static void write_u32(uint8_t* buf, uint32_t value, int little_endian) {
    if (little_endian) {
        buf[0] = (uint8_t)(value & 0xff);
        buf[1] = (uint8_t)((value >> 8) & 0xff);
        buf[2] = (uint8_t)((value >> 16) & 0xff);
        buf[3] = (uint8_t)((value >> 24) & 0xff);
    } else {
        buf[3] = (uint8_t)(value & 0xff);
        buf[2] = (uint8_t)((value >> 8) & 0xff);
        buf[1] = (uint8_t)((value >> 16) & 0xff);
        buf[0] = (uint8_t)((value >> 24) & 0xff);
    }
}

static int send_setup_success(int fd, int little_endian) {
    const char* vendor = "x11server";
    const uint16_t vendor_len = (uint16_t)strlen(vendor);
    const uint16_t pixmap_format_count = 1;
    const uint16_t screen_count = 1;

    const uint16_t vendor_pad = (uint16_t)((4 - (vendor_len % 4)) % 4);
    const uint16_t pixmap_format_len = 8;
    const uint16_t screen_len = 40;
    const uint16_t depth_len = 8;
    const uint16_t visual_len = 24;
    const uint16_t depth_count = 1;
    const uint16_t visual_count = 1;

    const uint16_t setup_len_bytes =
        32 + vendor_len + vendor_pad + (pixmap_format_count * pixmap_format_len) +
        screen_len + depth_len + (visual_count * visual_len);
    const uint16_t setup_len_words = (uint16_t)(setup_len_bytes / 4);

    uint8_t header[8];
    memset(header, 0, sizeof(header));
    header[0] = 1;
    write_u16(header + 2, 11, little_endian);
    write_u16(header + 4, 0, little_endian);
    write_u16(header + 6, setup_len_words, little_endian);

    uint8_t setup[32];
    memset(setup, 0, sizeof(setup));
    write_u32(setup + 0, 1, little_endian);
    write_u32(setup + 4, 0x10000000, little_endian);
    write_u32(setup + 8, 0x1fffff, little_endian);
    write_u32(setup + 12, 0, little_endian);
    write_u16(setup + 16, vendor_len, little_endian);
    write_u16(setup + 18, 65535, little_endian);
    setup[20] = (uint8_t)screen_count;
    setup[21] = (uint8_t)pixmap_format_count;
    setup[22] = 0;
    setup[23] = 0;
    setup[24] = 0;
    setup[25] = 32;
    setup[26] = 32;
    setup[27] = 32;
    setup[28] = 8;
    setup[29] = 255;

    uint8_t pixmap_format[8];
    memset(pixmap_format, 0, sizeof(pixmap_format));
    pixmap_format[0] = 24;
    pixmap_format[1] = 32;
    pixmap_format[2] = 32;

    uint8_t screen[40];
    memset(screen, 0, sizeof(screen));
    write_u32(screen + 0, 1, little_endian);
    write_u32(screen + 4, 32, little_endian);
    write_u32(screen + 8, 0xffffff, little_endian);
    write_u32(screen + 12, 0x000000, little_endian);
    write_u32(screen + 16, 0, little_endian);
    write_u16(screen + 20, 1024, little_endian);
    write_u16(screen + 22, 768, little_endian);
    write_u16(screen + 24, 270, little_endian);
    write_u16(screen + 26, 203, little_endian);
    write_u16(screen + 28, 1, little_endian);
    write_u16(screen + 30, 1, little_endian);
    write_u32(screen + 32, 33, little_endian);
    screen[36] = 0;
    screen[37] = 0;
    screen[38] = 24;
    screen[39] = (uint8_t)depth_count;

    uint8_t depth[8];
    memset(depth, 0, sizeof(depth));
    depth[0] = 24;
    write_u16(depth + 2, visual_count, little_endian);

    uint8_t visual[24];
    memset(visual, 0, sizeof(visual));
    write_u32(visual + 0, 33, little_endian);
    visual[4] = 4;
    visual[5] = 8;
    write_u16(visual + 6, 256, little_endian);
    write_u32(visual + 8, 0xff0000, little_endian);
    write_u32(visual + 12, 0x00ff00, little_endian);
    write_u32(visual + 16, 0x0000ff, little_endian);
    visual[20] = 16;
    visual[21] = 8;

    if (write_exact(fd, header, sizeof(header)) <= 0) {
        return -1;
    }
    if (write_exact(fd, setup, sizeof(setup)) <= 0) {
        return -1;
    }
    if (write_exact(fd, vendor, vendor_len) <= 0) {
        return -1;
    }
    if (vendor_pad) {
        uint8_t padding[4] = {0, 0, 0, 0};
        if (write_exact(fd, padding, vendor_pad) <= 0) {
            return -1;
        }
    }
    if (write_exact(fd, pixmap_format, sizeof(pixmap_format)) <= 0) {
        return -1;
    }
    if (write_exact(fd, screen, sizeof(screen)) <= 0) {
        return -1;
    }
    if (write_exact(fd, depth, sizeof(depth)) <= 0) {
        return -1;
    }
    if (write_exact(fd, visual, sizeof(visual)) <= 0) {
        return -1;
    }

    return 0;
}

static int handle_client_requests(int fd, int little_endian) {
    uint16_t sequence = 0;
    uint8_t header[4];

    while (1) {
        int got = read_exact(fd, header, sizeof(header));
        if (got <= 0) {
            return -1;
        }

        uint8_t opcode = header[0];
        uint16_t length = read_u16(header + 2, little_endian);
        uint32_t payload_len = 0;
        if (length > 1) {
            payload_len = (uint32_t)(length - 1) * 4;
            uint8_t discard[256];
            while (payload_len > 0) {
                size_t chunk = payload_len > sizeof(discard) ? sizeof(discard) : payload_len;
                if (read_exact(fd, discard, chunk) <= 0) {
                    return -1;
                }
                payload_len -= (uint32_t)chunk;
            }
        }

        sequence++;

        if (opcode == 43) {
            uint8_t reply[32];
            memset(reply, 0, sizeof(reply));
            reply[0] = 1;
            reply[1] = 0;
            write_u16(reply + 2, sequence, little_endian);
            write_u32(reply + 4, 0, little_endian);
            write_u32(reply + 8, 1, little_endian);
            if (write_exact(fd, reply, sizeof(reply)) <= 0) {
                return -1;
            }
        } else if (opcode == 38) {
            uint8_t reply[32];
            memset(reply, 0, sizeof(reply));
            reply[0] = 1;
            reply[1] = 1;
            write_u16(reply + 2, sequence, little_endian);
            write_u32(reply + 4, 0, little_endian);
            write_u32(reply + 8, 1, little_endian);
            write_u32(reply + 12, 0, little_endian);
            write_u16(reply + 16, 0, little_endian);
            write_u16(reply + 18, 0, little_endian);
            write_u16(reply + 20, 0, little_endian);
            write_u16(reply + 22, 0, little_endian);
            write_u16(reply + 24, 0, little_endian);
            if (write_exact(fd, reply, sizeof(reply)) <= 0) {
                return -1;
            }
        } else if (opcode == 14) {
            uint8_t reply[32];
            memset(reply, 0, sizeof(reply));
            reply[0] = 1;
            reply[1] = 24;
            write_u16(reply + 2, sequence, little_endian);
            write_u32(reply + 4, 0, little_endian);
            write_u32(reply + 8, 1, little_endian);
            write_u16(reply + 12, 1024, little_endian);
            write_u16(reply + 14, 768, little_endian);
            write_u16(reply + 16, 0, little_endian);
            write_u16(reply + 18, 0, little_endian);
            write_u16(reply + 20, 0, little_endian);
            if (write_exact(fd, reply, sizeof(reply)) <= 0) {
                return -1;
            }
        }
    }
}

static int handle_client(int fd) {
    uint8_t setup_header[12];
    if (read_exact(fd, setup_header, sizeof(setup_header)) <= 0) {
        return -1;
    }

    int little_endian = (setup_header[0] == 'l');
    if (!little_endian && setup_header[0] != 'B') {
        return -1;
    }

    uint16_t major = read_u16(setup_header + 2, little_endian);
    uint16_t minor = read_u16(setup_header + 4, little_endian);
    uint16_t auth_name_len = read_u16(setup_header + 6, little_endian);
    uint16_t auth_data_len = read_u16(setup_header + 8, little_endian);
    if (major != 11 || minor != 0) {
        return -1;
    }

    {
        const char* cookie_hex = getenv(X11_COOKIE_ENV);
        const char* expected_auth_name = "MIT-MAGIC-COOKIE-1";
        const size_t expected_cookie_len = 16;
        uint8_t expected_cookie[16];

        if (!cookie_hex) {
            return -1;
        }
        if (parse_cookie_hex(cookie_hex, expected_cookie, expected_cookie_len) != 0) {
            return -1;
        }

        if (auth_name_len != strlen(expected_auth_name) || auth_data_len != expected_cookie_len) {
            return -1;
        }

        {
            size_t auth_name_pad = (4 - (auth_name_len % 4)) % 4;
            size_t auth_data_pad = (4 - (auth_data_len % 4)) % 4;
            size_t auth_total = (size_t)auth_name_len + auth_name_pad + (size_t)auth_data_len + auth_data_pad;
            uint8_t* auth_buf = 0;

            if (auth_total == 0) {
                return -1;
            }

            auth_buf = (uint8_t*)malloc(auth_total);
            if (!auth_buf) {
                return -1;
            }

            if (read_exact(fd, auth_buf, auth_total) <= 0) {
                free(auth_buf);
                return -1;
            }

            if (memcmp(auth_buf, expected_auth_name, auth_name_len) != 0) {
                free(auth_buf);
                return -1;
            }
            if (memcmp(auth_buf + auth_name_len + auth_name_pad, expected_cookie, expected_cookie_len) != 0) {
                free(auth_buf);
                return -1;
            }

            free(auth_buf);
        }
    }

    if (send_setup_success(fd, little_endian) != 0) {
        return -1;
    }

    return handle_client_requests(fd, little_endian);
}

static pthread_t server_thread;
static int running = 0;
static int thread_started = 0;
static int server_fd = -1;
static int tcp_fd = -1;

#define LOG_TAG "X11"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static int setup_server_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    const char* prefix = getenv("PREFIX");
    const char* base_dir = X11_SOCKET_DIR_SUFFIX;
    char socket_dir[sizeof(addr.sun_path)];
    char socket_path[sizeof(addr.sun_path)];

    if (prefix && prefix[0] != '\0') {
        size_t needed = strlen(prefix) + strlen(X11_SOCKET_DIR_SUFFIX) + 1;
        if (needed <= sizeof(socket_dir)) {
            snprintf(socket_dir, sizeof(socket_dir), "%s%s", prefix, X11_SOCKET_DIR_SUFFIX);
            base_dir = socket_dir;
        }
    } else if (access(TERMUX_PREFIX X11_SOCKET_DIR_SUFFIX, F_OK) == 0) {
        base_dir = TERMUX_PREFIX X11_SOCKET_DIR_SUFFIX;
    }

    if (snprintf(socket_path, sizeof(socket_path), "%s%s", base_dir, X11_SOCKET_FILE) <= 0) {
        close(fd);
        return -1;
    }

    mkdir(base_dir, 0777);
    chmod(base_dir, 01777);
    unlink(socket_path);
    size_t path_len = strlen(socket_path);
    if (path_len + 1 > sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    memcpy(addr.sun_path, socket_path, path_len + 1);
    socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);

    if (bind(fd, (struct sockaddr*)&addr, addr_len) != 0) {
        LOGE("bind unix socket failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 8) != 0) {
        LOGE("listen unix socket failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOGI("listening on unix socket: %s", socket_path);

    return fd;
}

static int setup_tcp_socket(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE("socket tcp failed: %s", strerror(errno));
        return -1;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        LOGE("bind tcp 127.0.0.1:6000 failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 8) != 0) {
        LOGE("listen tcp 127.0.0.1:6000 failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOGI("listening on tcp 127.0.0.1:6000");

    return fd;
}

static void* server_loop(void* arg) {
    (void)arg;
    server_fd = setup_server_socket();
    tcp_fd = setup_tcp_socket();
    if (server_fd < 0 && tcp_fd < 0) {
        running = 0;
        return 0;
    }

    while (running) {
        fd_set read_set;
        FD_ZERO(&read_set);
        int max_fd = -1;
        if (server_fd >= 0) {
            FD_SET(server_fd, &read_set);
            max_fd = server_fd;
        }
        if (tcp_fd >= 0) {
            FD_SET(tcp_fd, &read_set);
            if (tcp_fd > max_fd) {
                max_fd = tcp_fd;
            }
        }

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;

        if (max_fd < 0) {
            continue;
        }

        int ready = select(max_fd + 1, &read_set, 0, 0, &timeout);
        if (ready > 0) {
            if (server_fd >= 0 && FD_ISSET(server_fd, &read_set)) {
                int client_fd = accept(server_fd, 0, 0);
                if (client_fd >= 0) {
                    handle_client(client_fd);
                    close(client_fd);
                }
            }
            if (tcp_fd >= 0 && FD_ISSET(tcp_fd, &read_set)) {
                int client_fd = accept(tcp_fd, 0, 0);
                if (client_fd >= 0) {
                    handle_client(client_fd);
                    close(client_fd);
                }
            }
        }
    }

    if (server_fd >= 0) {
        close(server_fd);
    }
    server_fd = -1;
    if (tcp_fd >= 0) {
        close(tcp_fd);
    }
    tcp_fd = -1;
    return 0;
}

void x11_server_start(void) {
    if (running) {
        return;
    }
    running = 1;
    if (pthread_create(&server_thread, 0, server_loop, 0) == 0) {
        thread_started = 1;
    } else {
        running = 0;
    }
}

void x11_server_pause(void) {
    if (!thread_started) {
        running = 0;
        return;
    }
    running = 0;
    pthread_join(server_thread, 0);
    thread_started = 0;
}

void x11_server_resume(void) {
    if (!running) {
        x11_server_start();
    }
}
