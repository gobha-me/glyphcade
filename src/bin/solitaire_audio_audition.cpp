// Manual hardware audition for Solitaire's five game-specific effects.

#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <thread>

#include <glyphcade/audio/device_sink.hpp>
#include <glyphcade/audio/engine.hpp>
#include <glyphcade/audio/sink.hpp>
#include <glyphcade/audio/synth.hpp>

namespace {

using glyphcade::audio::SfxId;

struct Effect {
  std::string_view name;
  SfxId id;
};

constexpr std::array kEffects{
    Effect{"Card deal", SfxId::CardDeal},
    Effect{"Card flip", SfxId::CardFlip},
    Effect{"Card place", SfxId::CardPlace},
    Effect{"Invalid move", SfxId::InvalidMove},
    Effect{"Card foundation", SfxId::CardFoundation},
};

} // namespace

auto main() -> int {
  using namespace std::chrono_literals;
  using glyphcade::audio::Engine;
  using glyphcade::audio::SinkKind;

  Engine engine;
  auto opened = engine.open(glyphcade::audio::make_device_sink());
  if (!opened) {
    std::cerr << "Solitaire audio audition: " << opened.error() << '\n';
    return 1;
  }

  if (engine.kind() == SinkKind::Discard) {
    std::cerr << "Solitaire audio audition: this build has no device backend; "
                 "reconfigure with -DGLYPHCADE_WITH_AUDIO=ON\n";
    return 2;
  }
  if (engine.kind() == SinkKind::Offline) {
    std::cerr << "Solitaire audio audition: GLYPHCADE_AUDIO_WAV selected the "
                 "offline sink; unset it to use the audio device\n";
    return 2;
  }

  std::cout << "Solitaire audio audition through " << engine.sink_name()
            << ". Each effect plays twice.\n";
  for (std::size_t i = 0; i < kEffects.size(); ++i) {
    const auto &effect = kEffects[i];
    std::cout << (i + 1) << '/' << kEffects.size() << ' ' << effect.name
              << std::endl;
    if (!engine.play(effect.id)) {
      std::cerr << "Solitaire audio audition: command queue rejected "
                << effect.name << '\n';
      return 1;
    }
    std::this_thread::sleep_for(400ms);
    if (!engine.play(effect.id)) {
      std::cerr << "Solitaire audio audition: command queue rejected "
                << effect.name << '\n';
      return 1;
    }
    std::this_thread::sleep_for(1s);
  }

  std::cout << "Audition complete.\n";
  return 0;
}
