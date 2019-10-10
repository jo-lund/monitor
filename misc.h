#ifndef MISC_H
#define MISC_H

// TODO: Clean up this file

#include <stdlib.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <limits.h>
#include <GeoIP.h>

#define GEOIP_PATH "/usr/share/GeoIP/GeoIPCity.dat"

/*
 * Only a portion of each packet is passed by the kernel to the application, this
 * size is the snapshot length or the snaplen.
 */
#define SNAPLEN 65535

/*
 * Timeout value that decides when BPF copies its buffer to the application. A
 * timeout value of 0 means that the application wants it as soon as BPF receives
 * the packet.
 */
#define TIME_TO_WAIT 0

#define MAXLINE 1000

#ifdef PATH_MAX
#define MAXPATH PATH_MAX
#else
#define MAXPATH 1024
#endif

typedef struct {
    char *device;
    char filename[MAXPATH + 1];
    bool capturing;
    struct options {
        bool show_statistics;
        bool nogeoip;
        bool use_ncurses;
        bool nopromiscuous;
        bool verbose;
        bool load_file;
    } opt;
    GeoIP *gi;
    struct sockaddr_in *local_addr;
    unsigned char mac[ETHER_ADDR_LEN];
} main_context;

enum event {
    NEW_PACKET,
    ALARM
};

extern main_context ctx;

void finish(int status);
void stop_scan();
void start_scan();

#endif
