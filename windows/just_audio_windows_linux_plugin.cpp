#include "just_audio_windows_linux_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <sstream>

#include "audio_player.h"

namespace just_audio_windows_linux {

std::unique_ptr<AudioPlayer> player;
// static
void JustAudioWindowsLinuxPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {

  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;

  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "com.ryanheise.just_audio.methods",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<JustAudioWindowsLinuxPlugin>();

  channel->SetMethodCallHandler([plugin_pointer = plugin.get(),
                                 messenger_pointer = registrar->messenger()](
                                    const auto &call, auto result) {
    plugin_pointer->HandleMethodCall(call, std::move(result),
                                     std::move(messenger_pointer));
  });

  registrar->AddPlugin(std::move(plugin));
}

JustAudioWindowsLinuxPlugin::JustAudioWindowsLinuxPlugin() {}

JustAudioWindowsLinuxPlugin::~JustAudioWindowsLinuxPlugin() {}

void JustAudioWindowsLinuxPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result,
    flutter::BinaryMessenger *messenger) {

  auto getValue = [](const flutter::EncodableMap &map,
                     const char *key) -> const flutter::EncodableValue * {
    auto it = map.find(flutter::EncodableValue(key));
    if (it == map.end()) {
      return nullptr;
    }
    return &(it->second);
  };

  const auto *args =
      std::get_if<flutter::EncodableMap>(method_call.arguments());
  if (args) {
    if (method_call.method_name().compare("init") == 0) {
      const auto *id = std::get_if<std::string>(getValue(*args, "id"));
      assert(id != nullptr);
      if (player != nullptr) {
        return result->Error("only support one player");
      } else {
        player = std::make_unique<AudioPlayer>(*id, messenger);
        result->Success();
      }
    } else if (method_call.method_name().compare("disposePlayer") == 0) {
      player = nullptr;
      result->Success(flutter::EncodableMap());
    } else if (method_call.method_name().compare("disposeAllPlayers") == 0) {
      player = nullptr;
      result->Success(flutter::EncodableMap());
    } else {
      result->NotImplemented();
    }
  } else {
    result->NotImplemented();
  }
}

} // namespace just_audio_windows_linux