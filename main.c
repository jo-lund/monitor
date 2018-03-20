/* Network traffic monitor
 *
 * This program will monitor all incoming/outgoing network traffic and
 * give a log of the packets on the network.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#ifdef __linux__
#include <netpacket/packet.h>
#endif
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include "misc.h"
#include "error.h"
#include "interface.h"
#include "ui/layout.h"
#include "decoder/packet.h"
#include "decoder/tcp_analyzer.h"
#include "vector.h"
#include "file_pcap.h"
#include "ui/protocols.h"

#define TABLE_SIZE 65536

struct sockaddr_in *local_addr;
bool statistics = false;
vector_t *packets;
main_context ctx;
static volatile sig_atomic_t signal_flag = 0;
static int sockfd = -1;
static bool use_ncurses = true;
static bool promiscuous = false;
static bool verbose = false;
static bool load_file = false;
static bool fd_changed = false;
static const char *geoip_path = "/usr/share/GeoIP/GeoIPCity.dat";

bool on_packet(unsigned char *buffer, uint32_t n, struct timeval *t);
static void print_help(char *prg);
static void init_socket(char *device);
static void init_structures();
static void run();
static void sig_alarm(int signo __attribute__((unused)));
static void sig_int(int signo __attribute__((unused)));

int main(int argc, char **argv)
{
    char *prg_name = argv[0];
    int opt;
    int idx;
    static struct option long_options[] = {
        { "interface", required_argument, 0, 'i' },
        { "help", no_argument, 0, 'h' },
        { "list-interfaces", no_argument, 0, 'l' },
        { "no-geoip", no_argument, 0, 'G' },
        { "statistics", no_argument, 0, 's' },
        { "verbose", no_argument, 0, 'v' },
        { 0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:r:Ghlpstv",
                              long_options, &idx)) != -1) {
        switch (opt) {
        case 'G':
            ctx.nogeoip = true;
            break;
        case 'i':
            ctx.device = strdup(optarg);
            break;
        case 'l':
            list_interfaces();
            exit(0);
            break;
        case 'p':
            promiscuous = true;
            break;
        case 'r':
            strcpy(ctx.filename, optarg);
            load_file = true;
            break;
        case 's':
            ctx.show_statistics = true;
            break;
        case 't':
            use_ncurses = false;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
        default:
            print_help(prg_name);
            exit(0);
        }
    }

#ifdef __linux__
    init_structures();
    analyzer_init();
    if (!ctx.device && !(ctx.device = get_default_interface())) {
        err_quit("Cannot find active network device");
    }
    local_addr = malloc(sizeof (struct sockaddr_in));
    get_local_address(ctx.device, (struct sockaddr *) local_addr);
    if (!ctx.nogeoip && !(ctx.gi = GeoIP_open(geoip_path, GEOIP_STANDARD))) {
        exit(1);
    }
    if (load_file) {
        enum file_error err;
        FILE *fp;

        ctx.capturing = false;
        if ((fp = open_file(ctx.filename, "r", &err)) == NULL) {
            err_sys("Error in %s", ctx.filename);
        }
        if ((err = read_file(fp, on_packet)) != NO_ERROR) {
            fclose(fp);
            err_quit("Error in %s: %s", ctx.filename, get_file_error(err));
        }
        fclose(fp);
        if (use_ncurses) {
            init_ncurses(&ctx);
            print_file();
        } else {
            for (int i = 0; i < vector_size(packets); i++) {
                char buf[MAXLINE];

                write_to_buf(buf, MAXLINE, vector_get_data(packets, i));
                printf("%s\n", buf);
            }
            finish();
        }
    } else {
        ctx.capturing = true;
        init_socket(ctx.device);
        if (use_ncurses) {
            init_ncurses(&ctx);
        }
    }
    run();
    finish();
#endif
}

void print_help(char *prg)
{
    printf("Usage: %s [-lvhpstG] [-i interface] [-r path]\n", prg);
    printf("Options:\n");
    printf("     -G, --no-geoip         Don't use GeoIP information\n");
    printf("     -i, --interface        Specify network interface\n");
    printf("     -l, --list-interfaces  List available interfaces\n");
    printf("     -p                     Use promiscuous mode\n");
    printf("     -r                     Read file in pcap format\n");
    printf("     -s, --statistics       Show statistics page\n");
    printf("     -t                     Use normal text output, i.e. don't use ncurses\n");
    printf("     -v, --verbose          Print verbose information\n");
    printf("     -h                     Print this help summary\n");
}

void sig_alarm(int signo __attribute__((unused)))
{
    signal_flag = 1;
}

void sig_int(int signo __attribute__((unused)))
{
    finish();
}

void finish()
{
    if (use_ncurses) {
        end_ncurses();
        vector_free(packets, free_packet);
    }
    free(ctx.device);
    free(local_addr);
    if (sockfd > 0) {
        close(sockfd);
    }
    analyzer_free();
    if (ctx.gi) {
        GeoIP_delete(ctx.gi);
    }
    exit(0);
}

/* Initialize device and prepare for reading */
void init_socket(char *device)
{
    int flag;
    int n = 1;
    struct sockaddr_ll ll_addr; /* device independent physical layer address */

    /* SOCK_RAW packet sockets include the link level header */
    if ((sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
        err_sys("socket error");
    }

    /* use non-blocking socket */
    if ((flag = fcntl(sockfd, F_GETFL, 0)) == -1) {
        err_sys("fcntl error");
    }
    if (fcntl(sockfd, F_SETFL, flag | O_NONBLOCK) == -1) {
        err_sys("fcntl error");
    }

    /* get timestamps */
    if (setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMP, &n, sizeof(n)) == -1) {
        err_sys("setsockopt error");
    }

    memset(&ll_addr, 0, sizeof(ll_addr));
    ll_addr.sll_family = PF_PACKET;
    ll_addr.sll_protocol = htons(ETH_P_ALL);
    ll_addr.sll_ifindex = get_interface_index(device);

    /* only receive packets on the specified interface */
    if (bind(sockfd, (struct sockaddr *) &ll_addr, sizeof(ll_addr)) == -1) {
        err_sys("bind error");
    }
}

