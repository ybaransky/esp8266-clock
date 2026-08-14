#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "config.h"  // kSoundNameLength

// What a catalog entry is used for. Both kinds share one catalog, one binary
// layout, and one player - they differ only in which dropdown offers them, so
// this classifies entries without splitting anything else in two. Stored per
// directory entry in /songs.bin; must match KIND_* in tools/pack_songs.py.
enum class SoundKind : uint8_t {
  kAlert = 0,  // Short burst; authored in assets/sounds.json.
  kSong = 1,   // Full melody; authored in assets/songs/*.json.
};

// Plays one sound at a time from the packed /songs.bin catalog, advancing one
// note per tick() rather than blocking.
//
// The catalog format is streamed, not loaded: only the pitch table (at most 256
// bytes) is cached, and notes are read two bytes at a time from an open file
// handle. Playback is a deadline state machine so a 40-second song costs the
// main loop nothing between note boundaries - the tone itself is produced by
// the ESP8266 waveform generator and keeps sounding without software help.
// That is the whole reason this is not a port of esp8266-sound's player, which
// blocks in a delay loop and re-enters its caller through a service callback.
class SoundPlayer {
 public:
  // Mounts storage if needed and validates the catalog header. Returns false
  // when no catalog is available; the object stays usable and silent.
  bool begin();

  // Advances at most one note boundary. Cheap and safe to call every loop.
  void tick(uint32_t nowMs);

  // Starts `name`, replacing whatever is playing. An empty or unknown name
  // stops playback and returns false, so "no sound configured" needs no
  // special case at any call site.
  bool play(const char* name, uint32_t nowMs);

  void stop();
  bool isPlaying() const { return phase_ != Phase::kIdle; }

  // Name of the sound currently playing, or an empty string when idle.
  const char* playingName() const { return playingName_; }

  // True when a valid catalog was found; false means every play() is silent.
  bool catalogAvailable() const { return catalogLoaded_; }

  // 0-100. Scales the PWM duty cycle, so it takes effect within the current
  // note rather than at the next one.
  void setVolume(uint8_t percent);
  uint8_t volume() const { return volumePercent_; }

  // Fills `array` with the name of every catalog entry of `kind`, in catalog
  // (alphabetical) order. Returns false when the catalog is unreadable.
  bool namesAsJson(JsonArray array, SoundKind kind);

  // How long `name` takes to play, in milliseconds; 0 when it is not in the
  // catalog. Reads the record without disturbing playback. Lets a browser know
  // when a preview ends without polling the device for it.
  uint32_t durationMs(const char* name);

 private:
  // What the player is doing between note boundaries. A note is a tone
  // followed by a short gap that articulates it from the next one; both are
  // timed here because PWM, unlike tone(duration), runs until it is stopped.
  enum class Phase : uint8_t { kIdle, kSounding, kGap };

  // One entry of the catalog directory.
  struct SongEntry {
    char name[kSoundNameLength] = "";  // Sound name; identity used everywhere.
    uint16_t offset = 0;      // Absolute file offset of the note record.
    uint16_t tempo = 0;       // Beats per minute; 0 selects simple timing.
    uint16_t noteCount = 0;   // Notes in the record.
    SoundKind kind = SoundKind::kSong;  // Which dropdown offers this entry.
  };

  bool loadCatalogHeader();
  uint32_t directoryStart() const;
  bool readEntry(File& file, SongEntry& entry) const;
  bool findSong(File& file, const char* name, SongEntry& entry) const;

  // Reads the next note and enters kSounding, or ends playback at the record's
  // end. `startMs` is the deadline the note is scheduled from, which is the
  // previous note's deadline rather than millis() so rounding cannot drift.
  bool startNextNote(uint32_t startMs);
  void startTone(uint16_t frequencyHz);
  void stopTone();
  void endPlayback();

  File record_;  // Open catalog file while playing; closed by endPlayback().
  Phase phase_ = Phase::kIdle;  // Current playback phase.
  uint32_t phaseEndsAtMs_ = 0;  // Deadline for the current tone or gap.
  uint16_t gapMs_ = 0;          // Gap that follows the tone now sounding.
  uint16_t notesRemaining_ = 0;  // Notes left in the record being played.
  uint32_t wholeNoteMs_ = 0;  // Whole-note length; 0 selects simple timing.
  char playingName_[kSoundNameLength] = "";  // Name of the sound in progress.

  uint16_t pitchTable_[128] = {};  // Distinct frequencies, indexed by records.
  uint8_t pitchCount_ = 0;  // Valid entries in pitchTable_.
  uint8_t songCount_ = 0;   // Directory entries in the catalog.
  bool catalogLoaded_ = false;  // True once the header validated successfully.
  bool toneSounding_ = false;  // True while the buzzer is actively driven.
  uint8_t volumePercent_ = 40;  // Loudness as a percentage of full duty.
};
