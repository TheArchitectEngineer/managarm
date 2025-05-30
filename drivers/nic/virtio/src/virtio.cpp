#include <nic/virtio/virtio.hpp>

#include <arch/dma_pool.hpp>
#include <core/virtio/core.hpp>

namespace {
	constexpr bool logFrames = false;
}

namespace {
// Device feature bits.
constexpr size_t legacyHeaderSize = 10;
enum {
	VIRTIO_NET_F_MAC = 5
};

// Bits for VirtHeader::flags.
enum {
	VIRTIO_NET_HDR_F_NEEDS_CSUM = 1
};

// Values for VirtHeader::gsoType.
enum {
	VIRTIO_NET_HDR_GSO_NONE = 0,
	VIRTIO_NET_HDR_GSO_TCPV4 = 1,
	VIRTIO_NET_HDR_GSO_UDP = 2,
	VIRTIO_NET_HDR_GSO_TCPV6 = 3,
	VIRTIO_NET_HDR_GSO_ECN = 0x80
};

struct VirtHeader {
	uint8_t flags;
	uint8_t gsoType;
	uint16_t hdrLen;
	uint16_t gsoSize;
	uint16_t csumStart;
	uint16_t csumOffset;
	uint16_t numBuffers;
};

struct VirtioNic : nic::Link {
	VirtioNic(mbus_ng::EntityId entity, std::unique_ptr<virtio_core::Transport> transport);
	async::result<void> initialize();

	async::result<size_t> receive(arch::dma_buffer_view) override;
	async::result<void> send(const arch::dma_buffer_view) override;

	~VirtioNic() override = default;
private:
	mbus_ng::EntityId entity_;
	std::unique_ptr<virtio_core::Transport> transport_;
	arch::contiguous_pool dmaPool_;
	virtio_core::Queue *receiveVq_;
	virtio_core::Queue *transmitVq_;
};

VirtioNic::VirtioNic(mbus_ng::EntityId entity, std::unique_ptr<virtio_core::Transport> transport)
	: nic::Link(1500, &dmaPool_), entity_{entity}, transport_ { std::move(transport) }
{
	if(transport_->checkDeviceFeature(VIRTIO_NET_F_MAC)) {
		for (int i = 0; i < 6; i++) {
			mac_[i] = transport_->loadConfig8(i);
		}
		char ms[3 * 6 + 1];
		sprintf(ms, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
				mac_[0], mac_[1], mac_[2],
				mac_[3], mac_[4], mac_[5]);
		std::cout << "virtio-driver: Device has a hardware MAC: "
			<< ms << std::endl;
		transport_->acknowledgeDriverFeature(VIRTIO_NET_F_MAC);
	}

	transport_->finalizeFeatures();
	transport_->claimQueues(2);
	receiveVq_ = transport_->setupQueue(0);
	transmitVq_ = transport_->setupQueue(1);

	promiscuous_ = true;
	all_multicast_ = true;
	multicast_ = true;
	broadcast_ = true;
	l1_up_ = true;

	transport_->runDevice();
}

async::result<void> VirtioNic::initialize() {
	mbus_ng::Properties netProperties{
		{"drvcore.mbus-parent", mbus_ng::StringItem{std::to_string(entity_)}},
		{"unix.subsystem", mbus_ng::StringItem{"net"}},
	};
	netProperties.merge(mbusNetworkProperties());

	auto netClassEntity = (co_await mbus_ng::Instance::global().createEntity(
		"net", netProperties)).unwrap();

	[] (mbus_ng::EntityManager entity) -> async::detached {
		while (true) {
			auto [localLane, remoteLane] = helix::createStream();

			// If this fails, too bad!
			(void)(co_await entity.serveRemoteLane(std::move(remoteLane)));
		}
	}(std::move(netClassEntity));
}

async::result<size_t> VirtioNic::receive(arch::dma_buffer_view frame) {
	arch::dma_object<VirtHeader> header { &dmaPool_ };

	virtio_core::Chain chain;
	chain.append(co_await receiveVq_->obtainDescriptor());
	chain.setupBuffer(virtio_core::deviceToHost,
			header.view_buffer().subview(0, legacyHeaderSize));
	chain.append(co_await receiveVq_->obtainDescriptor());
	chain.setupBuffer(virtio_core::deviceToHost, frame);

	co_return (co_await receiveVq_->submitDescriptor(chain.front()) - legacyHeaderSize);
}

async::result<void> VirtioNic::send(const arch::dma_buffer_view payload) {
	if (payload.size() > 1514) {
		throw std::runtime_error("data exceeds mtu");
	}

	arch::dma_object<VirtHeader> header { &dmaPool_ };
	memset(header.data(), 0, sizeof(VirtHeader));

	virtio_core::Chain chain;
	chain.append(co_await transmitVq_->obtainDescriptor());
	chain.setupBuffer(virtio_core::hostToDevice,
			header.view_buffer().subview(0, legacyHeaderSize));
	chain.append(co_await transmitVq_->obtainDescriptor());
	chain.setupBuffer(virtio_core::hostToDevice, payload);

	if(logFrames) {
		std::cout << "virtio-driver: sending frame" << std::endl;
	}
	co_await transmitVq_->submitDescriptor(chain.front());
	if(logFrames) {
		std::cout << "virtio-driver: sent frame" << std::endl;
	}
}
} // namespace

namespace nic::virtio {

async::result<std::shared_ptr<nic::Link>> makeShared(mbus_ng::EntityId entity,
		std::unique_ptr<virtio_core::Transport> transport) {
	auto nic = std::make_shared<VirtioNic>(entity, std::move(transport));
	co_await nic->initialize();

	co_return nic;
}

} // namespace nic::virtio
