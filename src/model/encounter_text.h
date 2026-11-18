// Contains built-in story variants indexed by the encounters stored in a run.

#pragma once

#include <array>

namespace wars::encounter_text {

inline constexpr std::array black_hat{
    "A mysterious figure in a black hat smells of stale energy drinks and questionable ethics.",
    "Psst. Fresh 0days. Or a leather jacket with more pockets than a surveillance program.",
    "A black hat emerges from an alley. 'I've got 0days, jackets, and absolutely no customer support.'",
    "The stranger adjusts his black hat. 'Guaranteed to work until somebody notices.'",
    "A vendor in a black hat offers two kinds of protection. Only one comes with a zipper.",
    "The figure in the black hat offers you a 0day and a jacket. Both have fewer holes than his business plan.",
    "Someone in a black hat opens a briefcase. The jacket has pockets. The other item has plausible deniability.",
    "A black hat hands you a loyalty card. 'Buy ten, get a sternly worded advisory.'",
};

struct MuggingStory {
    const char* carrying;
    const char* empty;
};

inline constexpr std::array<MuggingStory, 6> mugging{{
    {"A desperate founder corners you outside a cafe and calls the contents of your jacket a pre-seed acquisition.",
     "The founder searches your pockets, discovers you're poorer than his startup, and offers you an internship."},
    {"Fake conference security takes your cargo for inspection and disappears through a door marked Definitely Not A Scam.",
     "The fake security guard finds nothing in your jacket and asks if you're hiring."},
    {"A man in a Decentralize Everything shirt decentralizes your cargo into his own pockets.",
     "The crypto bro finds nothing worth stealing. He shrugs and tries to sell you a token."},
    {"A delivery robot classifies your cargo as an unclaimed package and flees at a breathtaking 4 km/h.",
     "The robot scans your empty pockets, displays ERROR: INSUFFICIENT ASSETS, and slowly drives away."},
    {"A recruiter promises a seven-figure salary and free kombucha. Some of your cargo disappears during the pitch.",
     "The recruiter finds nothing to steal and immediately offers you an executive position."},
    {"A pickpocket uses an AI agent to optimize your mugging. You receive a customer satisfaction survey.",
     "The thief's AI agent calculates a negative return on robbing you. It recommends buying you lunch."},
}};

inline constexpr std::array stash{
    "You find an abandoned server rack behind a failed startup. A note reads 'Please don't tell our investors.'",
    "Another AI startup goes bankrupt. Someone leaves a box of components beside the recycling.",
    "You find an unmarked shipping container. The manifest says Definitely Not GPUs.",
    "A tech influencer dumps a shipment because it won't fit in a YouTube thumbnail.",
    "You find spare gear from a cancelled AI conference. Apparently the keynote speaker was also generated.",
    "A self-driving delivery van encounters a roundabout and abandons its cargo.",
    "A lab throws out working gear because the new model requires something with a bigger number.",
    "A founder pays you in leftover cargo after discovering his revolutionary product is an API wrapper.",
};

inline constexpr std::array officer{
    "Those GPUs are underutilized. I'm confiscating them in the name of progress.",
    "You call that ownership? I call it inefficient resource allocation.",
    "Your hardware has potential. You, unfortunately, do not.",
    "Nothing personal. It's just artificial business.",
    "You can't spell monopoly without... actually, never mind. Hand it over.",
    "I'm not stealing your hardware. I'm democratizing its ownership. To me.",
    "Your commodities deserve a better future. Preferably in my data centre.",
    "The road to AGI is paved with other people's GPUs.",
    "I'm offering you a strategic partnership. You provide the hardware. I keep it.",
    "For the benefit of humanity, I'm going to need everything in your pockets.",
};

inline constexpr auto fight = "Your 0day worked! You've thwarted Ram Altman today!";

inline constexpr std::array escape{
    "You slip down an alley while Officer Ram Altman pitches an investor on the commercial potential of chasing you.",
    "You escape while the acquisition team waits for approval to cross the street.",
    "You duck into a repair shop. Officer Ram Altman loses interest when he hears the words 'right to repair.'",
};

inline constexpr std::array confiscation{
    "Ram Altman's team catches you. He calls taking your goods a 'strategic partnership'.",
    "Ram Altman and his goons apprehend you. He laughs, saying, 'Your inventory... acquired. Your consent... not required.'",
    "You fail to escape Ram Altman. As you lie stunned, you can hear him say, 'Resistance is futile... but available on our Pro plan.'",
    "You cannot escape Ram Altman and his goons! As you lay stunned you hear him say, 'Thank you for participating in our involuntary asset transfer program.'",
    "Try as you might, you fail to escape. You hear Ram Altman mutter, 'Your escape attempt exceeded your free-tier limits.'",
    "You fail to escape. As Ram Altman's goons rummage through your goods you hear him mutter, 'You can keep your dignity. It has no resale value.'",
    "Ram Altman and his goons catch you! Ram Altman proclaims, 'You weren't hoarding GPUs. You were temporarily storing mine.'",
};

} // namespace wars::encounter_text
