#include "protocols.h"
#include "queue.h"
#include "lib.h"
#include <arpa/inet.h>
#include <string.h>

struct route_table_entry *rtable;
int rtable_len;

struct arp_table_entry *arp_table;
int arp_table_len;

struct trie_node {
	struct route_table_entry *entry;
	struct trie_node *zero;
	struct trie_node *one;
};

void trie_insert(struct trie_node *root, struct route_table_entry *new_entry)
{
	struct trie_node *curr = root;
	uint32_t mask = ntohl(new_entry->mask);
	uint32_t prefix = ntohl(new_entry->prefix);

	for (int i = 31; i >= 0; i--) {
		if (((mask >> i) & 1) == 0) {
			break;
		}

		if (((prefix >> i) & 1) == 0) {
			if (!curr->zero) {
				curr->zero = calloc(1, sizeof(struct trie_node));
				DIE(!curr->zero, "calloc failed at trie insert zero");
			}
			curr = curr->zero;
		} else {
			if (!curr->one) {
				curr->one = calloc(1, sizeof(struct trie_node));
				DIE(!curr->one, "calloc failed at trie insert one");
			}
			curr = curr->one;
		}
	}
	curr->entry = new_entry;
}

struct route_table_entry *get_best_route(struct trie_node *root, uint32_t ip_dest)
{
	struct trie_node *curr = root;
	uint32_t ip = ntohl(ip_dest);
	struct route_table_entry *best_route = NULL;

	for (int i = 31; i >= 0; i--) {
		if (((ip >> i) & 1) == 0) {
			if (!curr->zero) {
				break;
			}
			curr = curr->zero;
		} else {
			if (!curr->one) {
				break;
			}
			curr = curr->one;
		}

		if (curr->entry) {
			best_route = curr->entry;
		}
	}

	return best_route;
}

struct arp_table_entry *get_arp_entry(uint32_t given_ip)
{
	for (int i = 0; i < arp_table_len; i++) {
		if (given_ip == arp_table[i].ip) {
			return &arp_table[i];
		}
	}

	return NULL;
}

void send_icmp_err(char *buf, int interface, uint8_t type)
{
	char *packet = malloc(MAX_PACKET_LEN);
	DIE(!packet, "malloc failed at icmp response");

	struct ether_hdr *resp_eth = (struct ether_hdr *)packet;
	struct ether_hdr *eth_hdr = (struct ether_hdr *)buf;

	// Ethernet header for response
	memcpy(resp_eth->ethr_dhost, eth_hdr->ethr_shost, 6);
	memcpy(resp_eth->ethr_shost, eth_hdr->ethr_dhost, 6);
	resp_eth->ethr_type = htons(0x0800);

	// IPv4 header for response
	struct ip_hdr *resp_iphdr = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));
	struct ip_hdr *iphdr = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));
	resp_iphdr->ver = 4;
	resp_iphdr->ihl = 5;
	resp_iphdr->tos = 0;
	resp_iphdr->tot_len = htons(sizeof(struct ip_hdr) + sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + 8);
	resp_iphdr->id = htons(4);
	resp_iphdr->frag = 0;
	resp_iphdr->ttl = 64;
	resp_iphdr->proto = 1;
	resp_iphdr->checksum = 0;
	resp_iphdr->source_addr = inet_addr(get_interface_ip(interface));
	resp_iphdr->dest_addr = iphdr->source_addr;
	resp_iphdr->checksum = htons(checksum((uint16_t *)resp_iphdr, sizeof(struct ip_hdr)));

	// ICMP header for response
	struct icmp_hdr *resp_icmp = (struct icmp_hdr *)(packet + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
	resp_icmp->mcode = 0;
	resp_icmp->mtype = type;
	resp_icmp->check = 0;

	memcpy((char *)resp_icmp + sizeof(struct icmp_hdr), iphdr, sizeof(struct ip_hdr) + 8);

	resp_icmp->check = checksum((uint16_t *)resp_icmp, sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + 8);

	send_to_link(sizeof(struct ether_hdr) + sizeof(struct ip_hdr) +
		sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + 8, packet, interface);
}

