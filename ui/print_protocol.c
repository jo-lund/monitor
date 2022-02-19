#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <netinet/igmp.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include "print_protocol.h"
#include "decoder/decoder.h"
#include "util.h"
#include "string.h"
#include "misc.h"

#define HOSTNAMELEN 255 /* maximum 255 according to rfc1035 */
#define TBUFLEN 16

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define PRINT_NUMBER(buffer, n, i)                  \
    snprintf(buffer, n, "%-" STR(NUM_WIDTH) "u", i)
#define PRINT_TIME(buffer, n, t)                    \
    snprintcat(buffer, n, "%-" STR(TIME_WIDTH) "s", t)
#define PRINT_ADDRESS(buffer, n, src, dst)                              \
    snprintcat(buffer, n, "%-" STR(ADDR_WIDTH) "s" "%-" STR(ADDR_WIDTH) "s", src, dst)
#define PRINT_PROTOCOL(buffer, n, prot)                     \
    snprintcat(buffer, n, "%-" STR(PROT_WIDTH) "s", prot)
#define PRINT_INFO(buffer, n, fmt, ...)         \
    snprintcat(buffer, n, fmt, ## __VA_ARGS__)
#define PRINT_LINE(buffer, n, i, t, src, dst, prot, fmt, ...)   \
    do {                                                        \
        PRINT_NUMBER(buffer, n, i);                             \
        PRINT_TIME(buffer, n, t);                               \
        PRINT_ADDRESS(buffer, n, src, dst);                     \
        PRINT_PROTOCOL(buffer, n, prot);                        \
        PRINT_INFO(buffer, n, fmt, ## __VA_ARGS__);             \
    } while (0)

static void print_error(char *buf, int size, struct packet *p);

void write_to_buf(char *buf, int size, struct packet *p)
{
    if (p->perr != NO_ERR && p->perr != UNK_PROTOCOL) {
        print_error(buf, size, p);
    } else {
        struct protocol_info *pinfo = NULL;

        if (p->root->next)
            pinfo = get_protocol(p->root->next->id);
        if (pinfo) {
            char time[TBUFLEN];
            struct timeval t = p->time;

            format_timeval(&t, time, TBUFLEN);
            PRINT_NUMBER(buf, size, p->num);
            PRINT_TIME(buf, size, time);
            pinfo->print_pdu(buf, size, p);
        } else if (p->len - ETHER_HDR_LEN)
            print_error(buf, size, p);
    }
}

static void print_error(char *buf, int size, struct packet *p)
{
    char smac[HW_ADDRSTRLEN];
    char dmac[HW_ADDRSTRLEN];
    char time[TBUFLEN];

    HW_ADDR_NTOP(smac, eth_src(p));
    HW_ADDR_NTOP(dmac, eth_dst(p));
    format_timeval(&p->time, time, TBUFLEN);
    if (p->perr != NO_ERR && p->perr != UNK_PROTOCOL) {
        PRINT_LINE(buf, size, p->num, time, smac, dmac,
                   "ETH II", "Ethertype: 0x%x [decode error]", ethertype(p));
    } else { /* not yet supported */
        PRINT_LINE(buf, size, p->num, time, smac, dmac, "ETH II", "Ethertype: 0x%x",
                   ethertype(p));
    }
}

void print_dns_record(struct dns_info *info, int i, char *buf, int n, uint16_t type)
{
    switch (type) {
    case DNS_TYPE_A:
    {
        char addr[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, (struct in_addr *) &info->record[i].rdata.address, addr, sizeof(addr));
        snprintcat(buf, n, "%s", addr);
        break;
    }
    case DNS_TYPE_NS:
        snprintcat(buf, n, "%s", info->record[i].rdata.nsdname);
        break;
    case DNS_TYPE_CNAME:
        snprintcat(buf, n, "%s", info->record[i].rdata.cname);
        break;
    case DNS_TYPE_PTR:
        snprintcat(buf, n, "%s", info->record[i].rdata.ptrdname);
        break;
    case DNS_TYPE_AAAA:
    {
        char addr[INET6_ADDRSTRLEN];

        inet_ntop(AF_INET6, (struct in_addr *) info->record[i].rdata.ipv6addr, addr, sizeof(addr));
        snprintcat(buf, n, "%s", addr);
        break;
    }
    case DNS_TYPE_HINFO:
        snprintcat(buf, n, "%s ", info->record[i].rdata.hinfo.cpu);
        snprintcat(buf, n, "%s", info->record[i].rdata.hinfo.os);
        break;
    case DNS_TYPE_MX:
        snprintcat(buf, n, "%u %s", info->record[i].rdata.mx.preference,
                   info->record[i].rdata.mx.exchange);
        break;
    default:
        break;
    }
}

void print_nbns_record(struct nbns_info *info, int i, char *buf, int n)
{
    switch (info->record[i].rrtype) {
    case NBNS_NB:
    {
        if (info->record[i].rdata.nb.g) {
            snprintcat(buf, n, "Group NetBIOS name ");
        } else {
            snprintcat(buf, n, "Unique NetBIOS name ");
        }
        int addrs = info->record[i].rdata.nb.num_addr;
        snprintcat(buf, n, "%s ", get_nbns_node_type(info->record[i].rdata.nb.ont));
        while (addrs--) {
            char addr[INET_ADDRSTRLEN];

            inet_ntop(AF_INET, (struct in_addr *) &info->record[i].rdata.nb.address[0], addr, sizeof(addr));
            snprintcat(buf, n, "%s ", addr);
        }
        break;
    }
    case NBNS_NS:
        snprintcat(buf, n, " NSD Name: %s", info->record[i].rdata.nsdname);
        break;
    case NBNS_A:
    {
        char addr[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, (struct in_addr *) &info->record[i].rdata.nsdipaddr, addr, sizeof(addr));
        snprintcat(buf, n, " NSD IP address: %s", addr);
        break;
    }
    case NBNS_NBSTAT:
        break;
    default:
        break;
    }
}

void print_arp(char *buf, int n, void *data)
{
    struct packet *p = data;
    struct arp_info *arp = get_arp(p);
    char sip[INET_ADDRSTRLEN];
    char tip[INET_ADDRSTRLEN];
    char sha[HW_ADDRSTRLEN];

    inet_ntop(AF_INET, arp->sip, sip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, arp->tip, tip, INET_ADDRSTRLEN);
    PRINT_ADDRESS(buf, n, sip, tip);
    PRINT_PROTOCOL(buf, n, "ARP");
    switch (arp->op) {
    case ARPOP_REQUEST:
        PRINT_INFO(buf, n, "Request: Looking for hardware address of %s", tip);
        break;
    case ARPOP_REPLY:
        HW_ADDR_NTOP(sha, arp->sha);
        PRINT_INFO(buf, n, "Reply: %s has hardware address %s", sip, sha);
        break;
    default:
        PRINT_INFO(buf, n, "Opcode %d", arp->op);
        break;
    }
}

void print_llc(char *buf, int n, void *data)
{
    struct packet *p = data;
    struct packet_data *pdata = p->root->next;
    struct protocol_info *pinfo = NULL;
    char smac[HW_ADDRSTRLEN];
    char dmac[HW_ADDRSTRLEN];

    HW_ADDR_NTOP(smac, eth_src(p));
    HW_ADDR_NTOP(dmac, eth_dst(p));
    PRINT_ADDRESS(buf, n, smac, dmac);
    if (pdata->next)
        pinfo = get_protocol(pdata->next->id);
    if (pinfo) {
        pinfo->print_pdu(buf, n, pdata->next);
    } else {
        PRINT_PROTOCOL(buf, n, "LLC");
        PRINT_INFO(buf, n, "SSAP: 0x%x  DSAP: 0x%x  Control: 0x%x",
                   llc_ssap(p), llc_dsap(p), llc_control(p));
    }
}

void print_stp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct stp_info *stp = pdata->data;

    PRINT_PROTOCOL(buf, n, "STP");
    switch (stp->type) {
    case CONFIG:
        PRINT_INFO(buf, n, "Configuration BPDU");
        break;
    case RST:
        PRINT_INFO(buf, n, "Rapid Spanning Tree BPDU. Root Path Cost: %u  Port ID: 0x%x",
                   stp->root_pc, stp->root_id);
        break;
    case TCN:
        PRINT_INFO(buf, n, "Topology Change Notification BPDU");
        break;
    }
}

void print_snap(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct snap_info *snap = pdata->data;

    PRINT_PROTOCOL(buf, n, "SNAP");
    PRINT_INFO(buf, n, "OUI: 0x%06x  Protocol Id: 0x%04x",
               snap->oui, snap->protocol_id);
}

void print_ipv4(char *buf, int n, void *data)
{
    struct packet *p = data;
    struct packet_data *pdata = p->root->next;
    struct ipv4_info *ip = pdata->data;
    char src[INET_ADDRSTRLEN];
    char dst[INET_ADDRSTRLEN];
    struct protocol_info *pinfo = NULL;

    inet_ntop(AF_INET, &ip->src, src, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ip->dst, dst, INET_ADDRSTRLEN);
    PRINT_ADDRESS(buf, n, src, dst);
    if (pdata->next)
        pinfo = get_protocol(pdata->next->id);
    if (pinfo) {
        pinfo->print_pdu(buf, n, pdata->next);
    } else {
        PRINT_PROTOCOL(buf, n, "IPv4");
        PRINT_INFO(buf, n, "Next header: %d", ip->protocol);
    }
}

void print_ipv6(char *buf, int n, void *data)
{
    struct packet *p = data;
    struct packet_data *pdata = p->root->next;
    struct ipv6_info *ip = pdata->data;
    char src[INET6_ADDRSTRLEN];
    char dst[INET6_ADDRSTRLEN];
    struct protocol_info *pinfo = NULL;

    inet_ntop(AF_INET6, ip->src, src, INET6_ADDRSTRLEN);
    inet_ntop(AF_INET6, ip->dst, dst, INET6_ADDRSTRLEN);
    PRINT_ADDRESS(buf, n, src, dst);
    if (pdata->next)
        pinfo = get_protocol(pdata->next->id);
    if (pinfo) {
        pinfo->print_pdu(buf, n, pdata->next);
    } else {
        PRINT_PROTOCOL(buf, n, "IPv6");
        PRINT_INFO(buf, n, "Next header: %d", ip->next_header);
    }
}

void print_icmp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct icmp_info *icmp = pdata->data;
    char org[32];
    char rcvd[32];
    char xmit[32];
    char addr[INET_ADDRSTRLEN];

    PRINT_PROTOCOL(buf, n, "ICMP");
    switch (icmp->type) {
    case ICMP_ECHOREPLY:
        PRINT_INFO(buf, n, "Echo reply:   id = 0x%x  seq = %d", icmp->echo.id, icmp->echo.seq_num);
        break;
    case ICMP_ECHO:
        PRINT_INFO(buf, n, "Echo request: id = 0x%x  seq = %d", icmp->echo.id, icmp->echo.seq_num);
        break;
    case ICMP_UNREACH:
        PRINT_INFO(buf, n, "%s", get_icmp_dest_unreach_code(icmp->code));
        break;
    case ICMP_REDIRECT:
        inet_ntop(AF_INET, &icmp->gateway, addr, INET_ADDRSTRLEN);
        PRINT_INFO(buf, n, "Redirect to %s", addr);
        break;
    case ICMP_TSTAMP:
        PRINT_INFO(buf, n, "Timestamp request: id = 0x%x  seq = %d, originate = %s, receive = %s, transmit = %s",
                   icmp->echo.id, icmp->echo.seq_num, get_time_from_ms_ut(icmp->timestamp.originate, org, 32),
                   get_time_from_ms_ut(icmp->timestamp.receive, rcvd, 32),
                   get_time_from_ms_ut(icmp->timestamp.transmit, xmit, 32));
        break;
    case ICMP_TSTAMPREPLY:
        PRINT_INFO(buf, n, "Timestamp reply  : id = 0x%x  seq = %d, originate = %s, receive = %s, transmit = %s",
                   icmp->echo.id, icmp->echo.seq_num, get_time_from_ms_ut(icmp->timestamp.originate, org, 32),
                   get_time_from_ms_ut(icmp->timestamp.receive, rcvd, 32),
                   get_time_from_ms_ut(icmp->timestamp.transmit, xmit, 32));
        break;
    case ICMP_MASKREQ:
        inet_ntop(AF_INET, &icmp->addr_mask, addr, INET_ADDRSTRLEN);
        PRINT_INFO(buf, n, "Address mask request: id = 0x%x  seq = %d, mask = %s",
                   icmp->echo.id, icmp->echo.seq_num, addr);
        break;
    case ICMP_MASKREPLY:
        inet_ntop(AF_INET, &icmp->addr_mask, addr, INET_ADDRSTRLEN);
        PRINT_INFO(buf, n, "Address mask reply:   id = 0x%x  seq = %d, mask = %s",
                   icmp->echo.id, icmp->echo.seq_num, addr);
        break;
    default:
        PRINT_INFO(buf, n, "%s", get_icmp_type(icmp->type));
        break;
    }
}

