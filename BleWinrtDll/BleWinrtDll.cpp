// BleWinrtDll.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
//

#include "stdafx.h"

#include "BleWinrtDll.h"

#pragma comment(lib, "windowsapp")

// macro for file, see also https://stackoverflow.com/a/14421702
#define __WFILE__ L"BleWinrtDll.cpp"

using namespace std;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Web::Syndication;

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;

using namespace Windows::Storage::Streams;

union to_guid
{
	uint8_t buf[16];
	guid guid;
};

const uint8_t BYTE_ORDER[] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };

guid make_guid(const wchar_t* value)
{
	to_guid to_guid;
	memset(&to_guid, 0, sizeof(to_guid));
	int offset = 0;
	for (int i = 0; i < wcslen(value); i++) {
		if (value[i] >= '0' && value[i] <= '9')
		{
			uint8_t digit = value[i] - '0';
			to_guid.buf[BYTE_ORDER[offset / 2]] += offset % 2 == 0 ? digit << 4 : digit;
			offset++;
		}
		else if (value[i] >= 'A' && value[i] <= 'F')
		{
			uint8_t digit = 10 + value[i] - 'A';
			to_guid.buf[BYTE_ORDER[offset / 2]] += offset % 2 == 0 ? digit << 4 : digit;
			offset++;
		}
		else if (value[i] >= 'a' && value[i] <= 'f')
		{
			uint8_t digit = 10 + value[i] - 'a';
			to_guid.buf[BYTE_ORDER[offset / 2]] += offset % 2 == 0 ? digit << 4 : digit;
			offset++;
		}
		else
		{
			// skip char
		}
	}

	return to_guid.guid;
}

// implement own caching instead of using the system-provicded cache as there is an AccessDenied error when trying to
// call GetCharacteristicsAsync on a service for which a reference is hold in global scope
// cf. https://stackoverflow.com/a/36106137

mutex errorLock;
wchar_t last_error[2048];
struct CharacteristicCacheEntry {
	GattCharacteristic characteristic = nullptr;
};
struct ServiceCacheEntry {
	GattDeviceService service = nullptr;
	map<long, CharacteristicCacheEntry> characteristics = { };
};
struct DeviceCacheEntry {
	BluetoothLEDevice device = nullptr;
	map<long, ServiceCacheEntry> services = { };
};
map<long, DeviceCacheEntry> cache;


// using hashes of uuids to omit storing the c-strings in reliable storage
long hsh(wchar_t* wstr)
{
	long hash = 5381;
	int c;
	while (c = *wstr++)
		hash = ((hash << 5) + hash) + c;
	return hash;
}

void clearError() {
	lock_guard error_lock(errorLock);
	wcscpy_s(last_error, L"Ok");
}

void saveError(const wchar_t* message, ...) {
	lock_guard error_lock(errorLock);
	va_list args;
	va_start(args, message);
	vswprintf_s(last_error, message, args);
	va_end(args);
	wcout << last_error << endl;
}