int main(int argc, char *argv[])
{
	char buf[MAX_PACKET_LEN];

	// Do not modify this line
	init(argv + 2, argc - 2);

	rtable = malloc(sizeof(struct route_table_entry) * 100000);
	DIE(rtable == NULL, "memory");
 
	arp_table = malloc(sizeof(struct  arp_table_entry) * 100);
	DIE(arp_table == NULL, "memory");

	struct trie_node *root = malloc(sizeof(struct trie_node));
	DIE(!root, "malloc failed at trie root");

	rtable_len = read_rtable(argv[1], rtable);

	for (int i = 0; i < rtable_len; i++) {
		trie_insert(root, &rtable[i]);
	}

	queue packet_q = create_queue();

	while (1) {

		size_t interface;
		size_t len;

		interface = recv_from_any_link(buf, &len);
		DIE(interface < 0, "recv_from_any_links");

    // TODO: Implement the router forwarding logic

    /* Note that packets received are in network order,
		any header field which has more than 1 byte will need to be conerted to
		host order. For example, ntohs(eth_hdr->ether_type). The oposite is needed when
		sending a packet on the link, */

		struct ether_hdr *eth_hdr = (struct ether_hdr *)buf;

		if (eth_hdr->ethr_type != htons(0x0800) && eth_hdr->ethr_type != htons(0x0806)) {
			printf("Ignored non-IPv4 or ARP packet\n");
			continue;
		}

		// IPv4 protocol
		if (eth_hdr->ethr_type == htons(0x0800)) {
			struct ip_hdr *iphdr = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));

			int ch_sum = iphdr->checksum;
			iphdr->checksum = 0;
			ch_sum = ntohs(ch_sum);
			
			if (ch_sum != checksum((uint16_t *)iphdr, sizeof(struct ip_hdr))) {
				printf("Wrong checksum!\n");
				continue;
			}

			ch_sum = htons(ch_sum);
			iphdr->checksum = ch_sum;
			uint32_t daddr = iphdr->dest_addr;

			struct route_table_entry *entry = get_best_route(root, daddr);

			if (!entry) {
				send_icmp_err(buf, interface, 3);
				continue;
			}

			daddr = entry->next_hop;

			if (iphdr->ttl <= 1) {
				send_icmp_err(buf, interface, 11);
				continue;
			} else {
				iphdr->ttl--;
			}

			iphdr->checksum = ~(~iphdr->checksum + ~((uint16_t)(iphdr->ttl + 1)) + (uint16_t)iphdr->ttl) - 1;

			uint32_t interface_ip = inet_addr(get_interface_ip(interface));
			
			// check if it's ICMP request for the router
			if (iphdr->proto == 1 && iphdr->dest_addr == interface_ip) {
				uint8_t aux_mac[6] = {0};
				memcpy(aux_mac, eth_hdr->ethr_dhost, 6);
				memcpy(eth_hdr->ethr_dhost, eth_hdr->ethr_shost, 6);
				memcpy(eth_hdr->ethr_shost, aux_mac, 6);

				uint32_t aux_ip;
				aux_ip = iphdr->source_addr;
				iphdr->source_addr = iphdr->dest_addr;
				iphdr->dest_addr = aux_ip;

				struct icmp_hdr *icmphdr = (struct icmp_hdr *)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
				icmphdr->mcode = 0;
				icmphdr->mtype = 0;
				icmphdr->check = 0;
				icmphdr->check = htons(checksum((uint16_t *)icmphdr, sizeof(struct icmp_hdr)));

				send_to_link(len, buf, interface);
				continue;
			}

			struct arp_table_entry *arp_entry = get_arp_entry(entry->next_hop);
			if (!arp_entry) {
				char *aux = malloc(MAX_PACKET_LEN);
				DIE(!aux, "malloc failed at packet enq");

				memcpy(aux, buf, MAX_PACKET_LEN);
				queue_enq(packet_q, aux);

				// Create ARP request
				char *arp_req = malloc(42);
				DIE(!arp_req, "malloc failed at ARP request");

				// ethernet header for request
				struct ether_hdr *arp_eth_hdr = (struct ether_hdr *)arp_req;
				memcpy(arp_eth_hdr->ethr_dhost, "\xff\xff\xff\xff\xff\xff", 6);

				uint8_t arp_src_mac[6] = {0};
				get_interface_mac(entry->interface, arp_src_mac);

				memcpy(arp_eth_hdr->ethr_shost, arp_src_mac, 6);
				arp_eth_hdr->ethr_type = htons(0x0806);

				// arp header for request
				struct arp_hdr *arp_header = (struct arp_hdr *)(arp_req + sizeof(struct ether_hdr));

				arp_header->hw_type = htons(1);
				arp_header->proto_type = htons(0x0800);
				arp_header->hw_len = 6;
				arp_header->proto_len = 4;
				arp_header->opcode = htons(1);

				memcpy(arp_header->shwa, arp_src_mac, 6);

				uint32_t next_interface_ip = inet_addr(get_interface_ip(entry->interface));

				arp_header->sprotoa = next_interface_ip;

				memset(arp_header->thwa, 0x00, 6);

				arp_header->tprotoa = entry->next_hop;
				send_to_link(42, arp_req, entry->interface);
				continue;
			}

			uint8_t src_mac[6] = {0};
			get_interface_mac(entry->interface, src_mac);

			memcpy(eth_hdr->ethr_shost, src_mac, 6);
			memcpy(eth_hdr->ethr_dhost, arp_entry->mac, 6);

			send_to_link(len, buf, entry->interface);
			continue;
		}

		// ARP protocol
		if (eth_hdr->ethr_type == htons(0x0806)) {
			struct arp_hdr *arp_header = (struct arp_hdr *)(buf + sizeof(struct ether_hdr));

			if (arp_header->opcode == htons(2)) {
				struct arp_table_entry new_entry;
    			new_entry.ip = arp_header->sprotoa;
    			memcpy(new_entry.mac, arp_header->shwa, 6);

    			arp_table[arp_table_len] = new_entry;
				arp_table_len++;

				if (queue_empty(packet_q)) {
					continue;
				}

				char *packet = queue_deq(packet_q);
				struct ether_hdr *packet_eth = (struct ether_hdr *)packet;
				struct ip_hdr *packet_iphdr = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));

				struct route_table_entry *entry = get_best_route(root, packet_iphdr->dest_addr);

				uint8_t src_mac[6] = {0};
				get_interface_mac(entry->interface, src_mac);

				memcpy(packet_eth->ethr_shost, src_mac, 6);
				memcpy(packet_eth->ethr_dhost, new_entry.mac, 6);

				int packet_len = ntohs(packet_iphdr->tot_len) + sizeof(struct ether_hdr);

				send_to_link(packet_len, packet, entry->interface);
				continue;
			}

			uint32_t interface_ip = inet_addr(get_interface_ip(interface));

			if (arp_header->opcode == htons(1) && arp_header->tprotoa == interface_ip) {
				arp_header->opcode = htons(2);

				// swap IP addresses
				uint32_t aux = arp_header->sprotoa;
				arp_header->sprotoa = arp_header->tprotoa;
				arp_header->tprotoa = aux;

				// swap MAC addresses
				memcpy(arp_header->thwa, arp_header->shwa, 6);

				uint8_t interface_mac[6] = {0};
				get_interface_mac(interface, interface_mac);

				memcpy(arp_header->shwa, interface_mac, 6);

				memcpy(eth_hdr->ethr_dhost, arp_header->thwa, 6);
				memcpy(eth_hdr->ethr_shost, arp_header->shwa, 6);

				send_to_link(len, buf, interface);
				continue;
			}
		}
	}
}

