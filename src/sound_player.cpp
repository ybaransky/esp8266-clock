#include "sound_player.h"

#include <string.h>

#include "hardware.h"
#include "log.h"
#include "storage_manager.h"

// ---------------------------------------------------------------------------
// Software volume control
//
// Notes are produced with a PWM square wave whose duty cycle sets the loudness,
// not with tone(). A square wave is loudest at 50% duty; lowering the duty makes
// it quieter at the same pitch.
//
// To control volume in hardware instead (a potentiometer on the module's VCC,
// see WIRING.md), set USE_SOFTWARE_VOLUME to 0. That reverts to plain
// full-volume tone() so the two attenuations do not stack.
#define USE_SOFTWARE_VOLUME 1

namespace {

// Binary layout of /songs.bin, produced by tools/pack_songs.py from
// assets/sounds.json and assets/songs/*.json. That script and this file are the
// only two places the layout is spelled out; change them together. See
// SOUNDS.md for the full description.
//
//   [0]   8 bytes   "SNG1", uint8 version, uint8 songCount, uint8 pitchCount,
//                   uint8 reserved
//   [8]   2 bytes each   pitchTable[pitchCount], distinct frequencies in Hz
//   then  directory, songCount entries sorted by name:
//                   uint16 offset, uint16 tempo, uint16 noteCount,
//                   uint8 kind, uint8 nameLength, name[nameLength]
//   then  records:  { uint8 pitchIndex, int8 durationDivisor } * noteCount
//
// A note costs two bytes because the catalog draws on 50 distinct pitches, so a
// one-byte index into the cached table is enough.

constexpr char kCatalogPath[] = "/songs.bin";
constexpr char kMagic[] = "SNG1";
constexpr size_t kMagicLength = 4;
constexpr uint8_t kFormatVersion = 2;
constexpr size_t kHeaderSize = 8;
// offset, tempo, noteCount, kind, nameLength.
constexpr size_t kDirectoryEntrySize = 8;

// Must match MAX_PITCHES in tools/pack_songs.py, and the pitchTable_ dimension.
constexpr size_t kPitchTableMax = 128;

constexpr int kPwmRange = 255;
constexpr int kMaxDuty = kPwmRange / 2;  // 50% duty cycle is loudest.

// analogWriteFreq() clamps below 100 Hz, so the catalog's single 82 Hz entry
// sounds an octave-ish sharp. Not worth correcting: it appears in one bass line
// and a piezo reproduces almost nothing down there anyway.
constexpr uint16_t kMinPwmFrequencyHz = 100;

// Playback deadlines advance from the previous deadline, not from millis(), so
// per-note rounding cannot accumulate into drift. If the loop stalls longer
// than this - a WiFi scan, a long flash write - the schedule is re-anchored to
// the current time instead of racing through the backlog to catch up.
constexpr uint32_t kMaxCatchUpMs = 250;
constexpr uint32_t kBoundaryBeepMaxMs = 100;
constexpr uint8_t kBoundaryPhaseCount = 4;

uint16_t readUint16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
}

// Length of one note's slot in milliseconds. wholeNoteMs is 0 in simple mode,
// where a divisor means "this fraction of a second" rather than a note value.
uint32_t noteSlotMs(int8_t divisor, uint32_t wholeNoteMs) {
  if (wholeNoteMs > 0) {
    const int magnitude = (divisor == 0) ? 4 : abs(divisor);
    const uint32_t slot = wholeNoteMs / magnitude;
    // A negative divisor is a dotted note: one and a half times the length.
    return (divisor < 0) ? (slot * 3 / 2) : slot;
  }
  const int magnitude = (divisor <= 0) ? 4 : divisor;
  return 1000u / magnitude;
}

// Total time one note occupies: the tone plus the gap that articulates it from
// the next one. startNextNote() splits the same slot into those two parts, so a
// duration summed from here cannot disagree with what playback actually spends.
uint32_t noteTotalMs(int8_t divisor, uint32_t wholeNoteMs) {
  const uint32_t slotMs = noteSlotMs(divisor, wholeNoteMs);
  if (wholeNoteMs > 0) return slotMs;   // 90% tone, 10% gap, within the slot.
  return slotMs + (slotMs * 30 / 100);  // Full slot of tone, then a 30% gap.
}