IAsyncOperation<BluetoothLEDevice> retrieveDevice(wchar_t* deviceId) {
	if (cache.count(hsh(deviceId)))
		co_return cache[hsh(deviceId)].device;
	// !!!! BluetoothLEDevice.FromIdAsync may prompt for consent, in this case bluetooth will fail in unity!
	BluetoothLEDevice result = co_await BluetoothLEDevice::FromIdAsync(deviceId);
	if (result == nullptr) {
		saveError(L"%s:%d Failed to connect to device.", __WFILE__, __LINE__);
		co_return nullptr;
	}
	else {
		clearError();
		cache[hsh(deviceId)] = { result };
		co_return cache[hsh(deviceId)].device;
	}
}
IAsyncOperation<GattDeviceService> retrieveService(wchar_t* deviceId, wchar_t* serviceId) {
	auto device = co_await retrieveDevice(deviceId);
	if (device == nullptr)
		co_return nullptr;
	if (cache[hsh(deviceId)].services.count(hsh(serviceId)))
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].service;
	GattDeviceServicesResult result = co_await device.GetGattServicesForUuidAsync(make_guid(serviceId), BluetoothCacheMode::Cached);
	if (result.Status() != GattCommunicationStatus::Success) {
		saveError(L"%s:%d Failed retrieving services.", __WFILE__, __LINE__);
		co_return nullptr;
	}
	else if (result.Services().Size() == 0) {
		saveError(L"%s:%d No service found with uuid ", __WFILE__, __LINE__);
		co_return nullptr;
	}
	else {
		clearError();
		cache[hsh(deviceId)].services[hsh(serviceId)] = { result.Services().GetAt(0) };
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].service;
	}
}
IAsyncOperation<GattCharacteristic> retrieveCharacteristic(wchar_t* deviceId, wchar_t* serviceId, wchar_t* characteristicId) {
	auto service = co_await retrieveService(deviceId, serviceId);
	if (service == nullptr)
		co_return nullptr;
	if (cache[hsh(deviceId)].services[hsh(serviceId)].characteristics.count(hsh(characteristicId)))
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].characteristics[hsh(characteristicId)].characteristic;
	GattCharacteristicsResult result = co_await service.GetCharacteristicsForUuidAsync(make_guid(characteristicId), BluetoothCacheMode::Cached);
	if (result.Status() != GattCommunicationStatus::Success) {
		saveError(L"%s:%d Error scanning characteristics from service %s with status %d", __WFILE__, __LINE__, serviceId, result.Status());
		co_return nullptr;
	}
	else if (result.Characteristics().Size() == 0) {
		saveError(L"%s:%d No characteristic found with uuid %s", __WFILE__, __LINE__, characteristicId);
		co_return nullptr;
	}
	else {
		clearError();
		cache[hsh(deviceId)].services[hsh(serviceId)].characteristics[hsh(characteristicId)] = { result.Characteristics().GetAt(0) };
		co_return cache[hsh(deviceId)].services[hsh(serviceId)].characteristics[hsh(characteristicId)].characteristic;
	}
}



DeviceWatcher deviceWatcher{ nullptr };
DeviceWatcher::Added_revoker deviceWatcherAddedRevoker;
DeviceWatcher::Updated_revoker deviceWatcherUpdatedRevoker;
DeviceWatcher::EnumerationCompleted_revoker deviceWatcherCompletedRevoker;

queue<DeviceUpdate> deviceQueue{};
mutex deviceQueueLock;
condition_variable deviceQueueSignal;
bool deviceScanFinished;

queue<Service> serviceQueue{};
mutex serviceQueueLock;
condition_variable serviceQueueSignal;
bool serviceScanFinished;

queue<Characteristic> characteristicQueue{};
mutex characteristicQueueLock;
condition_variable characteristicQueueSignal;
bool characteristicScanFinished;

// global flag to release calling thread
mutex quitLock;
bool quitFlag = false;

struct Subscription {
	GattCharacteristic characteristic = nullptr;
	GattCharacteristic::ValueChanged_revoker revoker;
};
list<Subscription*> subscriptions;
mutex subscribeQueueLock;
condition_variable subscribeQueueSignal;

queue<BLEData> dataQueue{};
mutex dataQueueLock;
condition_variable dataQueueSignal;

bool QuittableWait(condition_variable& signal, unique_lock<mutex>& waitLock) {
	{
		lock_guard quit_lock(quitLock);
		if (quitFlag)
			return true;
	}
	signal.wait(waitLock);
	lock_guard quit_lock(quitLock);
	return quitFlag;
}

