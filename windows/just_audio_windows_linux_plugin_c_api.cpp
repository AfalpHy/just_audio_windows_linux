#include "include/just_audio_windows_linux/just_audio_windows_linux_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "just_audio_windows_linux_plugin.h"

void JustAudioWindowsLinuxPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  just_audio_windows_linux::JustAudioWindowsLinuxPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