void print_icmp6(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct icmp6_info *icmp6 = pdata->data;
    char addr[INET6_ADDRSTRLEN];

    PRINT_PROTOCOL(buf, n, "ICMP6");
    switch (icmp6->type) {
    case ICMP6_DST_UNREACH:
        PRINT_INFO(buf, n, "%s", get_icmp6_dest_unreach(icmp6->code));
        break;
    case ICMP6_PACKET_TOO_BIG:
        PRINT_INFO(buf, n, "Packet too big message: MTU = %d", icmp6->mtu);
        break;
    case ICMP6_TIME_EXCEEDED:
        PRINT_INFO(buf, n, "%s", get_icmp6_time_exceeded(icmp6->code));
        break;
    case ICMP6_PARAM_PROB:
        PRINT_INFO(buf, n, "%s: Pointer = %d", get_icmp6_parameter_problem(icmp6->code),
                   icmp6->pointer);
        break;
    case ICMP6_ECHO_REQUEST:
    case ICMP6_ECHO_REPLY:
        PRINT_INFO(buf, n, "%s: id = %u  seq = %u", get_icmp6_type(icmp6->type), icmp6->echo.id,
                   icmp6->echo.seq);
        break;
    case ND_ROUTER_SOLICIT:
        PRINT_INFO(buf, n, "%s", get_icmp6_type(icmp6->type));
        break;
    case ND_ROUTER_ADVERT:
        PRINT_INFO(buf, n, "%s", get_icmp6_type(icmp6->type));
        break;
    case ND_NEIGHBOR_SOLICIT:
        inet_ntop(AF_INET6, (struct in_addr *) icmp6->target_addr, addr, sizeof(addr));
        PRINT_INFO(buf, n, "Neighbor solicitation for %s", addr);
        break;
    case ND_NEIGHBOR_ADVERT:
        inet_ntop(AF_INET6, (struct in_addr *) icmp6->neigh_adv.target_addr, addr, sizeof(addr));
        PRINT_INFO(buf, n, "Neighbor advertisement. Target address: %s", addr);
        break;
    case ND_REDIRECT:
    {
        char target[INET6_ADDRSTRLEN];
        char dest[INET6_ADDRSTRLEN];

        inet_ntop(AF_INET6, (struct in_addr *) icmp6->redirect.target_addr, target, sizeof(target));
        inet_ntop(AF_INET6, (struct in_addr *) icmp6->redirect.dest_addr, dest, sizeof(dest));
        PRINT_INFO(buf, n, "Redirect. Target: %s  Destination: %s", target, dest);
        break;
    }
    default:
        PRINT_INFO(buf, n, "%s", get_icmp6_type(icmp6->type));
        break;
    }
}