// A whole note in milliseconds; 0 keeps a song in simple timing mode.
uint32_t wholeNoteMsFor(uint16_t tempo) {
  return (tempo > 0) ? ((60000u * 4u) / tempo) : 0u;
}

}  // namespace

// -----------------------------------------------------------------------------
// SoundPlayer - catalog access
// -----------------------------------------------------------------------------

bool SoundPlayer::begin() {
#if USE_SOFTWARE_VOLUME
  pinMode(Hardware::Pins::BUZZER, OUTPUT);
  // The buzzer hangs off an inverting NPN whose base pulldown is also what
  // holds this strapping pin LOW at boot; driving it LOW here keeps the module
  // idle. See WIRING.md.
  digitalWrite(Hardware::Pins::BUZZER, LOW);
  analogWriteRange(kPwmRange);
#endif

  if (!storageManager.ensureMounted("sound catalog")) return false;
  return loadCatalogHeader();
}

bool SoundPlayer::loadCatalogHeader() {
  if (catalogLoaded_) return true;

  File file = STORAGE.open(kCatalogPath, "r");
  if (!file) {
    LOG_PRINTF("%s missing; sound is unavailable", kCatalogPath);
    return false;
  }

  uint8_t header[kHeaderSize];
  if (file.read(header, kHeaderSize) != static_cast<int>(kHeaderSize)) {
    LOG_PRINTF("%s is too short to hold a header", kCatalogPath);
    file.close();
    return false;
  }
  if (memcmp(header, kMagic, kMagicLength) != 0) {
    LOG_PRINTF("%s is not a sound catalog", kCatalogPath);
    file.close();
    return false;
  }
  if (header[4] != kFormatVersion) {
    LOG_PRINTF("%s is version %u; this firmware reads version %u - run "
               "'pio run -t uploadfs'",
               kCatalogPath, header[4], kFormatVersion);
    file.close();
    return false;
  }

  songCount_ = header[5];
  pitchCount_ = header[6];
  if (pitchCount_ > kPitchTableMax) {
    LOG_PRINTF("%s has %u pitches; this firmware holds %u", kCatalogPath,
               pitchCount_, static_cast<unsigned>(kPitchTableMax));
    file.close();
    return false;
  }

  for (uint8_t index = 0; index < pitchCount_; ++index) {
    uint8_t pitch[2];
    if (file.read(pitch, 2) != 2) {
      LOG_PRINTF("%s is truncated in the pitch table", kCatalogPath);
      file.close();
      return false;
    }
    pitchTable_[index] = readUint16(pitch);
  }
  file.close();

  catalogLoaded_ = true;
  LOG_PRINTF("Sound catalog: %u sounds, %u distinct pitches", songCount_,
             pitchCount_);
  return true;
}

uint32_t SoundPlayer::directoryStart() const {
  return kHeaderSize + 2u * static_cast<uint32_t>(pitchCount_);
}

// Reads the directory entry at the file's current position and leaves the
// position on the next one. A name longer than the firmware's field is read
// past but not stored, so it simply never matches - the packer rejects those at
// build time, which is where the problem belongs.
bool SoundPlayer::readEntry(File& file, SongEntry& entry) const {
  uint8_t buffer[kDirectoryEntrySize];
  if (file.read(buffer, kDirectoryEntrySize) !=
      static_cast<int>(kDirectoryEntrySize)) {
    return false;
  }
  entry.offset = readUint16(buffer);
  entry.tempo = readUint16(buffer + 2);
  entry.noteCount = readUint16(buffer + 4);
  // An unknown kind reads as a song: a new kind added later would then still
  // appear in a dropdown rather than vanishing from the UI with no explanation.
  entry.kind = (buffer[6] == static_cast<uint8_t>(SoundKind::kAlert))
                   ? SoundKind::kAlert
                   : SoundKind::kSong;

  const uint8_t nameLength = buffer[7];
  const size_t copyLength =
      (nameLength < (kSoundNameLength - 1)) ? nameLength : (kSoundNameLength - 1);
  if (file.read(reinterpret_cast<uint8_t*>(entry.name), copyLength) !=
      static_cast<int>(copyLength)) {
    return false;
  }
  entry.name[copyLength] = '\0';
  const size_t skipped = nameLength - copyLength;
  return (skipped == 0) || file.seek(skipped, SeekCur);
}

