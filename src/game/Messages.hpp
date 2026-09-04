#pragma once
// Server chat style constants - section-sign prefixes for broadcast/feedback texts. Text CONTENT is unchanged (wire-identical); only the
#include <string>

namespace cppfm::msg {

inline const std::string kGray   = "\u00a77"; // info / system text
inline const std::string kRed    = "\u00a7c"; // errors / death
inline const std::string kYellow = "\u00a7e"; // join / leave
inline const std::string kPink   = "\u00a7d"; // [Server] say prefix
inline const std::string kAqua   = "\u00a7b"; // brand highlight (welcome message)

} // namespace cppfm::msg
