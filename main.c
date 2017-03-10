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
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#ifdef MACOS
#include <net/if_dl.h>
#include <net/if_types.h>
#endif
#ifdef linux
#include <netpacket/packet.h>
#endif
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include "misc.h"
#include "error.h"
#include "interface.h"
#include "ui/layout.h"
#include "decoder/packet.h"
#include "vector.h"
#include "file_pcap.h"

#include "ui/protocols.h"

linkdef rx; /* data received */
linkdef tx; /* data transmitted */
struct sockaddr_in *local_addr;
char *device = NULL;
bool statistics = false;
vector_t *packets;

static volatile sig_atomic_t signal_flag = 0;
static int sockfd = -1; /* packet socket file descriptor */
static bool use_ncurses = true;
static bool promiscuous = false;
static bool verbose = false;
static context c = { NULL, NULL };

static void print_help(char *prg);
static void init_socket(char *device);
static void init_structures();
static void run();
static void sig_alarm(int signo);
static void sig_int(int signo);
static void calculate_rate();

int main(int argc, char **argv)
{
    char *prg_name = argv[0];
    int opt;

    while ((opt = getopt(argc, argv, "i:r:lhvpst")) != -1) {
        switch (opt) {
        case 'i':
            c.device = strdup(optarg);
            break;
        case 'l':
            list_interfaces();
            exit(0);
            break;
        case 'p':
            promiscuous = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 's':
            statistics = true;
            break;
        case 'r':
            c.filename = strdup(optarg);
            break;
        case 't':
            use_ncurses = false;
            break;
        case 'h':
        default:
            print_help(prg_name);
            exit(0);
        }
    }

#ifdef linux
    init_structures();
    if (!c.device && !(c.device = get_default_interface())) {
        err_quit("Cannot find active network device");
    }
    local_addr = malloc(sizeof (struct sockaddr_in));
    get_local_address(c.device, (struct sockaddr *) local_addr);
    if (c.filename) {
        enum file_error err;

        err = read_file(c.filename);
        if (err != NO_ERROR) {
            err_quit("Error in file: %s", c.filename);
        }
    } else {
        init_socket(c.device);
    }
    if (use_ncurses) {
        init_ncurses();
        create_layout(&c);
    }
    if (c.filename) {
        if (use_ncurses) {
            print_file();
        } else {
            for (int i = 0; i < vector_size(packets); i++) {
                char buf[MAXLINE];

                print_buffer(buf, MAXLINE, vector_get_data(packets, i));
                printf("%s\n", buf);
            }
            finish();
        }
    }
    run();
    finish();
#endif
}

void print_help(char *prg)
{
    printf("Usage: %s [-lvhpst] [-i interface] [-r path]\n", prg);
    printf("Options:\n");
    printf("     -i  Specify network interface\n");
    printf("     -l  List available interfaces\n");
    printf("     -p  Use promiscuous mode\n");
    printf("     -s  Show statistics page\n");
    printf("     -v  Print verbose information\n");
    printf("     -r  Read file in pcap format\n");
    printf("     -t  Use normal text output, i.e. don't use ncurses\n");
    printf("     -h  Print this help summary\n");
}

void sig_alarm(int signo)
{
    signal_flag = 1;
}

void sig_int(int signo)
{
    finish();
}

void finish()
{
    if (use_ncurses) {
        end_ncurses();
    }
    vector_clear(packets);
    free(c.device);
    free(c.filename);
    free(local_addr);
    if (sockfd > 0) {
        close(sockfd);
    }
    exit(0);
}

/* Initialize device and prepare for reading */
void init_socket(char *device)
{
    int flag;
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

    setuid(getuid()); /* no need for root access anymore */
    memset(&rx, 0, sizeof(linkdef));
    memset(&tx, 0, sizeof(linkdef));
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
    if (use_ncurses || c.filename) {
        packets = vector_init(1000, free_packet);
    }
}

/* The main event loop */
void run()
{
    struct pollfd fds[] = {
        { sockfd, POLLIN },
        { STDIN_FILENO, POLLIN }
    };

    if (statistics) alarm(1);
    while (1) {
        if (signal_flag) {
            signal_flag = 0;
            alarm(1);
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
                    print_packet(p);
                } else {
                    char buf[MAXLINE];

                    print_buffer(buf, MAXLINE, p);
                    printf("%s\n", buf);
                }
            }
        }
        if (fds[1].revents & POLLIN) {
            get_input();
        }
    }
}

// TODO: Move this
void calculate_rate()
{
    rx.kbps = (rx.tot_bytes - rx.prev_bytes) / 1024;
    tx.kbps = (tx.tot_bytes - tx.prev_bytes) / 1024;
    rx.prev_bytes = rx.tot_bytes;
    tx.prev_bytes = tx.tot_bytes;
}
