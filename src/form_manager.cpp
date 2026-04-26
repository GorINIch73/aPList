#include "form_manager.hpp"

namespace waysheet {

std::int64_t FormManager::openForm(FormType type, const std::string& title) {
  const std::int64_t id = nextId_++;
  forms_.emplace(id, FormInstance{id, type, title, true});
  return id;
}

void FormManager::closeForm(std::int64_t formId) {
  auto it = forms_.find(formId);
  if (it != forms_.end()) {
    it->second.open = false;
  }
}

std::vector<FormInstance> FormManager::activeForms() const {
  std::vector<FormInstance> out;
  out.reserve(forms_.size());
  for (const auto& [_, form] : forms_) {
    if (form.open) {
      out.push_back(form);
    }
  }
  return out;
}

}  // namespace waysheet