void DeviceWatcher_Added(DeviceWatcher sender, DeviceInformation deviceInfo) {
	DeviceUpdate deviceUpdate;
	wcscpy_s(deviceUpdate.id, sizeof(deviceUpdate.id) / sizeof(wchar_t), deviceInfo.Id().c_str());
	wcscpy_s(deviceUpdate.name, sizeof(deviceUpdate.name) / sizeof(wchar_t), deviceInfo.Name().c_str());
	deviceUpdate.nameUpdated = true;
	if (deviceInfo.Properties().HasKey(L"System.Devices.Aep.Bluetooth.Le.IsConnectable")) {
		deviceUpdate.isConnectable = unbox_value<bool>(deviceInfo.Properties().Lookup(L"System.Devices.Aep.Bluetooth.Le.IsConnectable"));
		deviceUpdate.isConnectableUpdated = true;
	}
	{
		lock_guard lock(quitLock);
		if (quitFlag)
			return;
	}
	{
		lock_guard queueGuard(deviceQueueLock);
		deviceQueue.push(deviceUpdate);
		deviceQueueSignal.notify_one();
	}
}
void DeviceWatcher_Updated(DeviceWatcher sender, DeviceInformationUpdate deviceInfoUpdate) {
	DeviceUpdate deviceUpdate;
	wcscpy_s(deviceUpdate.id, sizeof(deviceUpdate.id) / sizeof(wchar_t), deviceInfoUpdate.Id().c_str());
	if (deviceInfoUpdate.Properties().HasKey(L"System.Devices.Aep.Bluetooth.Le.IsConnectable")) {
		deviceUpdate.isConnectable = unbox_value<bool>(deviceInfoUpdate.Properties().Lookup(L"System.Devices.Aep.Bluetooth.Le.IsConnectable"));
		deviceUpdate.isConnectableUpdated = true;
	}
	{
		lock_guard lock(quitLock);
		if (quitFlag)
			return;
	}
	{
		lock_guard queueGuard(deviceQueueLock);
		deviceQueue.push(deviceUpdate);
		deviceQueueSignal.notify_one();
	}
}
void DeviceWatcher_EnumerationCompleted(DeviceWatcher sender, IInspectable const&) {
	StopDeviceScan();
}

void StartDeviceScan() {
	// as this is the first function that must be called, if Quit() was called before, assume here that the client wants to restart
	{
		lock_guard lock(quitLock);
		quitFlag = false;
		clearError();
	}

	IVector<hstring> requestedProperties = single_threaded_vector<hstring>({ L"System.Devices.Aep.DeviceAddress", L"System.Devices.Aep.IsConnected", L"System.Devices.Aep.Bluetooth.Le.IsConnectable" });
	hstring aqsAllBluetoothLEDevices = L"(System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\")"; // list Bluetooth LE devices
	deviceWatcher = DeviceInformation::CreateWatcher(
		aqsAllBluetoothLEDevices,
		requestedProperties,
		DeviceInformationKind::AssociationEndpoint);

	// see https://docs.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/handle-events#revoke-a-registered-delegate
	deviceWatcherAddedRevoker = deviceWatcher.Added(auto_revoke, &DeviceWatcher_Added);
	deviceWatcherUpdatedRevoker = deviceWatcher.Updated(auto_revoke, &DeviceWatcher_Updated);
	deviceWatcherCompletedRevoker = deviceWatcher.EnumerationCompleted(auto_revoke, &DeviceWatcher_EnumerationCompleted);
	// ~30 seconds scan ; for permanent scanning use BluetoothLEAdvertisementWatcher, see the BluetoothAdvertisement.zip sample
	deviceScanFinished = false;
	deviceWatcher.Start();
}

ScanStatus PollDevice(DeviceUpdate* device, bool block) {
	ScanStatus res;
	unique_lock<mutex> lock(deviceQueueLock);
	if (block && deviceQueue.empty() && !deviceScanFinished)
		if (QuittableWait(deviceQueueSignal, lock))
			return ScanStatus::FINISHED;
	if (!deviceQueue.empty()) {
		*device = deviceQueue.front();
		deviceQueue.pop();
		res = ScanStatus::AVAILABLE;
	}
	else if (deviceScanFinished)
		res = ScanStatus::FINISHED;
	else
		res = ScanStatus::PROCESSING;
	return res;
}

