#include "xbl_manager.h"
#include "halley/support/logger.h"
#include "halley/text/halleystring.h"
#include "halley/concurrency/concurrent.h"

#include <map>

#include <vccorlib.h>
#include <winrt/base.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Gaming.XboxLive.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include "xsapi/services.h"
#include <ppltasks.h>

using namespace Halley;

template <typename T>
T from_cx(Platform::Object^ from)
{
    T to{ nullptr };

    winrt::check_hresult(reinterpret_cast<::IUnknown*>(from)
        ->QueryInterface(winrt::guid_of<T>(),
            reinterpret_cast<void**>(winrt::put_abi(to))));

    return to;
}

template <typename T>
T^ to_cx(winrt::Windows::Foundation::IUnknown const& from)
{
    return safe_cast<T^>(reinterpret_cast<Platform::Object^>(winrt::get_abi(from)));
}

XBLManager::XBLManager()
{
	
}

XBLManager::~XBLManager()
{
	deInit();
}

void XBLManager::init()
{
	signIn();
}

void XBLManager::deInit()
{
}

std::shared_ptr<ISaveData> XBLManager::getSaveContainer(const String& name)
{
	auto iter = saveStorage.find(name);
	if (iter == saveStorage.end()) {
		auto save = std::make_shared<XBLSaveData>(*this, name);
		saveStorage[name] = save;
		return save;
	} else {
		return iter->second;
	}
}

Maybe<winrt::Windows::Gaming::XboxLive::Storage::GameSaveProvider> XBLManager::getProvider() const
{
	return gameSaveProvider;
}

XBLStatus XBLManager::getStatus() const
{
	return status;
}

void XBLManager::signIn()
{
	status = XBLStatus::Connecting;

	using namespace xbox::services::system;
	xboxUser = std::make_shared<xbox_live_user>(nullptr);
	auto dispatcher = to_cx<Platform::Object>(winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Dispatcher());

	xboxUser->signin_silently(dispatcher).then([=] (xbox::services::xbox_live_result<sign_in_result> result) -> winrt::Windows::Foundation::IAsyncAction
	{
		if (result.err()) {
			Logger::logError(String("Error signing in to Xbox Live: ") + result.err_message());
			status = XBLStatus::Disconnected;
		} else {
			bool loggedIn = false;
			switch (result.payload().status()) {
			case success:
				loggedIn = true;
				break;

			case user_interaction_required:
				xboxUser->signin(dispatcher).then([&](xbox::services::xbox_live_result<sign_in_result> loudResult)
                {
                    if (!loudResult.err()) {
                        auto resPayload = loudResult.payload();
                        switch (resPayload.status()) {
                        case success:
                            loggedIn = true;
                            break;
                        case user_cancel:
                            break;
                        }
                    }
                }, concurrency::task_continuation_context::use_current());
				break;
			}

			if (loggedIn) {
				xboxLiveContext = std::make_shared<xbox::services::xbox_live_context>(xboxUser);

				xbox_live_user::add_sign_out_completed_handler([this](const sign_out_completed_event_args&)
				{
					xboxUser.reset();
					xboxLiveContext.reset();
					gameSaveProvider.reset();
					status = XBLStatus::Disconnected;
				});

				co_await getConnectedStorage();
			} else {
				status = XBLStatus::Disconnected;
			}
		}
	});
}

winrt::Windows::Foundation::IAsyncAction XBLManager::getConnectedStorage()
{
	using namespace winrt::Windows::Gaming::XboxLive::Storage;
	
	auto windowsUser = co_await winrt::Windows::System::User::FindAllAsync();

	GameSaveProviderGetResult result = co_await GameSaveProvider::GetForUserAsync(*windowsUser.First(), xboxLiveContext->application_config()->scid());

	if (result.Status() == GameSaveErrorStatus::Ok) {
		gameSaveProvider = result.Value();
		status = XBLStatus::Connected;
	} else {
		status = XBLStatus::Disconnected;
	}
}

XBLSaveData::XBLSaveData(XBLManager& manager, String containerName)
	: manager(manager)
	, containerName(std::move(containerName))
{
	updateContainer();
}

bool XBLSaveData::isReady() const
{
	return gameSaveContainer.is_initialized();
}

Bytes XBLSaveData::getData(const String& path)
{
	if (!isReady()) {
		throw Exception("Container is not ready yet!");
	}

	auto key = winrt::hstring(path.getUTF16());
	std::vector<winrt::hstring> updates;
	updates.push_back(key);

	auto gameBlob = gameSaveContainer->GetAsync(winrt::single_threaded_vector<winrt::hstring>(std::move(updates)).GetView()).get();

	if (gameBlob.Status() == winrt::Windows::Gaming::XboxLive::Storage::GameSaveErrorStatus::Ok) {
		if (gameBlob.Value().HasKey(key)) {
			auto buffer = gameBlob.Value().Lookup(key);

			auto size = buffer.Length();
			Bytes result(size);
			auto dataReader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
			dataReader.ReadBytes(winrt::array_view<uint8_t>(result));

			return result;
		}
	}

	return {};
}

std::vector<String> XBLSaveData::enumerate(const String& root)
{
	if (!isReady()) {
		throw Exception("Container is not ready yet!");
	}

	// TODO
	return {};
}

void XBLSaveData::setData(const String& path, const Bytes& data, bool commit)
{
	if (!isReady()) {
		throw Exception("Container is not ready yet!");
	}

	auto dataWriter = winrt::Windows::Storage::Streams::DataWriter();
	dataWriter.WriteBytes(winrt::array_view<const uint8_t>(data));

	std::map<winrt::hstring, winrt::Windows::Storage::Streams::IBuffer> updates;
	updates[winrt::hstring(path.getUTF16())] = dataWriter.DetachBuffer();
	auto view = winrt::single_threaded_map(std::move(updates)).GetView();

	gameSaveContainer->SubmitUpdatesAsync(view, {}, L"");	
}

void XBLSaveData::commit()
{
	
}

void XBLSaveData::updateContainer()
{
	if (manager.getStatus() == XBLStatus::Connected) {
		if (!gameSaveContainer) {
			manager.getProvider()->CreateContainer(containerName.getUTF16().c_str());
		}
	} else {
		gameSaveContainer.reset();
	}
}
