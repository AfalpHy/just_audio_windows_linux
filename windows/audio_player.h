#pragma once

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "miniaudio.h"

namespace just_audio_windows_linux {

enum class PlayerState { IDLE, LOADING, READY = 3, COMPLETED = 4 };

class AudioPlayer {
private:
  std::string id_;

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      player_channel_;
  std::unique_ptr<flutter::EventSink<>> event_sink_ = nullptr;
  std::unique_ptr<flutter::EventSink<>> data_sink_ = nullptr;

  ma_context context_;
  ma_decoder decoder_{};
  ma_device device_{};
  std::atomic<ma_uint64> current_frame_{0};
  ma_uint64 seek_frame_{0};
  bool need_seek_ = false;
  PlayerState state_{PlayerState::IDLE};
  std::atomic<double> volume_{1.0};
  double speed_ = 1.0;
  bool initialized_ = false;
  bool playing_ = false;
  int64_t duration_ = 0;
  static void DataCallback(ma_device *device, void *output, const void *input,
                           ma_uint32 frameCount);

public:
  AudioPlayer(std::string id, flutter::BinaryMessenger *messenger);
  ~AudioPlayer();

  bool load(std::string uri);
  void play();
  void pause();
  void seek(int64_t positionMs);

  int64_t position();

  void sendPlaybackEvent();
  void sendPlaybackData();

  void AudioPlayer::HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
};

} // namespace just_audio_windows_linux