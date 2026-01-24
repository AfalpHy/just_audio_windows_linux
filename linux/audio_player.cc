#define MINIAUDIO_IMPLEMENTATION
#include "audio_player.h"

#include "miniaudio_libopus.c"
#include "miniaudio_libvorbis.c"

#include <cstring>
#include <iostream>

namespace just_audio_windows_linux {

inline unsigned char HexCharToValue(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return 0;
}

std::string decodeURL(const std::string &encoded) {
  std::string bytes;
  bytes.reserve(encoded.size());

  for (size_t i = 0; i < encoded.size(); ++i) {
    char ch = encoded[i];
    if (ch == '+') {
      bytes += ' ';
    } else if (ch == '%' && i + 2 < encoded.size()) {
      unsigned char high = HexCharToValue(encoded[i + 1]);
      unsigned char low = HexCharToValue(encoded[i + 2]);
      bytes += static_cast<char>((high << 4) | low);
      i += 2;
    } else {
      bytes += ch;
    }
  }

  return bytes;
}

static FlEventChannel *event_channel = nullptr;
static FlEventChannel *data_channel = nullptr;

static gboolean send_playback_event_cb(gpointer user_data) {
  AudioPlayer *player = (AudioPlayer *)user_data;

  FlValue *map = fl_value_new_map();

  fl_value_set_string(map, "processingState",
                      fl_value_new_int((int)player->state_));

  fl_value_set_string(map, "updatePosition",
                      fl_value_new_int(player->position()));

  fl_value_set_string(map, "bufferedPosition",
                      fl_value_new_int(player->duration_));

  fl_value_set_string(map, "duration", fl_value_new_int(player->duration_));

  gint64 now_ms = g_get_real_time() / 1000;
  fl_value_set_string(map, "updateTime", fl_value_new_int(now_ms));

  fl_value_set_string(map, "currentIndex", fl_value_new_int(0));

  fl_event_channel_send(event_channel, map, NULL, NULL);

  return G_SOURCE_REMOVE;
}

static gboolean send_playback_data_cb(gpointer user_data) {
  AudioPlayer *player = (AudioPlayer *)user_data;

  FlValue *map = fl_value_new_map();

  fl_value_set_string(map, "playing", fl_value_new_bool(player->playing_));

  fl_value_set_string(map, "volume", fl_value_new_float(player->volume_));

  fl_value_set_string(map, "speed", fl_value_new_float(1.0));

  fl_value_set_string(map, "loopMode", fl_value_new_int(0));

  fl_value_set_string(map, "shuffleMode", fl_value_new_int(0));

  fl_event_channel_send(data_channel, map, NULL, NULL);

  return G_SOURCE_REMOVE;
}

static FlValue *lookup_map(FlValue *map, const char *key) {
  if (map == nullptr || fl_value_get_type(map) != FL_VALUE_TYPE_MAP) {
    return nullptr;
  }
  return fl_value_lookup_string(map, key);
}

/* ---------------- ctor / dtor ---------------- */

AudioPlayer::AudioPlayer(const std::string &id, FlBinaryMessenger *messenger) {

  /* -------- method channel -------- */
  player_channel_ = fl_method_channel_new(
      messenger, ("com.ryanheise.just_audio.methods." + id).c_str(),
      FL_METHOD_CODEC(fl_standard_method_codec_new()));

  fl_method_channel_set_method_call_handler(
      player_channel_,
      [](FlMethodChannel *, FlMethodCall *call, gpointer user_data) {
        static_cast<AudioPlayer *>(user_data)->HandleMethodCall(call);
      },
      this, nullptr);

  /* -------- event channel -------- */
  event_channel = fl_event_channel_new(
      messenger, ("com.ryanheise.just_audio.events." + id).c_str(),
      FL_METHOD_CODEC(fl_standard_method_codec_new()));

  /* -------- data channel -------- */
  data_channel = fl_event_channel_new(
      messenger, ("com.ryanheise.just_audio.data." + id).c_str(),
      FL_METHOD_CODEC(fl_standard_method_codec_new()));
}

AudioPlayer::~AudioPlayer() {
  if (player_channel_) {
    fl_method_channel_set_method_call_handler(player_channel_, nullptr, nullptr,
                                              nullptr);
  }

  if (initialized_) {
    ma_device_uninit(&device_);
    ma_decoder_uninit(&decoder_);
  }
}

/* ---------------- audio control ---------------- */

bool AudioPlayer::load(const std::string &uri) {
  current_frame_ = 0;
  state_ = PlayerState::LOADING;
  sendPlaybackEvent();

  std::string path = uri;
  path = path.substr(7);
  path = decodeURL(path);

  if (initialized_) {
    ma_device_stop(&device_);
    ma_device_uninit(&device_);
    ma_decoder_uninit(&decoder_);
    ma_context_uninit(&context_);
    initialized_ = false;
  }

  if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
    state_ = PlayerState::READY;
    sendPlaybackEvent();

    return false;
  }

  ma_decoder_config decoder_config =
      ma_decoder_config_init(ma_format_f32, 2, 44100);

