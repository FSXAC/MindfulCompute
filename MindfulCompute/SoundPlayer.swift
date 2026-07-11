import AVFoundation

@MainActor
final class SoundPlayer {
    static let shared = SoundPlayer()

    enum Sound: String {
        case bowl = "tibetan_bowl"
    }

    private var player: AVAudioPlayer?

    func play(_ sound: Sound) {
        guard let url = Bundle.main.url(forResource: sound.rawValue, withExtension: "wav") else {
            return
        }
        player = try? AVAudioPlayer(contentsOf: url)
        player?.play()
    }
}
