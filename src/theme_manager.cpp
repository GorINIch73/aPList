#include "theme_manager.hpp"

namespace waysheet {

ThemeManager::ThemeManager() {
  themes_.emplace("Light", Theme{"Light", "#F7F9FC", "#FFFFFF", "#3A76F0", "#1C2230"});
  themes_.emplace("Dark", Theme{"Dark", "#1C1F2A", "#232735", "#7CC6FF", "#E8EEFF"});
  themes_.emplace("Ocean", Theme{"Ocean", "#E9F4FF", "#D9ECFF", "#0086D1", "#0F3555"});
  themes_.emplace("Sunset", Theme{"Sunset", "#FFF1E8", "#FFE1D2", "#E7624E", "#4A2720"});
  themes_.emplace("Forest", Theme{"Forest", "#EEF7EE", "#DFF0DF", "#2E7B4A", "#1F3E2A"});
}

bool ThemeManager::setCurrent(const std::string& name) {
  if (!themes_.contains(name)) {
    return false;
  }
  current_ = name;
  return true;
}

const Theme& ThemeManager::current() const {
  return themes_.at(current_);
}

std::vector<std::string> ThemeManager::names() const {
  std::vector<std::string> out;
  out.reserve(themes_.size());
  for (const auto& [name, _] : themes_) {
    out.push_back(name);
  }
  return out;
}

}  // namespace waysheet
