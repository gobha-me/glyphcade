// Authoring helper for test/18audio-synth's compact numeric fingerprints.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

#include <glyphcade/audio/synth.hpp>

int main() {
  using namespace glyphcade::audio;
  constexpr int rate = 48000;
  constexpr float zero_band = 1.0F / 1000.0F;
  constexpr std::string_view names[]{
      "Click",    "Reveal",   "Flag",       "Explode",     "Win",
      "Lose",     "MenuMove", "MenuSelect", "Slide",       "Merge",
      "Eat",      "Lock",     "Tetris",     "LevelUp",     "Seat",
      "CardDeal", "CardFlip", "CardPlace",  "InvalidMove", "CardFoundation"};

  for (std::size_t index = 0; index < kSfxIds.size(); ++index) {
    const auto id = kSfxIds[index];
    const auto& spec = spec_for(id);
    const int frames = frames_for(spec, rate);
    std::vector<float> samples(static_cast<std::size_t>(frames), 0.0F);
    Voice voice;
    voice.trigger(spec, rate, id, 1);
    voice.render_add(samples.data(), frames);

    double peak = 0.0;
    double sumsq = 0.0;
    int sign = 0;
    int crossings = 0;
    for (float sample : samples) {
      peak = std::max(peak, static_cast<double>(std::fabs(sample)));
      sumsq += static_cast<double>(sample) * sample;
      const int current =
          sample > zero_band ? 1 : (sample < -zero_band ? -1 : 0);
      if (current != 0) {
        if (sign != 0 && current != sign) ++crossings;
        sign = current;
      }
    }
    const double rms = std::sqrt(sumsq / frames);
    std::cout << names[index] << ' ' << frames << ' ' << peak << ' ' << rms
              << ' ' << crossings << '\n';
  }
}