  if (ma_decoder_init_file(path.c_str(), &decoder_config, &decoder_) !=
      MA_SUCCESS) {
    decoder_config = ma_decoder_config_init_default();

    ma_decoding_backend_vtable *pCustomBackendVTables[] = {
        ma_decoding_backend_libopus, ma_decoding_backend_libvorbis};
    decoder_config.pCustomBackendUserData = NULL;
    decoder_config.ppCustomBackendVTables = pCustomBackendVTables;
    decoder_config.customBackendCount =
        sizeof(pCustomBackendVTables) / sizeof(pCustomBackendVTables[0]);

    if (ma_decoder_init_file(path.c_str(), &decoder_config, &decoder_) !=
        MA_SUCCESS) {
      ma_decoder_uninit(&decoder_);
      ma_context_uninit(&context_);

      state_ = PlayerState::READY;
      sendPlaybackEvent();

      return false;
    }
  }

  ma_device_config device_config =
      ma_device_config_init(ma_device_type_playback);
  device_config.playback.format = decoder_.outputFormat;
  device_config.playback.channels = decoder_.outputChannels;
  device_config.sampleRate = decoder_.outputSampleRate;
  device_config.dataCallback = AudioPlayer::DataCallback;
  device_config.pUserData = this;

  if (ma_device_init(&context_, &device_config, &device_) != MA_SUCCESS) {
    ma_decoder_uninit(&decoder_);
    ma_context_uninit(&context_);
    state_ = PlayerState::READY;
    sendPlaybackEvent();

    return false;
  }

  initialized_ = true;

  ma_uint64 total_frames = 0;
  ma_decoder_get_length_in_pcm_frames(&decoder_, &total_frames);
  duration_ = (total_frames * 1000000) / decoder_.outputSampleRate;

  if (playing_) {
    ma_device_start(&device_);
  }

  state_ = PlayerState::READY;
  sendPlaybackEvent();

  return true;
}

void AudioPlayer::play() {
  playing_ = true;
  state_ = PlayerState::READY;
  if (!initialized_) {
    return;
  }
  ma_device_start(&device_);
}

void AudioPlayer::pause() {
  playing_ = false;
  state_ = PlayerState::READY;
  if (!initialized_) {
    return;
  }
  ma_device_stop(&device_);
}

void AudioPlayer::stop() {
  playing_ = false;
  state_ = PlayerState::IDLE;
  if (!initialized_) {
    return;
  }
  ma_device_stop(&device_);
}

void AudioPlayer::seek(int64_t positionMs) {
  if (!initialized_) {
    return;
  }
  ma_uint64 frames = positionMs * (int64_t)decoder_.outputSampleRate / 1000000;
  seek_frame_ = frames;
  need_seek_ = true;
}
/* ---------------- queries ---------------- */

int64_t AudioPlayer::position() {
  if (!initialized_) {
    return 0;
  }
  return (current_frame_ * 1000000) / decoder_.outputSampleRate;
}

/* ---------------- miniaudio callback ---------------- */

void AudioPlayer::DataCallback(ma_device *device, void *output, const void *,
                               ma_uint32 frameCount) {
  auto *self = static_cast<AudioPlayer *>(device->pUserData);
  if (self->state_ == PlayerState::COMPLETED) {
    return;
  }

  ma_uint64 frames_read = 0;
  ma_decoder_read_pcm_frames(&self->decoder_, output, frameCount, &frames_read);

  float *samples = static_cast<float *>(output);
  double gain = self->volume_;
  ma_uint32 channels = device->playback.channels;

  for (ma_uint64 i = 0; i < frames_read * channels; ++i) {
    samples[i] *= static_cast<float>(gain);
  }

  self->current_frame_ += frames_read;

  if (self->need_seek_) {
    self->need_seek_ = false;
    ma_decoder_seek_to_pcm_frame(&self->decoder_, self->seek_frame_);
    self->current_frame_ = self->seek_frame_;
    return;
  }

  if (frames_read < frameCount) {
    std::memset(samples + frames_read * channels, 0,
                (frameCount - frames_read) * channels * sizeof(float));
    self->state_ = PlayerState::COMPLETED;
    self->sendPlaybackEvent();
  }
}

void AudioPlayer::sendPlaybackEvent() {
  g_main_context_invoke(NULL, send_playback_event_cb, this);
}

void AudioPlayer::sendPlaybackData() {
  g_main_context_invoke(NULL, send_playback_data_cb, this);
}

/* ---------------- flutter method dispatch ---------------- */

void AudioPlayer::HandleMethodCall(FlMethodCall *method_call) {
  const gchar *method = fl_method_call_get_name(method_call);
  FlValue *args = fl_method_call_get_args(method_call);

  if (strcmp(method, "load") == 0) {
    FlValue *audio_source = lookup_map(args, "audioSource");
    FlValue *children = lookup_map(audio_source, "children");
    for (size_t i = 0; i < fl_value_get_length(children); ++i) {
      FlValue *child = fl_value_get_list_value(children, i);

      FlValue *uri_val = lookup_map(child, "uri");

      const char *uri = fl_value_get_string(uri_val);
      load(uri);
    }
  } else if (strcmp(method, "play") == 0) {
    play();
  } else if (strcmp(method, "pause") == 0) {
    pause();
  } else if (strcmp(method, "stop") == 0) {
    stop();
  } else if (strcmp(method, "seek") == 0) {
    seek(fl_value_get_int(lookup_map(args, "position")));
  } else if (strcmp(method, "setVolume") == 0) {
    FlValue *volume = lookup_map(args, "volume");
    volume_ = fl_value_get_float(volume);
  } else {
    // I don't care
  }
  fl_method_call_respond_success(method_call, fl_value_new_map(), nullptr);
}

} // namespace just_audio_windows_linux
