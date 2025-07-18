
#include <linux/netlink.h>
#include <sstream>

#include "drvcore.hpp"
#include "netlink/nl-socket.hpp"

namespace drvcore {

std::shared_ptr<sysfs::Object> globalDevicesObject;
std::shared_ptr<sysfs::Object> globalVirtualDeviceParent;
std::shared_ptr<sysfs::Object> globalBusObject;
std::shared_ptr<sysfs::Object> globalClassObject;
std::shared_ptr<sysfs::Object> globalCharObject;
std::shared_ptr<sysfs::Object> globalBlockDevObject;
std::shared_ptr<sysfs::Object> globalBlockObject;
std::shared_ptr<sysfs::Object> globalFirmwareObject;

sysfs::Object *devicesObject() {
	assert(globalDevicesObject);
	return globalDevicesObject.get();
}

sysfs::Object *virtualDeviceParent() {
	assert(globalVirtualDeviceParent);
	return globalVirtualDeviceParent.get();
}

sysfs::Object *busObject() {
	assert(globalBusObject);
	return globalBusObject.get();
}

sysfs::Object *classObject() {
	assert(globalClassObject);
	return globalClassObject.get();
}

std::shared_ptr<sysfs::Object> firmwareObject() {
	assert(globalFirmwareObject);
	return globalFirmwareObject;
}

struct UeventAttribute : sysfs::Attribute {
	static auto singleton() {
		static UeventAttribute attr;
		return &attr;
	}

private:
	UeventAttribute()
	: sysfs::Attribute("uevent", true) { }

public:
	async::result<frg::expected<Error, std::string>> show(sysfs::Object *object) override {
		auto device = static_cast<Device *>(object);

		UeventProperties ue;
		device->composeStandardUevent(ue);
		device->composeUevent(ue);

		std::stringstream ss;
		for(const auto &[name, value] : ue)
			ss << name << '=' << value << '\n';

		co_return ss.str();
	}

