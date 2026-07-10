import Foundation

struct Quote {
    let text: String
    let author: String?
}

enum Quotes {
    static let all: [Quote] = [
        Quote(text: "How we spend our days is, of course, how we spend our lives.",
              author: "Annie Dillard"),
        Quote(text: "The present moment is the only time over which we have dominion.",
              author: "Thích Nhất Hạnh"),
        Quote(text: "It is not that we have a short time to live, but that we waste a lot of it.",
              author: "Seneca"),
        Quote(text: "Rest is not idleness, and to lie sometimes on the grass under trees on a summer's day is by no means a waste of time.",
              author: "John Lubbock"),
        Quote(text: "Tell me, what is it you plan to do with your one wild and precious life?",
              author: "Mary Oliver"),
        Quote(text: "Nature does not hurry, yet everything is accomplished.",
              author: "Lao Tzu"),
        Quote(text: "You have power over your mind — not outside events. Realize this, and you will find strength.",
              author: "Marcus Aurelius"),
        Quote(text: "In an age of speed, nothing could be more invigorating than going slow.",
              author: "Pico Iyer"),
        Quote(text: "Almost everything will work again if you unplug it for a few minutes, including you.",
              author: "Anne Lamott"),
        Quote(text: "The quieter you become, the more you can hear.",
              author: "Ram Dass"),
        Quote(text: "Wherever you are, be there totally.",
              author: "Eckhart Tolle"),
        Quote(text: "Walk as if you are kissing the Earth with your feet.",
              author: "Thích Nhất Hạnh"),
        Quote(text: "You did what you came to do. That is enough.",
              author: nil),
        Quote(text: "Your eyes have carried you this far. Let them rest on something distant.",
              author: nil),
        Quote(text: "Stepping away is part of the work.",
              author: nil),
        Quote(text: "The screen will keep. Go stretch, breathe, look out a window.",
              author: nil),
    ]

    static func random() -> Quote {
        all.randomElement()!
    }
}
