import AVFoundation
import os

@MainActor
final class SoundPlayer {
    static let shared = SoundPlayer()

    enum Sound: String {
        case bowl = "tibetan_bowl"
    }

    private static let log = Logger(subsystem: "MindfulCompute", category: "SoundPlayer")

    private var players: [Sound: AVAudioPlayer] = [:]

    func play(_ sound: Sound) {
        guard let player = player(for: sound) else { return }
        // Rings fire during heavy animation; reuse the primed player and rewind.
        player.currentTime = 0
        player.play()
    }

    private func player(for sound: Sound) -> AVAudioPlayer? {
        if let cached = players[sound] { return cached }
        guard let url = Bundle.main.url(forResource: sound.rawValue, withExtension: "wav") else {
            Self.log.error("Missing sound resource: \(sound.rawValue, privacy: .public)")
            return nil
        }
        guard let player = try? AVAudioPlayer(contentsOf: url) else { return nil }
        player.prepareToPlay()
        players[sound] = player
        return player
    }
}