void print_igmp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct igmp_info *igmp = pdata->data;

    char addr[INET_ADDRSTRLEN];

    PRINT_PROTOCOL(buf, n, "IGMP");
    switch (igmp->type) {
    case IGMP_HOST_MEMBERSHIP_QUERY:
        PRINT_INFO(buf, n, "Membership query  Max response time: %d seconds",
                   igmp->max_resp_time / 10);
        break;
    case IGMP_v1_HOST_MEMBERSHIP_REPORT:
        PRINT_INFO(buf, n, "Membership report");
        break;
    case IGMP_v2_HOST_MEMBERSHIP_REPORT:
        PRINT_INFO(buf, n, "IGMP2 Membership report");
        break;
    case IGMP_v3_HOST_MEMBERSHIP_REPORT:
        PRINT_INFO(buf, n, "IGMP3 Membership report");
        break;
    case IGMP_HOST_LEAVE_MESSAGE:
        PRINT_INFO(buf, n, "Leave group");
        break;
    default:
        PRINT_INFO(buf, n, "Type 0x%x", igmp->type);
        break;
    }
    if (igmp->type != IGMP_v3_HOST_MEMBERSHIP_REPORT) {
        inet_ntop(AF_INET, &igmp->group_addr, addr, INET_ADDRSTRLEN);
        PRINT_INFO(buf, n, "  Group address: %s", addr);
    }
}

