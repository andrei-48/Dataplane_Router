# **PCOM-homework-1: Dataplane Router**

**Marinescu Andrei-Bogdan 325CA**

## Task Description
The program implements the dataplane of a router, used for forwarding packets from one host to another. The router takes care of IPv4 (including ICMP) packets and ARP packets.

## Functionalities
The router can forward a packet from one hop to another, create ARP requests, send ARP replies and send ICMP (echo reply, time exceeded, host unreachable) packets. It also has a dynamic ARP table and a trie for faster LPM aglorithm.

## Implementation Information
### Data Structures Used
* queue
```c
struct queue
{
	list head;
	list tail;
};
```
* linked list (used for the queue)
```c
struct cell
{
  void *element;
  list next;
};
```
* trie
```c
struct trie_node {
	struct route_table_entry *entry;
	struct trie_node *zero;
	struct trie_node *one;
};
```
### The workflow of the program
When a packet is received by the router it extracts the ethernet header from it and checks if it's an IPv4 or ARP packet, otherwise it is dropped.

It then checks which type of packet it is in order to process it accordingly:

#### IPv4:

The IP header is extracted and processed. First we verify the checksum. If it is wrong the packet is dropped. Then the router checks for the next hop of the packet, using a trie for LPM search (will be presented later in the document). If the route doesn't exist an ICMP packet is sent to the packet's sender. Then the ttl of the packet is checked. In case of an error an ICMP time exceeded packet is sent, otherwise we recalculate the checksum with the new ttl.

If the packet is an ICMP request for the router, the sender and receiver addresses are swapped and the required codes are changed in the ICMP header of the packet. It is then sent back to the sender.

In order to forward a packet we need to know the ip and the mac of the next hop. We use a dynamic arp table for this. If no match has been found for the next hop of the packet we broadcast an ARP request and wait for a reply. The packet in put in a queue until the reply is sent.

#### ARP:

The router can receive two types of ARP packets: replies for previous requests or requests for its own mac address.

* Replies: when the router gets a reply it adds the new ip-mac pair to its arp table in case other packets will need it. The router then dequeues the packet that was waiting for the reply, completes its ethernet header with the next hop's mac and sends the packet.

* Requests: when the router gets a request for its mac address it simply swaps the ips in the arp header, swaps the mac addresses in the arp header and the ethernet header and replaces the unknown mac with its own (in both the arp and ethernet headers). The packet is then sent back.

### LPM
As mentioned before, the LPM algorithm is optimized using a trie for faster IP retrieval.

* Trie insertion: after we parse the routing table into an array we go through each entry and add it to the trie as follows: We convert the mask and the prefix into host order (from network); we go through every bit of the prefix while the mask still has bits set to one (from left to right), going either to the "zero" child of the current node or the "one" child. After we finish the traversal (when there are no more bits set to 1 in the mask) we add the routing entry to the current node in the trie.

* Trie search: just like the insertion we convert the traget ip to host order then we go trough every bit of the ip, going either to "zero" or "one". At every step of the search we check if there is an entry in the current node and save it as the best route. After going through all bits of the ip we simply return the last entry we found.

This aproach has a O(32) - size of IPv4 addresses; time complexity for LPM lookup because it just goes through every bit of the address and returns a route.

# Rescources
* Lab 4 implementation of the forwarding process
* [Trie idea](https://vinesmsuic.github.io/notes-networkingIP-L3/)