bool SoundPlayer::findSong(File& file, const char* name,
                           SongEntry& entry) const {
  if (!file.seek(directoryStart(), SeekSet)) return false;
  for (uint8_t index = 0; index < songCount_; ++index) {
    if (!readEntry(file, entry)) return false;
    if (strcmp(entry.name, name) == 0) return true;
    yield();
  }
  return false;
}

bool SoundPlayer::namesAsJson(JsonArray array, SoundKind kind) {
  if (!loadCatalogHeader()) return false;

  File file = STORAGE.open(kCatalogPath, "r");
  if (!file) return false;
  if (!file.seek(directoryStart(), SeekSet)) {
    file.close();
    return false;
  }

  SongEntry entry;
  for (uint8_t index = 0; index < songCount_; ++index) {
    if (!readEntry(file, entry)) break;
    if (entry.kind == kind) array.add(entry.name);
    yield();
  }
  file.close();
  return true;
}

// Reads the record a chunk at a time and sums each note's slot. Deliberately
// not stored in the catalog: it is derivable, and a stored copy is one more
// field that can disagree with the notes it describes.
//
// Nothing on the device needs this - a boundary cue just plays - so the cost
// falls only on the web preview that asks, never on the announcement path.
uint32_t SoundPlayer::durationMs(const char* name) {
  if ((name == nullptr) || (name[0] == '\0')) return 0;
  if (!loadCatalogHeader()) return 0;

  File file = STORAGE.open(kCatalogPath, "r");
  if (!file) return 0;
  SongEntry entry;
  if (!findSong(file, name, entry) || !file.seek(entry.offset, SeekSet)) {
    file.close();
    return 0;
  }

  const uint32_t wholeNoteMs = wholeNoteMsFor(entry.tempo);
  uint32_t totalMs = 0;
  uint8_t notes[64];  // Even size: a note is two bytes and must not split.
  uint16_t remaining = entry.noteCount;
  while (remaining > 0) {
    const size_t wanted =
        min(sizeof(notes), static_cast<size_t>(remaining) * 2u);
    const int read = file.read(notes, wanted);
    if (read < 2) break;  // Truncated record; report what was measurable.
    for (int at = 0; at + 1 < read; at += 2) {
      totalMs += noteTotalMs(static_cast<int8_t>(notes[at + 1]), wholeNoteMs);
    }
    remaining -= static_cast<uint16_t>(read / 2);
    yield();
  }
  file.close();
  return totalMs;
}

// -----------------------------------------------------------------------------
// SoundPlayer - playback
// -----------------------------------------------------------------------------

void SoundPlayer::startTone(uint16_t frequencyHz) {
  if (frequencyHz == 0) return;  // A rest still consumes its slot.
  toneSounding_ = true;
#if USE_SOFTWARE_VOLUME
  analogWriteFreq((frequencyHz < kMinPwmFrequencyHz) ? kMinPwmFrequencyHz
                                                     : frequencyHz);
  analogWrite(Hardware::Pins::BUZZER,
              (volumePercent_ * kMaxDuty) / 100);
#else
  tone(Hardware::Pins::BUZZER, frequencyHz);
#endif
}

void SoundPlayer::stopTone() {
  toneSounding_ = false;
#if USE_SOFTWARE_VOLUME
  analogWrite(Hardware::Pins::BUZZER, 0);
  // Hold the pin LOW so the module is silent and the strap stays satisfied.
  digitalWrite(Hardware::Pins::BUZZER, LOW);
#else
  noTone(Hardware::Pins::BUZZER);
#endif
}

void SoundPlayer::setVolume(uint8_t percent) {
  volumePercent_ = (percent > 100) ? 100 : percent;
#if USE_SOFTWARE_VOLUME
  // Re-apply the duty if a note is sounding, so the change is heard now rather
  // than at the next note - a 40-second song would otherwise ignore the slider.
  if (toneSounding_) {
    analogWrite(Hardware::Pins::BUZZER, (volumePercent_ * kMaxDuty) / 100);
  }
#endif
}

void SoundPlayer::endPlayback() {
  stopTone();
  if (record_) record_.close();
  phase_ = Phase::kIdle;
  playback_ = Playback::kIdle;
  notesRemaining_ = 0;
  playingName_[0] = '\0';
}

