#ifndef FLUTTER_PLUGIN_JUST_AUDIO_WINDOWS_LINUX_PLUGIN_H_
#define FLUTTER_PLUGIN_JUST_AUDIO_WINDOWS_LINUX_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>

namespace just_audio_windows_linux {

class JustAudioWindowsLinuxPlugin : public flutter::Plugin {
public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  JustAudioWindowsLinuxPlugin();

  virtual ~JustAudioWindowsLinuxPlugin();

  // Disallow copy and assign.
  JustAudioWindowsLinuxPlugin(const JustAudioWindowsLinuxPlugin &) = delete;
  JustAudioWindowsLinuxPlugin &
  operator=(const JustAudioWindowsLinuxPlugin &) = delete;

  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result,
      flutter::BinaryMessenger *messenger);
};

} // namespace just_audio_windows_linux

#endif // FLUTTER_PLUGIN_JUST_AUDIO_WINDOWS_LINUX_PLUGIN_H_