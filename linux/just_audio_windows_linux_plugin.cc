#include "include/just_audio_windows_linux/just_audio_windows_linux_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <cstring>
#include <memory>
#include <string>

#include "audio_player.h"
#include <iostream>
#define JUST_AUDIO_WINDOWS_LINUX_PLUGIN(obj)                                   \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),                                           \
                              just_audio_windows_linux_plugin_get_type(),      \
                              JustAudioWindowsLinuxPlugin))

struct _JustAudioWindowsLinuxPlugin {
  GObject parent_instance;
  FlBinaryMessenger *messenger;
};

G_DEFINE_TYPE(JustAudioWindowsLinuxPlugin, just_audio_windows_linux_plugin,
              g_object_get_type())

/* ---------------- Global player (same behavior as Windows) ---------------- */

static std::unique_ptr<just_audio_windows_linux::AudioPlayer> player;

/* ---------------- Method handler ---------------- */

static void just_audio_windows_linux_plugin_handle_method_call(
    JustAudioWindowsLinuxPlugin *self, FlMethodCall *method_call) {

  const gchar *method = fl_method_call_get_name(method_call);
  FlValue *args = fl_method_call_get_args(method_call);

  /* -------- init -------- */
  if (strcmp(method, "init") == 0) {
    if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
      fl_method_call_respond_error(method_call, "invalid_args",
                                   "arguments must be a map", nullptr, nullptr);
      return;
    }

    FlValue *id_value = fl_value_lookup_string(args, "id");
    if (id_value == nullptr ||
        fl_value_get_type(id_value) != FL_VALUE_TYPE_STRING) {
      fl_method_call_respond_error(method_call, "invalid_args", "missing id",
                                   nullptr, nullptr);
      return;
    }

    if (player) {
      fl_method_call_respond_error(method_call, "error",
                                   "only support one player", nullptr, nullptr);
      return;
    }

    const char *id = fl_value_get_string(id_value);

    player = std::make_unique<just_audio_windows_linux::AudioPlayer>(
        id, self->messenger);

    fl_method_call_respond_success(method_call, nullptr, nullptr);
    return;
  }

  /* -------- disposePlayer -------- */
  if (strcmp(method, "disposePlayer") == 0) {
    player.reset();
    fl_method_call_respond_success(method_call, fl_value_new_map(), nullptr);
    return;
  }

  /* -------- disposeAllPlayers -------- */
  if (strcmp(method, "disposeAllPlayers") == 0) {
    player.reset();
    fl_method_call_respond_success(method_call, fl_value_new_map(), nullptr);
    return;
  }

  fl_method_call_respond_not_implemented(method_call, nullptr);
}

/* ---------------- GObject lifecycle ---------------- */

static void just_audio_windows_linux_plugin_dispose(GObject *object) {
  player.reset();
  G_OBJECT_CLASS(just_audio_windows_linux_plugin_parent_class)->dispose(object);
}

static void just_audio_windows_linux_plugin_class_init(
    JustAudioWindowsLinuxPluginClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = just_audio_windows_linux_plugin_dispose;
}

static void
just_audio_windows_linux_plugin_init(JustAudioWindowsLinuxPlugin *self) {}

/* ---------------- Channel callback ---------------- */

static void method_call_cb(FlMethodChannel *channel, FlMethodCall *method_call,
                           gpointer user_data) {

  JustAudioWindowsLinuxPlugin *plugin =
      JUST_AUDIO_WINDOWS_LINUX_PLUGIN(user_data);

  just_audio_windows_linux_plugin_handle_method_call(plugin, method_call);
}

/* ---------------- Plugin registration ---------------- */

void just_audio_windows_linux_plugin_register_with_registrar(
    FlPluginRegistrar *registrar) {

  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;

  JustAudioWindowsLinuxPlugin *plugin = JUST_AUDIO_WINDOWS_LINUX_PLUGIN(
      g_object_new(just_audio_windows_linux_plugin_get_type(), nullptr));

  plugin->messenger = fl_plugin_registrar_get_messenger(registrar);

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();

  g_autoptr(FlMethodChannel) channel = fl_method_channel_new(
      plugin->messenger, "com.ryanheise.just_audio.methods",
      FL_METHOD_CODEC(codec));

  fl_method_channel_set_method_call_handler(
      channel, method_call_cb, g_object_ref(plugin), g_object_unref);

  g_object_unref(plugin);
}