void StopDeviceScan() {
	lock_guard lock(deviceQueueLock);
	if (deviceWatcher != nullptr) {
		deviceWatcherAddedRevoker.revoke();
		deviceWatcherUpdatedRevoker.revoke();
		deviceWatcherCompletedRevoker.revoke();
		deviceWatcher.Stop();
		deviceWatcher = nullptr;
	}
	deviceScanFinished = true;
	deviceQueueSignal.notify_one();
}

fire_and_forget ScanServicesAsync(wchar_t* deviceId) {
	{
		lock_guard queueGuard(serviceQueueLock);
		serviceScanFinished = false;
	}
	try {
		auto bluetoothLeDevice = co_await retrieveDevice(deviceId);
		if (bluetoothLeDevice != nullptr) {
			GattDeviceServicesResult result = co_await bluetoothLeDevice.GetGattServicesAsync(BluetoothCacheMode::Uncached);
			if (result.Status() == GattCommunicationStatus::Success) {
				IVectorView<GattDeviceService> services = result.Services();
				for (auto&& service : services)
				{
					Service serviceStruct;
					wcscpy_s(serviceStruct.uuid, sizeof(serviceStruct.uuid) / sizeof(wchar_t), to_hstring(service.Uuid()).c_str());
					{
						lock_guard lock(quitLock);
						if (quitFlag)
							break;
					}
					{
						lock_guard queueGuard(serviceQueueLock);
						serviceQueue.push(serviceStruct);
						serviceQueueSignal.notify_one();
					}
				}
			}
			else {
				saveError(L"%s:%d Failed retrieving services.", __WFILE__, __LINE__);
			}
		}
	}
	catch (hresult_error& ex)
	{
		saveError(L"%s:%d ScanServicesAsync catch: %s", __WFILE__, __LINE__, ex.message().c_str());
	}
	{
		lock_guard queueGuard(serviceQueueLock);
		serviceScanFinished = true;
		serviceQueueSignal.notify_one();
	}
}
void ScanServices(wchar_t* deviceId) {
	ScanServicesAsync(deviceId);
}

ScanStatus PollService(Service* service, bool block) {
	ScanStatus res;
	unique_lock<mutex> lock(serviceQueueLock);
	if (block && serviceQueue.empty() && !serviceScanFinished)
		if (QuittableWait(serviceQueueSignal, lock))
			return ScanStatus::FINISHED;
	if (!serviceQueue.empty()) {
		*service = serviceQueue.front();
		serviceQueue.pop();
		res = ScanStatus::AVAILABLE;
	}
	else if (serviceScanFinished)
		res = ScanStatus::FINISHED;
	else
		res = ScanStatus::PROCESSING;
	return res;
}

