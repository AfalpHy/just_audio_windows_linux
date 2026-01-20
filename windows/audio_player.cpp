#define MA_IMPLEMENTATION
#include "audio_player.h"

#include <chrono>

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

std::wstring DecodeUrlToWstring(const std::string &encoded) {
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

  // UTF-8 -> wstring
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(),
                                        (int)bytes.size(), nullptr, 0);
  std::wstring wstr(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), &wstr[0],
                      size_needed);

  return wstr;
}

const flutter::EncodableValue *getValue(const flutter::EncodableMap *map,
                                        const char *key) {
  auto it = map->find(flutter::EncodableValue(key));
  if (it == map->end()) {
    return nullptr;
  }
  return &(it->second);
}

AudioPlayer::AudioPlayer(std::string id, flutter::BinaryMessenger *messenger)
    : id_(id) {

  player_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          messenger, "com.ryanheise.just_audio.methods." + id,
          &flutter::StandardMethodCodec::GetInstance());

  player_channel_->SetMethodCallHandler([this](const auto &call, auto result) {
    this->HandleMethodCall(call, std::move(result));
  });

  auto event_channel =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          messenger, "com.ryanheise.just_audio.events." + id,
          &flutter::StandardMethodCodec::GetInstance());

  event_channel->SetStreamHandler(
      std::make_unique<
          flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
          // onListen
          [this](const flutter::EncodableValue *arguments,
                 std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>
                     &&sink) {
            this->event_sink_ = std::move(sink);
            return nullptr;
          },
          // onCancel
          [this](const flutter::EncodableValue *arguments) {
            this->event_sink_.reset();
            return nullptr;
          }));

  auto data_channel =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          messenger, "com.ryanheise.just_audio.data." + id,
          &flutter::StandardMethodCodec::GetInstance());

  data_channel->SetStreamHandler(
      std::make_unique<
          flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
          // onListen
          [this](const flutter::EncodableValue *arguments,
                 std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>
                     &&sink) {
            this->data_sink_ = std::move(sink);
            return nullptr;
          },
          // onCancel
          [this](const flutter::EncodableValue *arguments) {
            this->data_sink_.reset();
            return nullptr;
          }));
}

AudioPlayer::~AudioPlayer() {
  if (player_channel_) {
    player_channel_->SetMethodCallHandler(nullptr);
  }
  if (initialized_) {
    ma_device_uninit(&device_);
    ma_decoder_uninit(&decoder_);
  }
}

bool AudioPlayer::load(std::string uri) {
  current_frame_ = 0;
  state_ = PlayerState::LOADING;
  sendPlaybackEvent();
  uri = uri.substr(8);

  if (initialized_) {
    ma_device_stop(&device_);
    ma_device_uninit(&device_);
    ma_decoder_uninit(&decoder_);
    ma_context_uninit(&context_);
    initialized_ = false;
  }
  if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
    return false;
  }

  ma_decoder_config decoder_config =
      ma_decoder_config_init(ma_format_f32, 2, 44100);
  if (ma_decoder_init_file_w(DecodeUrlToWstring(uri).c_str(), &decoder_config,
                             &decoder_) != MA_SUCCESS) {
    ma_context_uninit(&context_);

    return false;
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
  ma_device_start(&device_);
  playing_ = true;
  state_ = PlayerState::READY;
}

void AudioPlayer::pause() {
  ma_device_stop(&device_);
  playing_ = false;
  state_ = PlayerState::READY;
}

void AudioPlayer::seek(int64_t positionMs) {
  ma_uint64 frames = positionMs * (int64_t)decoder_.outputSampleRate / 1000000;
  seek_frame_ = frames;
  need_seek_ = true;
}

int64_t AudioPlayer::position() {
  if (!initialized_) {
    return 0;
  }
  return (current_frame_ * 1000000) / decoder_.outputSampleRate;
}

void AudioPlayer::DataCallback(ma_device *device, void *output, const void *,
                               ma_uint32 frameCount) {
  auto *self = static_cast<AudioPlayer *>(device->pUserData);
  if (self->state_ == PlayerState::COMPLETED) {
    return;
  }

  ma_uint64 frames_read = 0;
  ma_decoder_read_pcm_frames(&self->decoder_, output, frameCount, &frames_read);

  float *samples = static_cast<float *>(output);
  double gain = self->volume_.load();
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
  auto eventData = flutter::EncodableMap();

  eventData[flutter::EncodableValue("processingState")] =
      flutter::EncodableValue((int)state_);

  eventData[flutter::EncodableValue("updatePosition")] =
      flutter::EncodableValue(position()); // int

  eventData[flutter::EncodableValue("bufferedPosition")] =
      flutter::EncodableValue(duration_); // int
  eventData[flutter::EncodableValue("duration")] =
      flutter::EncodableValue(duration_); // int
  auto now = std::chrono::system_clock::now();

  eventData[flutter::EncodableValue("updateTime")] = flutter::EncodableValue(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count()); // int

  // I don't care about the following data
  eventData[flutter::EncodableValue("currentIndex")] =
      flutter::EncodableValue(0); // int

  event_sink_->Success(eventData);
}

void AudioPlayer::sendPlaybackData() {
  auto eventData = flutter::EncodableMap();

  eventData[flutter::EncodableValue("playing")] =
      flutter::EncodableValue(playing_);
  eventData[flutter::EncodableValue("volume")] =
      flutter::EncodableValue(volume_.load());
  eventData[flutter::EncodableValue("speed")] = flutter::EncodableValue(speed_);

  // I don't care about the following data
  eventData[flutter::EncodableValue("loopMode")] = flutter::EncodableValue(0);
  eventData[flutter::EncodableValue("shuffleMode")] =
      flutter::EncodableValue(0);

  data_sink_->Success(eventData);
}

void AudioPlayer::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const auto &method = method_call.method_name();
  const auto *args =
      std::get_if<flutter::EncodableMap>(method_call.arguments());

  if (method == "load") {
    const auto *audioSourceData =
        std::get_if<flutter::EncodableMap>(getValue(args, "audioSource"));

    const auto *children = std::get_if<flutter::EncodableList>(
        getValue(audioSourceData, "children"));

    const auto *childMap = std::get_if<flutter::EncodableMap>(&children->at(0));

    const auto *uri = std::get_if<std::string>(getValue(childMap, "uri"));

    load(*uri);
  } else if (method == "play") {
    play();
  } else if (method == "pause") {
    pause();
  } else if (method == "seek") {
    const auto *position = getValue(args, "position");
    if (position != nullptr) {
      seek((*position).LongValue());
    }
  } else if (method == "setVolume") {
    const auto *volume = std::get_if<double>(getValue(args, "volume"));
    volume_.store(*volume);
  } else if (method == "setSpeed") {
    // const auto *speed = std::get_if<double>(getValue(args, "speed"));
    std::cerr << "not implement yet" << std::endl;
  } else {
    // I don't care other method
  }
  result->Success(flutter::EncodableMap());
}

} // namespace just_audio_windows_linux