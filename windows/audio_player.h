#pragma once

#include <memory>
#include <string>

#include <atomic>
#include <mfapi.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <string>
#include <wrl/client.h>

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

namespace just_audio_windows_linux {

enum class State { IDLE, LOADING, PLAYING, PAUSED, COMPLETED };

class AudioPlayer : public IMFPMediaPlayerCallback {
private:
  std::string id_;

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      player_channel_;
  std::unique_ptr<flutter::EventSink<>> event_sink_ = nullptr;
  std::unique_ptr<flutter::EventSink<>> data_sink_ = nullptr;

  Microsoft::WRL::ComPtr<IMFPMediaPlayer> player_;
  std::atomic<State> state_{State::IDLE};
  int64_t durationMs_{0};

public:
  AudioPlayer(std::string id, flutter::BinaryMessenger *messenger);
  ~AudioPlayer();

  bool load(const std::wstring &uri);
  void play();
  void pause();
  void stop();
  void seek(int64_t positionMs);

  int64_t position() const;
  int64_t duration() const;

  void STDMETHODCALLTYPE
  OnMediaPlayerEvent(MFP_EVENT_HEADER *event_hander) override;

  void AudioPlayer::HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
};

} // namespace just_audio_windows_linux