void print_pim(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct pim_info *pim = pdata->data;
    char *type = get_pim_message_type(pim->type);

    PRINT_PROTOCOL(buf, n, "PIM");
    if (type) {
        PRINT_INFO(buf, n, "Message type: %s", type);
    } else {
        PRINT_INFO(buf, n, "Message type: %d", pim->type);
    }
}

void print_tcp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct tcp *tcp = pdata->data;
    struct protocol_info *pinfo = NULL;

    if (pdata->next)
        pinfo = get_protocol(pdata->next->id);
    if (pinfo) {
        pinfo->print_pdu(buf, n, pdata->next);
    } else {
        PRINT_PROTOCOL(buf, n, "TCP");
        PRINT_INFO(buf, n, "Source port: %d  Destination port: %d",
                   tcp->sport, tcp->dport);
        PRINT_INFO(buf, n, "  Flags:");
        if (tcp->fin)
            PRINT_INFO(buf, n, " FIN");
        if (tcp->syn)
            PRINT_INFO(buf, n, " SYN");
        if (tcp->rst)
            PRINT_INFO(buf, n, " RST");
        if (tcp->psh)
            PRINT_INFO(buf, n, " PSH");
        if (tcp->ack)
            PRINT_INFO(buf, n, " ACK");
        if (tcp->urg)
            PRINT_INFO(buf, n, " URG");
        if (tcp->ece)
            PRINT_INFO(buf, n, " ECE");
        if (tcp->cwr)
            PRINT_INFO(buf, n, " CWR");
        if (tcp->ns)
            PRINT_INFO(buf, n, " NS");
        PRINT_INFO(buf, n, "  seq: %u  ack: %u  win: %u",
                   tcp->seq_num, tcp->ack_num, tcp->window);
    }
}

