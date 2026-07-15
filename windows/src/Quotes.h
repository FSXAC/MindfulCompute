#pragma once
// Portable core: quote data ported verbatim from MindfulCompute/Quotes.swift.
// `author` empty means no attribution (the Swift `author: nil` cases).
#include <string>
#include <vector>
#include <cstdlib>

struct Quote {
    std::wstring text;
    std::wstring author;   // empty == anonymous (Swift author: nil)
};

namespace Quotes {

inline const std::vector<Quote>& all() {
    static const std::vector<Quote> q = {
        { L"How we spend our days is, of course, how we spend our lives.", L"Annie Dillard" },
        { L"The present moment is the only time over which we have dominion.", L"Thích Nhất Hạnh" },
        { L"It is not that we have a short time to live, but that we waste a lot of it.", L"Seneca" },
        { L"Rest is not idleness, and to lie sometimes on the grass under trees on a summer's day is by no means a waste of time.", L"John Lubbock" },
        { L"Tell me, what is it you plan to do with your one wild and precious life?", L"Mary Oliver" },
        { L"Nature does not hurry, yet everything is accomplished.", L"Lao Tzu" },
        { L"You have power over your mind — not outside events. Realize this, and you will find strength.", L"Marcus Aurelius" },
        { L"In an age of speed, nothing could be more invigorating than going slow.", L"Pico Iyer" },
        { L"Almost everything will work again if you unplug it for a few minutes, including you.", L"Anne Lamott" },
        { L"The quieter you become, the more you can hear.", L"Ram Dass" },
        { L"Wherever you are, be there totally.", L"Eckhart Tolle" },
        { L"Walk as if you are kissing the Earth with your feet.", L"Thích Nhất Hạnh" },
        { L"The unexamined life is not worth living.", L"Socrates" },
        { L"Happiness depends upon ourselves.", L"Aristotle" },
        { L"The greatest wealth is to live content with little.", L"Plato" },
        { L"No man ever steps in the same river twice, for it is not the same river and he is not the same man.", L"Heraclitus" },
        { L"He who has a why to live can bear almost any how.", L"Friedrich Nietzsche" },
        { L"Waste no more time arguing what a good person should be. Be one.", L"Marcus Aurelius" },
        { L"Very little is needed to make a happy life; it is all within yourself, in your way of thinking.", L"Marcus Aurelius" },
        { L"We suffer more often in imagination than in reality.", L"Seneca" },
        { L"To be everywhere is to be nowhere.", L"Seneca" },
        { L"The only true wisdom is in knowing you know nothing.", L"Socrates" },
        { L"It is not enough to be busy. So are the ants. The question is: What are we busy about?", L"Henry David Thoreau" },
        { L"Our life is frittered away by detail. Simplify, simplify.", L"Henry David Thoreau" },
        { L"The price of anything is the amount of life you exchange for it.", L"Henry David Thoreau" },
        { L"Adopt the pace of nature: her secret is patience.", L"Ralph Waldo Emerson" },
        { L"Nothing can bring you peace but yourself.", L"Ralph Waldo Emerson" },
        { L"All I have seen teaches me to trust the Creator for all I have not seen.", L"Ralph Waldo Emerson" },
        { L"Do not spoil what you have by desiring what you have not.", L"Epicurus" },
        { L"If you are distressed by anything external, the pain is not due to the thing itself, but to your estimate of it.", L"Marcus Aurelius" },
        { L"No great thing is created suddenly.", L"Epictetus" },
        { L"First say to yourself what you would be; and then do what you have to do.", L"Epictetus" },
        { L"How much more grievous are the consequences of anger than the causes of it.", L"Marcus Aurelius" },
        { L"All of humanity's problems stem from man's inability to sit quietly in a room alone.", L"Blaise Pascal" },
        { L"Life can only be understood backwards; but it must be lived forwards.", L"Soren Kierkegaard" },
        { L"Well-being is attained little by little, and nevertheless is no little thing itself.", L"Zeno of Citium" },
        { L"Be patient toward all that is unsolved in your heart and try to love the questions themselves.", L"Rainer Maria Rilke" },
        { L"No one saves us but ourselves. No one can and no one may. We ourselves must walk the path.", L"Buddha" },
        { L"Every morning we are born again. What we do today is what matters most.", L"Buddha" },
        { L"Peace comes from within. Do not seek it without.", L"Buddha" },
        { L"When walking, walk. When eating, eat.", L"Zen proverb" },
        { L"The obstacle is the path.", L"Zen proverb" },
        { L"Do every act of your life as though it were the very last act of your life.", L"Marcus Aurelius" },
        { L"If you seek tranquility, do less.", L"Marcus Aurelius" },
        { L"You did what you came to do. That is enough.", L"" },
        { L"Your eyes have carried you this far. Let them rest on something distant.", L"" },
        { L"Stepping away is part of the work.", L"" },
        { L"The screen will keep. Go stretch, breathe, look out a window.", L"" },
        { L"Look up. The room is still here, and so are you.", L"" },
        { L"Blink slowly. Your eyes have earned gentleness.", L"" },
        { L"Leave one thing unfinished: your habit of hurrying.", L"" },
        { L"Breathe out the urgency that no longer serves you.", L"" },
        { L"You can return sharper by leaving softer.", L"" },
        { L"Today did not require perfection. It required presence.", L"" },
        { L"Stand up as if your future self asked you to.", L"" },
    };
    return q;
}

inline const Quote& random() {
    const auto& q = all();
    return q[static_cast<size_t>(rand()) % q.size()];
}

} // namespace Quotes
