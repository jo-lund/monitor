#include <string.h>
#include "packet_ssdp.h"
#include "packet.h"
#include "../list.h"

static void parse_ssdp(char *str, int n, list_t *msg_header);

/*
 * The Simple Service Discovery Protocol (SSDP) is a network protocol based on
 * the Internet Protocol Suite for advertisement and discovery of network
 * services and presence information. It is a text-based protocol based on HTTP.
 * Services are announced by the hosting system with multicast addressing to a
 * specifically designated IP multicast address at UDP port number 1900.
 *
 * SSDP uses a NOTIFY HTTP method to announce the establishment or withdrawal of
 * services (presence) information to the multicast group. A client that wishes
 * to discover available services on a network uses the M-SEARCH method.
 * Responses to such search requests are sent via unicast addressing to the
 * originating address and port number of the multicast request.
 */
packet_error handle_ssdp(unsigned char *buffer, int n, struct application_info *adu)
{
    adu->ssdp = mempool_pealloc(sizeof(struct ssdp_info));
    pstat[PROT_SSDP].num_packets++;
    pstat[PROT_SSDP].num_bytes += n;
    adu->ssdp->fields = list_init(&d_alloc);
    parse_ssdp((char *) buffer, n, adu->ssdp->fields);
    return NO_ERR;
}

/*
 * Parses an SSDP string. SSDP strings are based on HTTP1.1 but contains no
 * message body.
 *
 * Copies the lines delimited by CRLF, i.e. the start line and the SSDP message
 * header fields, to msg_header list.
 */
void parse_ssdp(char *str, int n, list_t *msg_header)
{
    char *token;
    char cstr[n + 1];

    strncpy(cstr, str, n);
    cstr[n] = '\0';
    token = strtok(cstr, "\r\n");
    while (token) {
        char *field;

        field = mempool_pecopy0(token, strlen(token));
        list_push_back(msg_header, field);
        token = strtok(NULL, "\r\n");
    }
}
