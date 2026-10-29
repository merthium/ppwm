#include "config.hpp"
#include <iostream>

int main() {
    using namespace ppwm::config;
    for (std::size_t i = 0; i < std::size(keys); ++i) {
        for (std::size_t j = i + 1; j < std::size(keys); ++j) {
            if (keys[i].key == keys[j].key && keys[i].modifiers == keys[j].modifiers) {
                std::cerr << "Duplicate key binding at " << i << " and " << j << '\n';
                return 1;
            }
        }
        const auto &binding = keys[i];
        if ((binding.action == Action::ViewTag || binding.action == Action::ToggleViewTag ||
             binding.action == Action::SendTag || binding.action == Action::ToggleSendTag) &&
            (binding.argument < 1 || binding.argument > static_cast<int>(tag_labels.size()))) {
            std::cerr << "Invalid tag binding at " << i << '\n';
            return 1;
        }
        if (binding.action == Action::Spawn && !binding.command) {
            std::cerr << "Missing spawn command at " << i << '\n';
            return 1;
        }
    }
    for (std::size_t i = 0; i < std::size(mouse_bindings); ++i)
        for (std::size_t j = i + 1; j < std::size(mouse_bindings); ++j)
            if (mouse_bindings[i].context == mouse_bindings[j].context &&
                mouse_bindings[i].button == mouse_bindings[j].button &&
                mouse_bindings[i].modifiers == mouse_bindings[j].modifiers) {
                std::cerr << "Duplicate mouse binding at " << i << " and " << j << '\n';
                return 1;
            }
    return 0;
}
