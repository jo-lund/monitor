#include <ifaddrs.h>
#include <string.h>
#include <sys/types.h>
#include <net/if_dl.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <net/bpf.h>
#include <sys/socket.h>
#include <net/if.h>
#include <fcntl.h>
#include <unistd.h>
#include <machine/atomic.h>
#include "../interface.h"
#include "../monitor.h"
#include "../error.h"

#define NUM_BUFS 2
#define BUFSIZE 65536

static void bsd_activate(iface_handle_t *handle, char *dev, struct bpf_prog *bpf);
static void bsd_close(iface_handle_t *handle);
static void bsd_read_packet_zbuf(iface_handle_t *handle);
static void bsd_read_packet_buffer(iface_handle_t *handle);
static void bsd_set_promiscuous(iface_handle_t *handle, char *dev, bool enable);

static unsigned char buffers[NUM_BUFS][BUFSIZE];

static struct iface_operations bsd_op = {
    .activate = bsd_activate,
    .close = bsd_close,
    .read_packet = bsd_read_packet_zbuf,
    .set_promiscuous = bsd_set_promiscuous,
};

/*
 * Return ownership of a buffer to the kernel for reuse.
 */
static inline void buffer_acknowledge(struct bpf_zbuf_header *bzh)
{
    atomic_store_rel_int(&bzh->bzh_user_gen, bzh->bzh_kernel_gen);
}

/*
 * Check whether a buffer has been assigned to userspace by the kernel.
 * Return true if userspace owns the buffer, and false otherwise.
 */
static inline bool buffer_check(struct bpf_zbuf_header *bzh)
{
    return bzh->bzh_user_gen != atomic_load_acq_int(&bzh->bzh_kernel_gen);
}

iface_handle_t *iface_handle_create(unsigned char *buf, size_t len, packet_handler fn)
{
    iface_handle_t *handle = calloc(1, sizeof(iface_handle_t));

    handle->fd = -1;
    handle->op = &bsd_op;
    handle->buf = buf;
    handle->len = len;
    handle->on_packet = fn;
    return handle;
}

void bsd_activate(iface_handle_t *handle, char *dev, struct bpf_prog *bpf UNUSED)
{
    struct ifreq ifr;
    struct bpf_zbuf zbuf;
    unsigned int mode, imm;

    if ((handle->fd = open("/dev/bpf", O_RDONLY)) < 0)
        err_sys("%s: open error", __func__);

    /* use zero-copy buffer mode if supported */
    mode = BPF_BUFMODE_ZBUF;
    if (ioctl(handle->fd, BIOCSETBUFMODE, &mode) != -1) {
        zbuf.bz_bufa = buffers[0];
        zbuf.bz_bufb = buffers[1];
        zbuf.bz_buflen = BUFSIZE;
        if (ioctl(handle->fd, BIOCSETZBUF, &zbuf) == -1)
            err_sys("ioctl error BIOCSETZBUF");
        handle->use_zerocopy = true;
    } else {
        DEBUG("Failed setting zero-copy mode");
        handle->op->read_packet = bsd_read_packet_buffer;
        mode = BPF_BUFMODE_BUFFER;
        if (ioctl(handle->fd, BIOCSETBUFMODE, &mode) == -1)
            err_sys("ioctl error BIOCSETBUFMODE");

        /* Enable immediate mode. When immediate mode is enabled, reads return
           immediatly upon packet reception */
        imm = 1;
        if (ioctl(handle->fd, BIOCIMMEDIATE, &imm) == -1)
            err_sys("ioctl error BIOCIMMEDIATE");

        /* set buffer length */
        if (ioctl(handle->fd, BIOCSBLEN, &handle->len) == -1)
            err_sys("ioctl error BIOCSBLEN");
    }
    /* set the hardware interface associated with the file */
    strncpy(ifr.ifr_name, dev, IFNAMSIZ - 1);
    ifr.ifr_addr.sa_family = AF_INET;
    if (ioctl(handle->fd, BIOCSETIF, &ifr) == -1)
        err_sys("ioctl error BIOCSETIF");

    /* get link type */
    if (ioctl(handle->fd, BIOCGDLT, &handle->linktype) < 0)
        err_sys("ioctl error BIOCGDLT");
    if (handle->linktype != LINKTYPE_ETHERNET)
        err_sys("Link type not supported");
}

void bsd_close(iface_handle_t *handle)
{
    close(handle->fd);
    handle->fd = -1;
}

void bsd_read_packet_zbuf(iface_handle_t *handle)
{
    unsigned int zbuf_header_len = sizeof(struct bpf_zbuf_header);
    struct bpf_zbuf_header *zhdr;
    unsigned char *p;
    struct bpf_hdr *hdr;

    for (int i = 0; i < NUM_BUFS; i++) {
        if (buffer_check((struct bpf_zbuf_header *) buffers[i])) {
            zhdr = (struct bpf_zbuf_header *) buffers[i];
            p = buffers[i] + zbuf_header_len;
            while (p < buffers[i] + zhdr->bzh_kernel_len) {
                hdr = (struct bpf_hdr *) p;
                handle->buf = p + hdr->bh_hdrlen;
                handle->on_packet(handle, handle->buf, hdr->bh_caplen, &hdr->bh_tstamp);
                p += BPF_WORDALIGN(hdr->bh_hdrlen + hdr->bh_caplen);
            }
            buffer_acknowledge((struct bpf_zbuf_header *) buffers[i]);
        }
    }
}

void bsd_read_packet_buffer(iface_handle_t *handle)
{
    struct bpf_hdr *hdr;
    ssize_t n;
    unsigned char *p;

    if ((n = read(handle->fd, handle->buf, handle->len)) < 0)
        err_sys("read error");
    p = handle->buf;
    while (p < handle->buf + n) {
        hdr = (struct bpf_hdr *) p;
        handle->on_packet(handle, p + hdr->bh_hdrlen, hdr->bh_caplen, &hdr->bh_tstamp);
        p += BPF_WORDALIGN(hdr->bh_hdrlen + hdr->bh_caplen);
    }
}

void bsd_set_promiscuous(iface_handle_t *handle, char *dev UNUSED, bool enable)
{
    /* the interface remains in promiscuous mode until all files listening
       promiscuously are closed */
    if (!enable)
        return;

    if (ioctl(handle->fd, BIOCPROMISC, NULL) == -1) {
        err_sys("ioctl error");
    }
}

void get_local_mac(char *dev UNUSED, unsigned char *mac)
{
     struct ifaddrs *ifp, *ifhead;

    if (getifaddrs(&ifp) == -1)
        err_sys("getifaddrs error");
    ifhead = ifp;
    while (ifp) {
        if (ifp->ifa_addr->sa_family == AF_LINK) {
            struct sockaddr_dl *dl_addr;

            dl_addr = (struct sockaddr_dl *) ifp->ifa_addr;
            memcpy(mac, (char *) LLADDR(dl_addr), dl_addr->sdl_alen);
            break;
        }
    }
    freeifaddrs(ifhead);
}

bool is_wireless(char *dev UNUSED)
{
    return false;
}
