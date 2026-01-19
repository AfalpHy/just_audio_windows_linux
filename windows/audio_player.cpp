#include "audio_player.h"

namespace just_audio_windows_linux {

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
            event_sink_ = std::move(sink);
            return nullptr;
          },
          // onCancel
          [this](const flutter::EncodableValue *arguments) {
            event_sink_.reset();
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
            data_sink_ = std::move(sink);
            return nullptr;
          },
          // onCancel
          [this](const flutter::EncodableValue *arguments) {
            data_sink_.reset();
            return nullptr;
          }));
}

AudioPlayer::~AudioPlayer() { player_channel_->SetMethodCallHandler(nullptr); }

bool AudioPlayer::load(const std::string &uri) {
  ma_decoder_config decoder_config =
      ma_decoder_config_init(ma_format_f32, 0, 0);

  if (ma_decoder_init_file(uri.c_str(), &decoder_config, &decoder_) !=
      MA_SUCCESS) {
    return false;
  }

  ma_device_config device_config =
      ma_device_config_init(ma_device_type_playback);

  device_config.playback.format = decoder_.outputFormat;
  device_config.playback.channels = decoder_.outputChannels;
  device_config.sampleRate = decoder_.outputSampleRate;
  device_config.dataCallback = AudioPlayer::DataCallback;
  device_config.pUserData = this;

  if (ma_device_init(nullptr, &device_config, &device_) != MA_SUCCESS) {
    ma_decoder_uninit(&decoder_);
    return false;
  }

  return true;
}

void AudioPlayer::play() {
  if (!initialized_)
    return;

  if (state_ != PlayerState::PLAYING) {
    ma_device_start(&device_);
    state_ = PlayerState::PLAYING;
  }
}

void AudioPlayer::pause() {
  if (!initialized_)
    return;

  if (state_ == PlayerState::PLAYING) {
    ma_device_stop(&device_);
    state_ = PlayerState::PAUSED;
  }
}

void AudioPlayer::stop() {
  if (!initialized_)
    return;

  ma_device_stop(&device_);
  ma_decoder_seek_to_pcm_frame(&decoder_, 0);
  current_frame_ = 0;
  state_ = PlayerState::STOPPED;
}

void AudioPlayer::seek(int64_t positionMs) {
  if (!initialized_)
    return;

  ma_uint64 frame = (positionMs * decoder_.outputSampleRate) / 1000;

  ma_decoder_seek_to_pcm_frame(&decoder_, frame);
  current_frame_ = frame;
}

int64_t AudioPlayer::position() {
  if (!initialized_)
    return 0;
  return (current_frame_ * 1000) / decoder_.outputSampleRate;
}

int64_t AudioPlayer::duration() {
  if (!initialized_)
    return 0;

  ma_uint64 total_frames = 0;
  ma_decoder_get_length_in_pcm_frames(&decoder_, &total_frames);

  return (total_frames * 1000) / decoder_.outputSampleRate;
}

void AudioPlayer::DataCallback(ma_device *device, void *output, const void *,
                               ma_uint32 frameCount) {
  auto *self = static_cast<AudioPlayer *>(device->pUserData);

  ma_uint64 frames_read = 0;
  ma_decoder_read_pcm_frames(&self->decoder_, output, frameCount, &frames_read);

  float *samples = static_cast<float *>(output);
  double gain = self->volume_.load();
  ma_uint32 channels = device->playback.channels;

  for (ma_uint64 i = 0; i < frames_read * channels; ++i) {
    samples[i] *= static_cast<float>(gain);
  }

  self->current_frame_ += frames_read;

  if (frames_read < frameCount) {
    std::memset(samples + frames_read * channels, 0,
                (frameCount - frames_read) * channels * sizeof(float));
    self->state_ = PlayerState::STOPPED;
  }
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
    for (auto &child : *children) {
      const auto *childMap = std::get_if<flutter::EncodableMap>(&child);
      const auto *child =
          std::get_if<flutter::EncodableMap>(getValue(childMap, "child"));
      const auto *uri = std::get_if<std::string>(getValue(child, "uri"));
      std::cout << *uri << std::endl;
      result->Success(load(*uri));
    }
  } else if (method == "play") {
    play();
    result->Success();
  } else if (method == "pause") {
    pause();
    result->Success();
  } else if (method == "stop") {
    stop();
    result->Success();
  } else if (method == "seek") {
    auto pos = std::get<int64_t>(*method_call.arguments());
    seek(pos);
    result->Success();
  } else if (method == "position") {
    result->Success(position());
  } else if (method == "duration") {
    result->Success(duration());
  } else {
    result->NotImplemented();
  }
}

} // namespace just_audio_windows_linux