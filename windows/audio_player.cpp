#include "audio_player.h"

#include <mferror.h>
#include <propvarutil.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplay.lib")

namespace just_audio_windows_linux {

const flutter::EncodableValue *getValue(const flutter::EncodableMap &map,
                                        const char *key) {
  auto it = map.find(flutter::EncodableValue(key));
  if (it == map.end()) {
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
                     &&sink) { event_sink_ = std::move(sink); },
          // onCancel
          [this](const flutter::EncodableValue *arguments) {
            event_sink_.reset();
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
                     &&sink) { data_sink_ = std::move(sink); },
          // onCancel
          [this](const flutter::EncodableValue *arguments) {
            data_sink_.reset();
          }));

  HRESULT hr = MFStartup(MF_VERSION);
  assert(SUCCEEDED(hr));

  hr = MFPCreateMediaPlayer(nullptr, FALSE, MFP_OPTION_NONE, this, nullptr,
                            &player_);

  assert(SUCCEEDED(hr));
}

AudioPlayer::~AudioPlayer() {
  player_channel_->SetMethodCallHandler(nullptr);

  if (player_) {
    player_->Shutdown();
  }
  MFShutdown();
}

bool AudioPlayer::load(const std::wstring &uri) {
  if (!player_)
    return false;

  state_ = State::LOADING;
  durationMs_ = 0;

  HRESULT hr = player_->CreateMediaItemFromURL(uri.c_str(),
                                               FALSE, // async
                                               0, nullptr);

  if (FAILED(hr)) {
    return false;
  }
  return true;
}

void AudioPlayer::play() {
  if (!player_)
    return;

  if (SUCCEEDED(player_->Play())) {
    state_ = State::PLAYING;
  }
}

void AudioPlayer::pause() {
  if (!player_)
    return;

  if (SUCCEEDED(player_->Pause())) {
    state_ = State::PAUSED;
  }
}

void AudioPlayer::stop() {
  if (!player_)
    return;

  player_->Stop();
  state_ = State::IDLE;
}

void AudioPlayer::seek(int64_t positionMs) {
  if (!player_)
    return;

  PROPVARIANT var;
  PropVariantInit(&var);

  var.vt = VT_I8;
  var.hVal.QuadPart = positionMs * 10000; // ms → 100ns

  player_->SetPosition(GUID_NULL, &var);
  PropVariantClear(&var);
}

int64_t AudioPlayer::position() const {
  if (!player_)
    return 0;

  return 0;
}

int64_t AudioPlayer::duration() const { return durationMs_; }

void STDMETHODCALLTYPE
AudioPlayer::OnMediaPlayerEvent(MFP_EVENT_HEADER *event_header) {
  if (!event_header)
    return;

  switch (event_header->eEventType) {
  case MFP_EVENT_TYPE_MEDIAITEM_CREATED: {
    auto *created =
        reinterpret_cast<MFP_MEDIAITEM_CREATED_EVENT *>(event_header);
    if (created->pMediaItem) {
      player_->SetMediaItem(created->pMediaItem);
    }
    break;
  }

  case MFP_EVENT_TYPE_MEDIAITEM_SET: {
    auto *set = reinterpret_cast<MFP_MEDIAITEM_SET_EVENT *>(event_header);
    if (set->pMediaItem) {
      PROPVARIANT var;
      PropVariantInit(&var);

      PropVariantClear(&var);
    }
    break;
  }

  case MFP_EVENT_TYPE_PLAY:
    state_ = State::PLAYING;
    if (event_sink_) {
      event_sink_->Success("playing");
    }
    break;

  case MFP_EVENT_TYPE_PAUSE:
    state_ = State::PAUSED;
    if (event_sink_) {
      event_sink_->Success("paused");
    }
    break;

  case MFP_EVENT_TYPE_STOP:
    state_ = State::IDLE;
    if (event_sink_) {
      event_sink_->Success("stopped");
    }
    break;

  case MFP_EVENT_TYPE_PLAYBACK_ENDED:
    state_ = State::COMPLETED;
    if (event_sink_) {
      event_sink_->Success("completed");
    }
    break;

  case MFP_EVENT_TYPE_ERROR:
    if (event_sink_) {
      event_sink_->Success("error");
    }
    break;

  default:
    break;
  }
}

void AudioPlayer::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const auto &method = method_call.method_name();

  if (method == "load") {
    std::cout << method << std::endl;
    assert(0);
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
    const auto *args =
        std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (args) {
      auto it = args->find(flutter::EncodableValue("position"));
      if (it != args->end()) {
        seek(std::get<int64_t>(it->second));
      }
    }
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