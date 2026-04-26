#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace waysheet {

struct Theme {
  std::string name;
  std::string background;
  std::string panel;
  std::string accent;
  std::string text;
};

class ThemeManager {
 public:
  ThemeManager();

  bool setCurrent(const std::string& name);
  const Theme& current() const;
  std::vector<std::string> names() const;

 private:
  std::unordered_map<std::string, Theme> themes_;
  std::string current_{"Light"};
};

}  // namespace waysheet