// Only ever reached from an explicit user request (the /format Stop button via
// ClockController::stopSound); internal stops go through endPlayback(). That is
// why the idle case logs too - a silent console would read as a dead button.
void SoundPlayer::stop() {
  if (playback_ == Playback::kIdle) {
    LOG_PRINTLN("sound: stop requested; nothing playing");
    return;
  }
  LOG_PRINTF("sound: stopped %s", playingName_);
  endPlayback();
}

bool SoundPlayer::play(const char* name, uint32_t nowMs) {
  // An unset cue is the normal way to say "announce this silently", so it stops
  // whatever is playing and reports failure without logging noise.
  if ((name == nullptr) || (name[0] == '\0')) {
    endPlayback();
    return false;
  }
  if (!loadCatalogHeader()) return false;

  endPlayback();

  File file = STORAGE.open(kCatalogPath, "r");
  if (!file) return false;
  SongEntry entry;
  if (!findSong(file, name, entry)) {
    LOG_PRINTF("sound: no sound named %s", name);
    file.close();
    return false;
  }
  if (!file.seek(entry.offset, SeekSet)) {
    file.close();
    return false;
  }

  record_ = file;
  notesRemaining_ = entry.noteCount;
  wholeNoteMs_ = wholeNoteMsFor(entry.tempo);
  strlcpy(playingName_, entry.name, sizeof(playingName_));

  if (!startNextNote(nowMs)) {
    endPlayback();
    return false;
  }
  playback_ = Playback::kCatalog;
  LOG_PRINTF("sound: playing %s (%u notes)", playingName_, entry.noteCount);
  return true;
}

void SoundPlayer::startBoundaryAlert(uint16_t frequencyHz,
                                     uint16_t totalDurationSeconds,
                                     uint8_t startingBeatsHz,
                                     uint32_t startMs,
                                     uint32_t elapsedMs) {
  endPlayback();
  boundaryFrequencyHz_ = frequencyHz;
  boundaryStartingBeatsHz_ = startingBeatsHz;
  boundaryDurationMs_ =
      static_cast<uint32_t>(totalDurationSeconds) * 1000UL;
  boundaryStartedAtMs_ = startMs - elapsedMs;
  playback_ = Playback::kBoundaryAlert;
  strlcpy(playingName_, "boundary alert", sizeof(playingName_));
  tickBoundaryAlert(startMs);
  LOG_PRINTF("sound: boundary alert %u Hz, starts at %u beats/sec, %lu ms remaining",
             frequencyHz, startingBeatsHz,
             boundaryDurationMs_ - elapsedMs);
}

void SoundPlayer::updateBoundaryAlert(uint32_t targetUnix, uint32_t nowUnix,
                                      uint32_t secondStartedAtMs,
                                      uint16_t frequencyHz,
                                      uint16_t totalDurationSeconds,
                                      uint8_t startingBeatsHz) {
  if ((frequencyHz == 0) || (totalDurationSeconds == 0) ||
      (startingBeatsHz == 0) ||
      (targetUnix <= nowUnix)) {
    cancelBoundaryAlert();
    return;
  }

  const bool targetChanged = (armedBoundaryUnix_ != targetUnix);
  if (targetChanged && (playback_ == Playback::kBoundaryAlert)) endPlayback();
  armedBoundaryUnix_ = targetUnix;

  const uint32_t durationMs =
      static_cast<uint32_t>(totalDurationSeconds) * 1000UL;
  const uint32_t remainingSeconds = targetUnix - nowUnix;
  if (remainingSeconds * 1000UL > durationMs) return;

  const uint32_t elapsedMs = durationMs - remainingSeconds * 1000UL;
  if ((playback_ != Playback::kBoundaryAlert) || targetChanged ||
      (boundaryFrequencyHz_ != frequencyHz) ||
      (boundaryDurationMs_ != durationMs) ||
      (boundaryStartingBeatsHz_ != startingBeatsHz)) {
    startBoundaryAlert(frequencyHz, totalDurationSeconds, startingBeatsHz,
                       secondStartedAtMs, elapsedMs);
  }
}

void SoundPlayer::cancelBoundaryAlert() {
  armedBoundaryUnix_ = 0;
  if (playback_ == Playback::kBoundaryAlert) endPlayback();
}