fire_and_forget ScanCharacteristicsAsync(wchar_t* deviceId, wchar_t* serviceId) {
	{
		lock_guard lock(characteristicQueueLock);
		characteristicScanFinished = false;
	}
	try {
		auto service = co_await retrieveService(deviceId, serviceId);
		if (service != nullptr) {
			GattCharacteristicsResult charScan = co_await service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached);
			if (charScan.Status() != GattCommunicationStatus::Success)
				saveError(L"%s:%d Error scanning characteristics from service %s width status %d", __WFILE__, __LINE__, serviceId, (int)charScan.Status());
			else {
				for (auto c : charScan.Characteristics())
				{
					Characteristic charStruct;
					wcscpy_s(charStruct.uuid, sizeof(charStruct.uuid) / sizeof(wchar_t), to_hstring(c.Uuid()).c_str());
					// retrieve user description
					GattDescriptorsResult descriptorScan = co_await c.GetDescriptorsForUuidAsync(make_guid(L"00002901-0000-1000-8000-00805F9B34FB"), BluetoothCacheMode::Uncached);
					if (descriptorScan.Descriptors().Size() == 0) {
						const wchar_t* defaultDescription = L"no description available";
						wcscpy_s(charStruct.userDescription, sizeof(charStruct.userDescription) / sizeof(wchar_t), defaultDescription);
					}
					else {
						GattDescriptor descriptor = descriptorScan.Descriptors().GetAt(0);
						auto nameResult = co_await descriptor.ReadValueAsync();
						if (nameResult.Status() != GattCommunicationStatus::Success)
							saveError(L"%s:%d couldn't read user description for charasteristic %s, status %d", __WFILE__, __LINE__, to_hstring(c.Uuid()).c_str(), nameResult.Status());
						else {
							auto dataReader = DataReader::FromBuffer(nameResult.Value());
							auto output = dataReader.ReadString(dataReader.UnconsumedBufferLength());
							wcscpy_s(charStruct.userDescription, sizeof(charStruct.userDescription) / sizeof(wchar_t), output.c_str());
							clearError();
						}
					}
					{
						lock_guard lock(quitLock);
						if (quitFlag)
							break;
					}
					{
						lock_guard queueGuard(characteristicQueueLock);
						characteristicQueue.push(charStruct);
						characteristicQueueSignal.notify_one();
					}
				}
			}
		}
	}
	catch (hresult_error& ex)
	{
		saveError(L"%s:%d ScanCharacteristicsAsync catch: %s", __WFILE__, __LINE__, ex.message().c_str());
	}
	{
		lock_guard lock(characteristicQueueLock);
		characteristicScanFinished = true;
		characteristicQueueSignal.notify_one();
	}
}

void ScanCharacteristics(wchar_t* deviceId, wchar_t* serviceId) {
	ScanCharacteristicsAsync(deviceId, serviceId);
}

ScanStatus PollCharacteristic(Characteristic* characteristic, bool block) {
	ScanStatus res;
	unique_lock<mutex> lock(characteristicQueueLock);
	if (block && characteristicQueue.empty() && !characteristicScanFinished)
		if (QuittableWait(characteristicQueueSignal, lock))
			return ScanStatus::FINISHED;
	if (!characteristicQueue.empty()) {
		*characteristic = characteristicQueue.front();
		characteristicQueue.pop();
		res = ScanStatus::AVAILABLE;
	}
	else if (characteristicScanFinished)
		res = ScanStatus::FINISHED;
	else
		res = ScanStatus::PROCESSING;
	return res;
}

void Characteristic_ValueChanged(GattCharacteristic const& characteristic, GattValueChangedEventArgs args)
{
	BLEData data;
	wcscpy_s(data.characteristicUuid, sizeof(data.characteristicUuid) / sizeof(wchar_t), to_hstring(characteristic.Uuid()).c_str());
	wcscpy_s(data.serviceUuid, sizeof(data.serviceUuid) / sizeof(wchar_t), to_hstring(characteristic.Service().Uuid()).c_str());
	wcscpy_s(data.deviceId, sizeof(data.deviceId) / sizeof(wchar_t), characteristic.Service().Device().DeviceId().c_str());

	data.size = args.CharacteristicValue().Length();
	// IBuffer to array, copied from https://stackoverflow.com/a/55974934
	memcpy(data.buf, args.CharacteristicValue().data(), data.size);

	{
		lock_guard lock(quitLock);
		if (quitFlag)
			return;
	}
	{
		lock_guard queueGuard(dataQueueLock);
		dataQueue.push(data);
		dataQueueSignal.notify_one();
	}
}
fire_and_forget SubscribeCharacteristicAsync(wchar_t* deviceId, wchar_t* serviceId, wchar_t* characteristicId, bool* result) {
	try {
		auto characteristic = co_await retrieveCharacteristic(deviceId, serviceId, characteristicId);
		if (characteristic != nullptr) {
			auto status = co_await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify);
			if (status != GattCommunicationStatus::Success)
				saveError(L"%s:%d Error subscribing to characteristic with uuid %s and status %d", __WFILE__, __LINE__, characteristicId, status);
			else {
				Subscription *subscription = new Subscription();
				subscription->characteristic = characteristic;
				subscription->revoker = characteristic.ValueChanged(auto_revoke, &Characteristic_ValueChanged);
				subscriptions.push_back(subscription);
				if (result != 0)
					*result = true;
			}
		}
	}
	catch (hresult_error& ex)
	{
		saveError(L"%s:%d SubscribeCharacteristicAsync catch: %s", __WFILE__, __LINE__, ex.message().c_str());
	}
	subscribeQueueSignal.notify_one();
}
bool SubscribeCharacteristic(wchar_t* deviceId, wchar_t* serviceId, wchar_t* characteristicId, bool block) {
	unique_lock<mutex> lock(subscribeQueueLock);
	bool result = false;
	SubscribeCharacteristicAsync(deviceId, serviceId, characteristicId, block ? &result : 0);
	if (block && QuittableWait(subscribeQueueSignal, lock))
		return false;

	return result;
}