void print_udp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct udp_info *udp = pdata->data;
    struct protocol_info *pinfo = NULL;

    if (pdata->next)
        pinfo = get_protocol(pdata->next->id);
    if (pinfo) {
        pinfo->print_pdu(buf, n, pdata->next);
    } else {
        PRINT_PROTOCOL(buf, n, "UDP");
        PRINT_INFO(buf, n, "Source port: %d  Destination port: %d",
                   udp->sport, udp->dport);
    }
}

void print_dns(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct dns_info *dns = pdata->data;

    if (get_protocol_key(pdata->id) == DNS) {
        PRINT_PROTOCOL(buf, n, "DNS");
    } else if (get_protocol_key(pdata->id) == MDNS) {
        PRINT_PROTOCOL(buf, n, "MDNS");
    } else {
        PRINT_PROTOCOL(buf, n, "LLMNR");
    }
    if (dns->qr == 0) {
        switch (dns->opcode) {
        case DNS_QUERY:
            if (dns->question) {
                PRINT_INFO(buf, n, "Standard query: ");
                PRINT_INFO(buf, n, "%s ", dns->question[0].qname);
                PRINT_INFO(buf, n, "%s", get_dns_type(dns->question[0].qtype));
            }
            break;
        case DNS_IQUERY:
            PRINT_INFO(buf, n, "Inverse query");
            break;
        case DNS_STATUS:
            PRINT_INFO(buf, n, "Server status request");
            break;
        }
    } else {
        if (dns->rcode == DNS_NO_ERROR) {
            PRINT_INFO(buf, n, "Response: ");
        } else {
            PRINT_INFO(buf, n, "Response: %s ", get_dns_rcode(dns->rcode));
        }
        if (dns->question) {
            PRINT_INFO(buf, n, "%s ", dns->question[0].qname);
        }
        if (dns->record) {
            for (unsigned int i = 0; i < dns->section_count[ANCOUNT]; i++) {
                PRINT_INFO(buf, n, "%s ", get_dns_type(dns->record[i].type));
                print_dns_record(dns, i, buf, n, dns->record[i].type);
                PRINT_INFO(buf, n, " ");
            }
        }
    }
}