	async::result<Error> store(sysfs::Object *object, std::string data) override {
		(void) data;

		auto device = static_cast<Device *>(object);

		UeventProperties ue;
		device->composeStandardUevent(ue);
		device->composeUevent(ue);

		udev::emitAddEvent(device->getSysfsPath(), ue);
		co_return Error::success;
	}
};

//-----------------------------------------------------------------------------
// Device implementation.
//-----------------------------------------------------------------------------

Device::Device(std::shared_ptr<Device> parent, std::shared_ptr<sysfs::Object> parentDirectory,
	std::string name, UnixDevice *unix_device, Subsystem *subsys)
: sysfs::Object{
	parentDirectory ? parentDirectory : (subsys != nullptr ? subsys->object() : globalDevicesObject),
	std::move(name)}, _unixDevice{unix_device}, _parentDevice{parent},
	_parentDirectory{parentDirectory}, _subsystem{subsys} { }

std::string Device::getSysfsPath() {
	std::string path = name();
	auto parent = directoryNode()->treeLink()->getOwner();
	auto link = parent->treeLink().get();
	while(true) {
		auto owner = link->getOwner();
		if(!owner)
			break;
		path = link->getName() + '/' + path;
		link = owner->treeLink().get();
	}

	return path;
}

void Device::composeStandardUevent(UeventProperties &ue) {
	if(auto unix_dev = unixDevice(); unix_dev) {
		auto node_path = unix_dev->nodePath();
		if(!node_path.empty())
			ue.set("DEVNAME", node_path);
		ue.set("MAJOR", std::to_string(unix_dev->getId().first));
		ue.set("MINOR", std::to_string(unix_dev->getId().second));
	}
}

void Device::linkToSubsystem() {
	// Nothing to do for devices outside of a subsystem.
}

//-----------------------------------------------------------------------------
// BusSubsystem and BusDevice implementation.
//-----------------------------------------------------------------------------

BusSubsystem::BusSubsystem(std::string name)
: Subsystem{std::make_shared<sysfs::Object>(globalBusObject, std::move(name))} {
	object()->addObject();
	_devicesObject = std::make_shared<sysfs::Object>(object(), "devices");
	_devicesObject->addObject();
	_driversObject = std::make_shared<sysfs::Object>(object(), "drivers");
	_driversObject->addObject();
}

BusDevice::BusDevice(BusSubsystem *subsystem, std::string name,
		UnixDevice *unix_device, std::shared_ptr<Device> parent)
: Device{parent, parent, std::move(name), unix_device, subsystem} { }

void BusDevice::linkToSubsystem() {
	auto devices_object = static_cast<BusSubsystem *>(subsystem())->devicesObject();
	devices_object->createSymlink(name(), devicePtr());
	createSymlink("subsystem", subsystem()->object());
}

//-----------------------------------------------------------------------------
// ClassSubsystem and ClassDevice implementation.
//-----------------------------------------------------------------------------

ClassSubsystem::ClassSubsystem(std::string name)
: Subsystem{std::make_shared<sysfs::Object>(globalClassObject, std::move(name))} {
	object()->addObject();
}

ClassDevice::ClassDevice(ClassSubsystem *subsystem, std::shared_ptr<Device> parent,
		std::string name, UnixDevice *unix_device)
: Device{parent, subsystem->classDirFor(parent), std::move(name), unix_device, subsystem},
parentDevice_{parent} { }

void ClassDevice::linkToSubsystem() {
	auto subsystem_object = subsystem()->object();
	if(parentDevice_) {
		subsystem_object->createSymlink(name(), devicePtr());
		createSymlink("device", parentDevice_);
	}
	createSymlink("subsystem", subsystem_object);
}

//-----------------------------------------------------------------------------
// BlockDevice implementation.
//-----------------------------------------------------------------------------

BlockDevice::BlockDevice(ClassSubsystem *subsystem, std::shared_ptr<Device> parent,
		std::string name, UnixDevice *unix_device)
: Device{parent, parent, std::move(name), unix_device, subsystem} { }

void BlockDevice::linkToSubsystem() {
	auto subsystem_object = subsystem()->object();
	globalBlockObject->createSymlink(name(), devicePtr());
	subsystem_object->createSymlink(name(), devicePtr());
	if (auto parent = parentDevice(); parent)
		createSymlink("device", std::move(parent));
	createSymlink("subsystem", subsystem_object);
}

//-----------------------------------------------------------------------------
// Free functions.
//-----------------------------------------------------------------------------

void initialize() {
	netlink::nl_socket::setupProtocols();

	// Create the /sys/dev/{char,block} directories.
	auto dev_object = std::make_shared<sysfs::Object>(nullptr, "dev");
	globalCharObject = std::make_shared<sysfs::Object>(dev_object, "char");
	globalBlockDevObject = std::make_shared<sysfs::Object>(dev_object, "block");

	// Create the global /sys/{devices,class,dev ...} directories.
	globalDevicesObject = std::make_shared<sysfs::Object>(nullptr, "devices");
	globalVirtualDeviceParent = std::make_shared<sysfs::Object>(globalDevicesObject, "virtual");
	globalBusObject = std::make_shared<sysfs::Object>(nullptr, "bus");
	globalClassObject = std::make_shared<sysfs::Object>(nullptr, "class");
	globalBlockObject = std::make_shared<sysfs::Object>(nullptr, "block");
	globalFirmwareObject = std::make_shared<sysfs::Object>(nullptr, "firmware");
	globalDevicesObject->addObject();
	globalVirtualDeviceParent->addObject();
	globalBusObject->addObject();
	globalClassObject->addObject();
	globalBlockObject->addObject();
	dev_object->addObject();
	globalCharObject->addObject(); // TODO: Do this before dev_object is visible.
	globalBlockDevObject->addObject();
	globalFirmwareObject->addObject();

	// Create /sys/fs/cgroup directories
	auto fs_object = std::make_shared<sysfs::Object>(nullptr, "fs");
	auto cgroup_object = std::make_shared<sysfs::Object>(fs_object, "cgroup");
	fs_object->addObject();
	cgroup_object->addObject();
}

namespace {

std::map<mbus_ng::EntityId, std::shared_ptr<Device>> mbusMap;

} // namespace

async::recurring_event mbusMapUpdate;

// TODO(no92): also attach type info (USB, PCI, etc.) about the device here?
void registerMbusDevice(mbus_ng::EntityId id, std::shared_ptr<Device> dev) {
	mbusMap.insert({id, std::move(dev)});
	mbusMapUpdate.raise();
}

std::shared_ptr<Device> getMbusDevice(mbus_ng::EntityId id) {
	auto v = mbusMap.find(id);

	if(v != mbusMap.end()) {
		return v->second;
	}

	return {};
}

void installDevice(std::shared_ptr<Device> device) {
	device->setupDevicePtr(device);
	device->addObject();

	// TODO: Do this before the object becomes visible in sysfs.
	device->linkToSubsystem();
	device->realizeAttribute(UeventAttribute::singleton());

	if(auto unix_dev = device->unixDevice(); unix_dev) {
		std::stringstream id_ss;
		id_ss << unix_dev->getId().first << ":" << unix_dev->getId().second;
		if (unix_dev->type() == VfsType::charDevice)
			globalCharObject->createSymlink(id_ss.str(), device);
		else if (unix_dev->type() == VfsType::blockDevice)
			globalBlockDevObject->createSymlink(id_ss.str(), device);
		else
			assert(!"Unsupported unix device trying to be added!");
	}

	UeventProperties ue;
	device->composeStandardUevent(ue);
	device->composeUevent(ue);

	udev::emitAddEvent(device->getSysfsPath(), ue);
}

namespace udev {

namespace {

// TODO: There could be a race between makeHotplugSeqnum() and emitHotplug().
//       Is it required that seqnums always appear in the correct order?
uint32_t allocateNextSeq() {
	static uint32_t seqnum = 1;
	return seqnum++;
}

void emitEvent(std::string buffer) {
	netlink::nl_socket::broadcast(NETLINK_KOBJECT_UEVENT, 1, std::move(buffer));
}

} // namespace

void emitAddEvent(std::string devpath, UeventProperties &ue) {
	std::stringstream ss;
	ss << "add@/" << devpath << '\0';
	ss << "ACTION=add" << '\0';
	ss << "DEVPATH=/" << devpath << '\0';
	ss << "SEQNUM=" << allocateNextSeq() << '\0';
	for(const auto &[name, value] : ue)
		ss << name << '=' << value << '\0';
	udev::emitEvent(ss.str());
}

void emitChangeEvent(std::string devpath, UeventProperties &ue) {
	std::stringstream ss;
	ss << "change@/" << devpath << '\0';
	ss << "ACTION=change" << '\0';
	ss << "DEVPATH=/" << devpath << '\0';
	ss << "SEQNUM=" << allocateNextSeq() << '\0';
	for(const auto &[name, value] : ue)
		ss << name << '=' << value << '\0';
	udev::emitEvent(ss.str());
}

} // namespace udev

} // namespace drvcore

