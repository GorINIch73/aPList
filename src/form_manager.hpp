#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace waysheet {

enum class FormType {
  Cars,
  Drivers,
  Routes,
  Waybill
};

struct FormInstance {
  std::int64_t id{};
  FormType type{FormType::Waybill};
  std::string title;
  bool open{true};
};

class FormManager {
 public:
  std::int64_t openForm(FormType type, const std::string& title);
  void closeForm(std::int64_t formId);
  std::vector<FormInstance> activeForms() const;

 private:
  std::int64_t nextId_{1};
  std::unordered_map<std::int64_t, FormInstance> forms_;
};

}  // namespace waysheet

