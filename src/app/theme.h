#pragma once
// The Studio's look: deep purples with a violet accent, so it sits with the rest of the ZeroPoint tooling.
// Plain Qt (Fusion style + palette + a little stylesheet): the same on macOS, Linux and Windows.

class QApplication;

namespace theme {

void apply(QApplication& app);

// Colours other widgets reuse.
constexpr const char* kAccent = "#8b5cf6";
constexpr const char* kBad = "#ef5b6b";
constexpr const char* kWarn = "#f0b44c";
constexpr const char* kGood = "#4cc38a";
constexpr const char* kMuted = "#8b8fb3";

} // namespace theme
