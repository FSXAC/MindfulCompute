#pragma once
// Bowl playback, re-expressed from MindfulCompute/SoundPlayer.swift.
// PlaySound(SND_FILENAME | SND_ASYNC) with the wav resolved next to the exe
// (CMake copies assets/sounds/tibetan_bowl.wav there at build time).
namespace Sound {
// Play the tibetan bowl asynchronously. Logs the PlaySound return value.
void playBowl();
}
