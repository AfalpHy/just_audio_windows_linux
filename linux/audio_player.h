#pragma once

#include <flutter_linux/flutter_linux.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "miniaudio.h"

namespace just_audio_windows_linux {

/* ---------------- Player state ---------------- */

enum class PlayerState { IDLE, LOADING, READY = 3, COMPLETED = 4 };

/* ---------------- AudioPlayer ---------------- */

class AudioPlayer {
public:
  AudioPlayer(const std::string &id, FlBinaryMessenger *messenger);
  ~AudioPlayer();

  /* -------- control -------- */
  bool load(const std::string &uri);
  void play();
  void pause();
  void stop();
  void seek(int64_t positionMs);

  /* -------- query -------- */
  int64_t position();

  /* -------- flutter method dispatch -------- */
  void HandleMethodCall(FlMethodCall *method_call);

  /* -------- flutter channels -------- */
  FlMethodChannel *player_channel_ = nullptr;

  /* -------- miniaudio -------- */
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

private:
  /* -------- miniaudio callback -------- */
  static void DataCallback(ma_device *device, void *output, const void *input,
                           ma_uint32 frameCount);

  /* -------- helpers -------- */
  void sendPlaybackEvent();
  void sendPlaybackData();
};

} // namespace just_audio_windows_linux