void print_nbns(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct nbns_info *nbns = pdata->data;
    char opcode[16];

    PRINT_PROTOCOL(buf, n, "NBNS");
    if (nbns->r == 0) {
        strncpy(opcode, get_nbns_opcode(nbns->opcode), sizeof(opcode) - 1);
        PRINT_INFO(buf, n, "Name %s request: ", string_tolower(opcode));
        PRINT_INFO(buf, n, "%s ", nbns->question.qname);
        PRINT_INFO(buf, n, "%s ", get_nbns_type(nbns->question.qtype));
        if (nbns->section_count[ARCOUNT]) {
            print_nbns_record(nbns, 0, buf, n);
        }
    } else {
        switch (nbns->rcode) {
        case NBNS_FMT_ERR:
            PRINT_INFO(buf, n, "Format Error. Request was invalidly formatted");
            return;
        case NBNS_SRV_ERR:
            PRINT_INFO(buf, n, "Server failure. Problem with NBNS, cannot process name");
            return;
        case NBNS_IMP_ERR:
            PRINT_INFO(buf, n, "Unsupported request error");
            return;
        case NBNS_RFS_ERR:
            PRINT_INFO(buf, n, "Refused error");
            return;
        case NBNS_ACT_ERR:
            PRINT_INFO(buf, n, "Active error. Name is owned by another node");
            return;
        case NBNS_CFT_ERR:
            PRINT_INFO(buf, n, "Name in conflict error");
            return;
        default:
            break;
        }
        strncpy(opcode, get_nbns_opcode(nbns->opcode), sizeof(opcode) - 1);
        PRINT_INFO(buf, n, "Name %s response: ", string_tolower(opcode));
        PRINT_INFO(buf, n, "%s ", nbns->record[0].rrname);
        PRINT_INFO(buf, n, "%s ", get_nbns_type(nbns->record[0].rrtype));
        print_nbns_record(nbns, 0, buf, n);
    }
}

void print_nbds(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct nbds_info *nbds = pdata->data;
    char *type;

    PRINT_PROTOCOL(buf, n, "NBDS");
    if ((type = get_nbds_message_type(nbds->msg_type))) {
        PRINT_INFO(buf, n, "%s", type);
    }
}

void print_ssdp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct ssdp_info *ssdp = pdata->data;
    const node_t *node;

    PRINT_PROTOCOL(buf, n, "SSDP");
    node = list_begin(ssdp->fields);
    if (node) {
        PRINT_INFO(buf, n, (char *) list_data(node));
    }
}

void print_http(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct http_info *http = pdata->data;

    PRINT_PROTOCOL(buf, n, "HTTP");
    PRINT_INFO(buf, n, "%s", http->start_line);
}

void print_imap(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct imap_info *imap = pdata->data;

    PRINT_PROTOCOL(buf, n, "IMAP");
    if (imap->lines) {
        PRINT_INFO(buf, n, "%s", (char *) list_front(imap->lines));
    }
}

void print_smtp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct smtp_info *smtp = pdata->data;

    PRINT_PROTOCOL(buf, n, "SMTP");
    if (smtp->data) {
        PRINT_INFO(buf, n, "C: Mail data");
    } else {
        if (smtp->response) {
            const node_t *node;
            struct smtp_rsp *rsp;

            PRINT_INFO(buf, n, "S: ");
            DLIST_FOREACH(smtp->rsps, node) {
                const node_t *line;

                rsp = list_data(node);
                PRINT_INFO(buf, n, "%d%c", rsp->code, list_size(rsp->lines) > 1 ? '-' : ' ');
                DLIST_FOREACH(rsp->lines, line) {
                    PRINT_INFO(buf, n, "%s  ", list_data(line));
                }
            }
        } else {
            const node_t *node;
            struct smtp_cmd *cmd;

            PRINT_INFO(buf, n, "C: ");
            DLIST_FOREACH(smtp->cmds, node) {
                cmd = list_data(node);
                PRINT_INFO(buf, n, "%s %s  ", cmd->command, cmd->params);
            }
        }
    }
}