void SoundPlayer::previewBoundaryAlert(uint16_t frequencyHz,
                                       uint16_t totalDurationSeconds,
                                       uint8_t startingBeatsHz,
                                       uint32_t nowMs) {
  armedBoundaryUnix_ = 0;
  startBoundaryAlert(frequencyHz, totalDurationSeconds, startingBeatsHz,
                     nowMs, 0);
}

void SoundPlayer::tickBoundaryAlert(uint32_t nowMs) {
  const uint32_t elapsedMs = nowMs - boundaryStartedAtMs_;
  if (elapsedMs >= boundaryDurationMs_) {
    endPlayback();
    return;
  }

  const uint32_t phaseMs = boundaryDurationMs_ / kBoundaryPhaseCount;
  const uint8_t phaseIndex = elapsedMs / phaseMs;
  const uint16_t rateHz = boundaryStartingBeatsHz_ << phaseIndex;
  const uint32_t withinPhaseMs = elapsedMs % phaseMs;
  const uint32_t pulseIndex = withinPhaseMs * rateHz / 1000UL;
  const uint32_t pulseStartsAtMs = pulseIndex * 1000UL / rateHz;
  const uint32_t nextPulseStartsAtMs =
      (pulseIndex + 1UL) * 1000UL / rateHz;
  const uint32_t pulsePeriodMs = nextPulseStartsAtMs - pulseStartsAtMs;
  const uint32_t halfPeriodMs = pulsePeriodMs / 2U;
  const uint32_t pulseLengthMs =
      (halfPeriodMs < kBoundaryBeepMaxMs) ? halfPeriodMs
                                          : kBoundaryBeepMaxMs;
  // Put the tone at the end of each pulse slot. Besides preserving an audible
  // gap, this makes the last beep end on the scheduled boundary rather than up
  // to half a slot before it.
  const bool shouldSound =
      (withinPhaseMs - pulseStartsAtMs) >=
      (pulsePeriodMs - pulseLengthMs);

  if (shouldSound && !toneSounding_) {
    startTone(boundaryFrequencyHz_);
  } else if (!shouldSound && toneSounding_) {
    stopTone();
  }
}

bool SoundPlayer::startNextNote(uint32_t startMs) {
  if (notesRemaining_ == 0) return false;

  uint8_t note[2];
  if (record_.read(note, 2) != 2) {
    LOG_PRINTF("%s is truncated in %s", kCatalogPath, playingName_);
    return false;
  }
  --notesRemaining_;

  const uint16_t frequencyHz =
      (note[0] < pitchCount_) ? pitchTable_[note[0]] : 0;
  const uint32_t slotMs = noteSlotMs(static_cast<int8_t>(note[1]), wholeNoteMs_);

  // Musical mode articulates with a 10% gap; simple mode uses the divisor as
  // the tone length and adds 30% on top, which is how the short cues were
  // authored. Both come straight from the source project's timing.
  uint32_t toneMs;
  uint32_t gapMs;
  if (wholeNoteMs_ > 0) {
    toneMs = slotMs * 9 / 10;
    gapMs = slotMs - toneMs;
  } else {
    toneMs = slotMs;
    gapMs = slotMs * 30 / 100;
  }

  startTone(frequencyHz);
  phase_ = Phase::kSounding;
  phaseEndsAtMs_ = startMs + toneMs;
  gapMs_ = static_cast<uint16_t>(gapMs);
  return true;
}

void SoundPlayer::tick(uint32_t nowMs) {
  if (playback_ == Playback::kBoundaryAlert) {
    tickBoundaryAlert(nowMs);
    return;
  }
  if (playback_ == Playback::kIdle) return;
  if (static_cast<int32_t>(nowMs - phaseEndsAtMs_) < 0) return;

  // Re-anchor after a stall rather than replaying the backlog at full speed.
  const uint32_t resumeMs =
      ((nowMs - phaseEndsAtMs_) > kMaxCatchUpMs) ? nowMs : phaseEndsAtMs_;

  if (phase_ == Phase::kSounding) {
    stopTone();
    if (gapMs_ > 0) {
      phase_ = Phase::kGap;
      phaseEndsAtMs_ = resumeMs + gapMs_;
      return;
    }
  }

  if (!startNextNote(resumeMs)) endPlayback();
}