void init_structures()
{
    struct sigaction act;

    /* set up an alarm and interrupt signal handler */
    act.sa_handler = sig_alarm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        err_sys("sigaction error");
    }
    act.sa_handler = sig_int;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, NULL) == -1) {
        err_sys("sigaction error");
    }

    /* Initialize table to store packets */
    if (use_ncurses || load_file) {
        packets = vector_init(TABLE_SIZE);
    }
}

void run()
{
    struct pollfd fds[] = {
        { sockfd, POLLIN, 0 },
        { STDIN_FILENO, POLLIN, 0 }
    };

    while (1) {
        if (signal_flag) {
            signal_flag = 0;
            layout(ALARM);
            alarm(1);
        }
        if (fd_changed) {
            fds[0].fd = sockfd;
            fd_changed = false;
        }
        if (poll(fds, 2, -1) == -1) {
            if (errno == EINTR) continue;
            err_sys("poll error");
        }
        if (fds[0].revents & POLLIN) {
            unsigned char buffer[SNAPLEN];
            size_t n;
            struct packet *p;

            n = read_packet(sockfd, buffer, SNAPLEN, &p);
            if (n) {
                if (use_ncurses) {
                    vector_push_back(packets, p);
                    layout(NEW_PACKET);
                } else {
                    char buf[MAXLINE];

                    write_to_buf(buf, MAXLINE, p);
                    printf("%s\n", buf);
                    free_packet(p);
                }
            }
        }
        if (fds[1].revents & POLLIN) {
            handle_input();
        }
    }
}

void stop_scan()
{
    close(sockfd);
    sockfd = -1;
    fd_changed = true;
}

void start_scan()
{
    clear_statistics();
    vector_clear(packets, free_packet);
    init_socket(ctx.device);
    fd_changed = true;
}

bool on_packet(unsigned char *buffer, uint32_t n, struct timeval *t)
{
    struct packet *p;

    if (!decode_packet(buffer, n, &p)) {
        return false;
    }
    p->time.tv_sec = t->tv_sec;
    p->time.tv_usec = t->tv_usec;
    vector_push_back(packets, p);
    return true;
}