bool PollData(BLEData* data, bool block) {
	unique_lock<mutex> lock(dataQueueLock);
	if (block && dataQueue.empty())
		if (QuittableWait(dataQueueSignal, lock))
			return false;
	if (!dataQueue.empty()) {
		*data = dataQueue.front();
		dataQueue.pop();
		return true;
	}
	return false;
}

fire_and_forget SendDataAsync(BLEData data, condition_variable* signal, bool* result) {
	try {
		auto characteristic = co_await retrieveCharacteristic(data.deviceId, data.serviceUuid, data.characteristicUuid);
		if (characteristic != nullptr) {
			// create IBuffer from data
			DataWriter writer;
			writer.WriteBytes(array_view<uint8_t const> (data.buf, data.buf + data.size));
			IBuffer buffer = writer.DetachBuffer();
			auto status = co_await characteristic.WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse);
			if (status != GattCommunicationStatus::Success)
				saveError(L"%s:%d Error writing value to characteristic with uuid %s", __WFILE__, __LINE__, data.characteristicUuid);
			else if (result != 0)
				*result = true;
		}
	}
	catch (hresult_error& ex)
	{
		saveError(L"%s:%d SendDataAsync catch: %s", __WFILE__, __LINE__, ex.message().c_str());
	}
	if (signal != 0)
		signal->notify_one();
}
bool SendData(BLEData* data, bool block) {
	mutex _mutex;
	unique_lock<mutex> lock(_mutex);
	condition_variable signal;
	bool result = false;
	// copy data to stack so that caller can free its memory in non-blocking mode
	SendDataAsync(*data, block ? &signal : 0, block ? &result : 0);
	if (block)
		signal.wait(lock);

	return result;
}

void Quit() {
	{
		lock_guard lock(quitLock);
		quitFlag = true;
	}
	StopDeviceScan();
	deviceQueueSignal.notify_one();
	{
		lock_guard lock(deviceQueueLock);
		deviceQueue = {};
	}
	serviceQueueSignal.notify_one();
	{
		lock_guard lock(serviceQueueLock);
		serviceQueue = {};
	}
	characteristicQueueSignal.notify_one();
	{
		lock_guard lock(characteristicQueueLock);
		characteristicQueue = {};
	}
	subscribeQueueSignal.notify_one();
	{
		lock_guard lock(subscribeQueueLock);
		for (auto subscription : subscriptions)
			subscription->revoker.revoke();
		subscriptions = {};
	}
	dataQueueSignal.notify_one();
	{
		lock_guard lock(dataQueueLock);
		dataQueue = {};
	}
	for (auto device : cache) {
		device.second.device.Close();
		for (auto service : device.second.services) {
			service.second.service.Close();
		}
	}
	cache.clear();
}

void GetError(ErrorMessage* buf) {
	lock_guard error_lock(errorLock);
	wcscpy_s(buf->msg, last_error);
}