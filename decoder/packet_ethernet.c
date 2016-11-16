#include <string.h>
#include "packet_ethernet.h"
#include "packet_arp.h"
#include "packet_ip.h"
#include "packet_stp.h"

#define LLC_HDR_LEN 3
#define SNAP_HDR_LEN 5

/*
 * Ethernet header
 *
 *       6           6       2
 * +-----------+-----------+---+
 * | Ethernet  | Ethernet  |   |
 * |destination|  source   |FT |
 * |  address  | address   |   |
 * +-----------+-----------+---+
 *
 * FT, the frame type or EtherType, can be used for two different purposes.
 * Values of 1500 and below (Ethernet 802.3) mean that it is used to indicate
 * the size of the payload in bytes, while values of 1536 and above (Ethernet II)
 * indicate that it is used as an EtherType, to indicate which protocol is
 * encapsulated in the payload of the frame.
 *
 * There are several types of 802.3 frames, e.g. 802.2 LLC (Logical Link Control)
 * and 802.2 SNAP (Subnetwork Access Protocol).
 *
 * 802.2 LLC Header
 *
 *     1        1         1
 * +--------+--------+--------+
 * | DSAP=K1| SSAP=K1| Control|
 * +--------+--------+--------+
 *
 * 802.2 SNAP Header
 *
 * When SNAP extension is used, it is located right after the LLC header. The
 * payload start bytes for SNAP is 0xaaaa, which means the K1 value is 0xaa.
 * The control value is 3 (Unnumbered Information).
 *
 * +--------+--------+---------+--------+--------+
 * |Protocol Id or Org Code =K2|    EtherType    |
 * +--------+--------+---------+--------+--------+
 *
 * The K2 value is 0 (zero).
 */
bool handle_ethernet(unsigned char *buffer, int n, struct eth_info *eth)
{
    if (n < ETHERNET_HDRLEN) return false;

    struct ethhdr *eth_header;
    bool error = false;

    eth_header = (struct ethhdr *) buffer;
    memcpy(eth->mac_src, eth_header->h_source, ETH_ALEN);
    memcpy(eth->mac_dst, eth_header->h_dest, ETH_ALEN);
    eth->ethertype = ntohs(eth_header->h_proto);

    /* Ethernet 802.3 frame */
    if (eth->ethertype < ETH_P_802_3_MIN) {
        unsigned char *ptr;

        ptr = buffer + ETH_HLEN;
        eth->llc = calloc(1, sizeof(struct eth_802_llc));
        eth->llc->dsap = ptr[0];
        eth->llc->ssap = ptr[1];
        eth->llc->control = ptr[2];

        /* Spanning Tree Protocol */
        if (eth->llc->dsap == 0x42 && eth->llc->ssap == 0x42) {
            error = !handle_stp(ptr + LLC_HDR_LEN, eth->ethertype - LLC_HDR_LEN, eth->llc);
        } else if (eth->llc->dsap == 0xaa && eth->llc->ssap == 0xaa) {
            /* SNAP extension */
            eth->llc->snap = malloc(sizeof(struct snap_info));
            ptr += LLC_HDR_LEN;
            memcpy(eth->llc->snap->oui, ptr, 3);
            ptr += 3; /* skip first 3 bytes of 802.2 SNAP */
            eth->llc->snap->protocol_id = ptr[0] << 8 | ptr[1];

            /* TODO: If OUI is 0 I need to to handle the internet protocols that
               will be layered on top of SNAP */
            ptr += 2;
            eth->llc->snap->payload_len = eth->ethertype - LLC_HDR_LEN - SNAP_HDR_LEN;
            eth->llc->snap->payload = malloc(eth->llc->snap->payload_len);
            memcpy(eth->llc->snap->payload, ptr, eth->llc->snap->payload_len);
        } else { /* not handled */
            eth->llc->payload_len = eth->ethertype - LLC_HDR_LEN;
            eth->llc->payload = malloc(eth->llc->payload_len);
            memcpy(eth->llc->payload, ptr, eth->llc->payload_len);
        }
    } else {
        switch (eth->ethertype) {
        case ETH_P_IP:
            error = !handle_ip(buffer + ETH_HLEN, n, eth);
            break;
        case ETH_P_ARP:
            error = !handle_arp(buffer + ETH_HLEN, n, eth);
            break;
        case ETH_P_IPV6:
        case ETH_P_PAE:
        default:
            //printf("Ethernet protocol: 0x%x\n", eth->ethertype);
            error = true;
            break;
        }
    }
    if (error) {
        eth->payload_len = n - ETH_HLEN;
        eth->payload = malloc(n - ETH_HLEN);
        memcpy(eth->payload, buffer + ETH_HLEN, n - ETH_HLEN);
    }
    return true;
}

char *get_ethernet_type(uint16_t ethertype)
{
    switch (ethertype) {
    case ETH_P_IP:
        return "IPv4";
    case ETH_P_ARP:
        return "ARP";
    case ETH_P_IPV6:
        return "IPv6";
    case ETH_P_PAE:
        return "Port Access Entity";
    default:
        return NULL;
    }
}

enum eth_802_type get_eth802_type(struct eth_802_llc *llc)
{
    /* DSAP and SSAP specify the upper layer protocols above LLC */
    if (llc->ssap == 0x42 && llc->dsap == 0x42) return ETH_802_STP;
    if (llc->ssap == 0xaa && llc->dsap == 0xaa) return ETH_802_SNAP;

    return ETH_802_UNKNOWN;
}

uint32_t get_eth802_oui(struct snap_info *snap)
{
    return snap->oui[0] << 16 | snap->oui[1] << 8 | snap->oui[2];
}
