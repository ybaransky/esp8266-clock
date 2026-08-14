#pragma once

class DisplayManager;
class SoundPlayer;

// The output devices a scheduled mode controller drives when its phase changes.
//
// Friday and Trading modes install a base view and, on a live boundary
// crossing, announce it. An announcement is a message and a sound together, so
// the controllers need both collaborators; bundling them keeps one parameter on
// tick() and gives a later output (a relay, an LED) somewhere to go without
// touching every signature again.
//
// Held by reference: ClockApplication owns both for the life of the program,
// and a controller must never outlive or copy them.
struct ModeOutputs {
  DisplayManager& display;  // Base view and overlay target.
  SoundPlayer& sound;       // Boundary-announcement audio.
};
