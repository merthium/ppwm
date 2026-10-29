#include "workspace.hpp"
#include <cstdlib>
#include <iostream>
void require(bool condition) {
    if (!condition) {
        std::cerr << "workspace invariant failed\n";
        std::exit(1);
    }
}
int main() {
    ppwm::Workspace configured({.master_ratio = 0.7});
    configured.view(4);
    require(configured.settings().master_ratio == 0.7);
    configured.view(ppwm::all_tags);
    require(configured.settings().master_ratio == 0.7);
    ppwm::Workspace workspace;
    require(workspace.active_tags() == 1 && workspace.primary_tag() == 0);
    workspace.settings().master_count = 2;
    workspace.settings().bar_visible = false;
    workspace.select_layout(2);
    require(workspace.view(2));
    require(workspace.settings().master_count == 1 && workspace.settings().bar_visible);
    workspace.select_layout(1);
    require(workspace.view(0));
    require(workspace.active_tags() == 1 && workspace.settings().master_count == 2 &&
            !workspace.settings().bar_visible);
    require(workspace.settings().layout_index == 2);
    workspace.toggle_layout();
    require(workspace.settings().layout_index == 0);
    workspace.toggle_layout();
    require(workspace.settings().layout_index == 2);
    require(workspace.toggle(2) && workspace.active_tags() == 3 && workspace.primary_tag() == 0);
    require(workspace.toggle(1) && workspace.active_tags() == 2 && workspace.primary_tag() == 1);
    require(!workspace.toggle(2) && workspace.active_tags() == 2);
    workspace.view(ppwm::all_tags);
    require(workspace.active_tags() == 511);
    workspace.settings().master_count = 4;
    workspace.view(1);
    require(workspace.settings().master_count == 2);
    workspace.view(ppwm::all_tags);
    require(workspace.settings().master_count == 4);
    auto saved = workspace.snapshot();
    ppwm::Workspace restored;
    require(restored.restore(saved, 3));
    require(restored.active_tags() == ppwm::all_tags && restored.settings().master_count == 4);
    restored.view(0);
    require(restored.active_tags() == 1 && restored.settings().master_count == 2);
    saved.active = 0;
    require(!restored.restore(saved, 3));
    saved = workspace.snapshot();
    saved.settings[0].layout_index = 99;
    require(!restored.restore(saved, 3));
    std::cout << "Workspace tests passed\n";
}