void print_tls(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct tls_info *tls = pdata->data;
    char *version = get_tls_version(tls->version);
    char *type = get_tls_type(tls->type);
    char records[MAXLINE];

    if (version) {
        PRINT_PROTOCOL(buf, n, version);
    } else {
        PRINT_PROTOCOL(buf, n, "TLS");
    }
    if (tls->type == TLS_HANDSHAKE) {
        snprintf(records, MAXLINE, "%s", get_tls_handshake_type(tls->handshake->type));
    } else {
        snprintf(records, MAXLINE, "%s", type);
    }
    tls = tls->next;
    while (tls) {
        if (tls->type == TLS_HANDSHAKE) {
            snprintcat(records, MAXLINE, ", %s", get_tls_handshake_type(tls->handshake->type));
        } else {
            snprintcat(records, MAXLINE, ", %s", type);
        }
        tls = tls->next;
    }
    PRINT_INFO(buf, n, "%s", records);
}

void print_dhcp(char *buf, int n, void *data)
{
    char hwaddr[HW_ADDRSTRLEN];
    struct dhcp_info *dhcp = (struct dhcp_info *)((struct packet_data *) data)->data;
    const node_t *node;

    PRINT_PROTOCOL(buf, n, "DHCP");
    DLIST_FOREACH(dhcp->options, node) {
        struct dhcp_options *opt = (struct dhcp_options *) list_data(node);

        if (opt->tag == DHCP_MESSAGE_TYPE) {
            switch (opt->byte) {
            case DHCPDISCOVER:
                HW_ADDR_NTOP(hwaddr, dhcp->chaddr);
                PRINT_INFO(buf, n, "Discover  Transaction id: 0x%x", dhcp->xid);
                break;
            case DHCPOFFER:
                PRINT_INFO(buf, n, "Offer     Transaction id: 0x%x", dhcp->xid);
                break;
            case DHCPREQUEST:
                PRINT_INFO(buf, n, "Request   Transaction id: 0x%x", dhcp->xid);
                break;
            case DHCPDECLINE:
                PRINT_INFO(buf, n, "Decline   Transaction id: 0x%x", dhcp->xid);
                break;
            case DHCPACK:
                PRINT_INFO(buf, n, "ACK       Transaction id: 0x%x", dhcp->xid);
                break;
            case DHCPNAK:
                PRINT_INFO(buf, n, "NAK       Transaction id: 0x%x", dhcp->xid);
                break;
            case DHCPRELEASE:
                PRINT_INFO(buf, n, "Release   Transaction id: 0x%x", dhcp->xid);
                break;
            case DHCPINFORM:
                PRINT_INFO(buf, n, "Inform    Transaction id: 0x%x", dhcp->xid);
            default:
                break;
            }
            break;
        }
    }
}

void print_snmp(char *buf, int n, void *data)
{
    struct packet_data *pdata = data;
    struct snmp_info *snmp = pdata->data;
    char *type;
    list_t *vars;

    PRINT_PROTOCOL(buf, n, "SNMP");
    if ((type = get_snmp_type(snmp))) {
        PRINT_INFO(buf, n, "%s ", type);
    } else {
        PRINT_INFO(buf, n, "type: %d ", snmp->pdu_type);
    }
    if (snmp->pdu_type == SNMP_TRAP) {
        vars = snmp->trap->varbind_list;
    } else {
        vars = snmp->pdu->varbind_list;
    }
    if (vars) {
        const node_t *n = list_begin(vars);

        while (n) {
            struct snmp_varbind *var = (struct snmp_varbind *) list_data(n);

            PRINT_INFO(buf, MAXLINE, "%s ", var->object_name);
            n = list_next(n);
        }
    }
}
