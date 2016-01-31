
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "net.hpp"

extern helx::EventHub eventHub;

enum {
	kEtherIp4 = 0x0800,
	kEtherArp = 0x0806
};

struct ArpPacket {
	uint16_t hwType;
	uint16_t protoType;
	uint8_t hwLength, protoLength;
	uint16_t operation;
	uint8_t senderHw[6];
	uint8_t senderProto[4];
	uint8_t targetHw[6];
	uint8_t targetProto[4];
};

namespace virtio {
namespace net {

void *receiveBuffer;
void *transmitBuffer;

Device::Device()
		: receiveQueue(*this, 0), transmitQueue(*this, 1) { }

void Device::sendPacket(std::string packet) {
	size_t tx_header_index = transmitQueue.lockDescriptor();
	size_t tx_packet_index = transmitQueue.lockDescriptor();

	auto header = (VirtHeader *)transmitBuffer;
	header->flags = 0;
	header->gsoType = 0;
	header->hdrLen = 0;
	header->gsoSize = 0;
	header->csumStart = 0;
	header->csumOffset = 0;
	
	memcpy((char *)transmitBuffer + sizeof(VirtHeader), packet.data(), packet.length());
	
	// setup a descriptor for the virtio header
	uintptr_t tx_header_physical;
	HEL_CHECK(helPointerPhysical(transmitBuffer, &tx_header_physical));
	
	VirtDescriptor *tx_header_desc = transmitQueue.accessDescriptor(tx_header_index);
	tx_header_desc->address = tx_header_physical;
	tx_header_desc->length = sizeof(VirtHeader);
	tx_header_desc->flags = VIRTQ_DESC_F_NEXT;
	tx_header_desc->next = tx_packet_index;

	// setup a descriptor for the packet
	VirtDescriptor *tx_packet_desc = transmitQueue.accessDescriptor(tx_packet_index);
	tx_packet_desc->address = tx_header_physical + sizeof(VirtHeader);
	tx_packet_desc->length = packet.length();
	tx_packet_desc->flags = 0;

	transmitQueue.postDescriptor(tx_header_index);
	transmitQueue.notifyDevice();
}

void Device::testDevice() {
	printf("testDevice()\n");

	libnet::testDevice(*this);

	// -----------------------------------------------------------------------------------------

	size_t rx_header_index = receiveQueue.lockDescriptor();
	size_t rx_packet_index = receiveQueue.lockDescriptor();

	// setup a descriptor for the header
	uintptr_t rx_header_physical;
	HEL_CHECK(helPointerPhysical((char *)receiveBuffer, &rx_header_physical));
	
	VirtDescriptor *rx_header_desc = receiveQueue.accessDescriptor(rx_header_index);
	rx_header_desc->address = rx_header_physical;
	rx_header_desc->length = sizeof(VirtHeader);
	rx_header_desc->flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
	rx_header_desc->next = rx_packet_index;

	// setup a descriptor for the packet
	VirtDescriptor *rx_packet_desc = receiveQueue.accessDescriptor(rx_packet_index);
	rx_packet_desc->address = rx_header_physical + sizeof(VirtHeader);
	rx_packet_desc->length = 1514;
	rx_packet_desc->flags = VIRTQ_DESC_F_WRITE;

	receiveQueue.postDescriptor(rx_header_index);
	receiveQueue.notifyDevice();

	while(true) {
		receiveQueue.processInterrupt();
		transmitQueue.processInterrupt();
	}
}

void Device::doInitialize() {
	receiveQueue.setupQueue();
	transmitQueue.setupQueue();

	receiveBuffer = malloc(2048);
	transmitBuffer = malloc(2048);

	// natural alignment to make sure we do not cross page boundaries
	assert((uintptr_t)receiveBuffer % 2048 == 0);
	assert((uintptr_t)transmitBuffer % 2048 == 0);	
}

void Device::retrieveDescriptor(size_t queue_index, size_t desc_index) {
	printf("retrieve(%lu, %lu)\n", queue_index, desc_index);

	if(queue_index == 0) {
		// FIXME: fill in the correct length
		libnet::onReceive((char *)receiveBuffer + sizeof(VirtHeader), 0);
	}
}

void Device::afterRetrieve() { }

void Device::onInterrupt(HelError error) { }

} } // namespace virtio::net