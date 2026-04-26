#include "app.hpp"

#include <exception>
#include <fstream>

int main() {
  try {
    waysheet::App app;
    return app.run();
  } catch (const std::exception& ex) {
    std::ofstream out("crash.log", std::ios::app);
    out << "Fatal error: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::ofstream out("crash.log", std::ios::app);
    out << "Fatal error: unknown exception\n";
    return 1;
  }
}
