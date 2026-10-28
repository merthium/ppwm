#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <utility>

namespace ppwm {
using TagMask = unsigned;
inline constexpr TagMask all_tags = (1u << 9) - 1;
constexpr unsigned first_tag(TagMask tags) {
    return tags ? std::countr_zero(tags) : 0;
}

struct WorkspaceSettings {
    double master_ratio{0.55};
    int master_count{1};
    std::size_t layout_index{}, previous_layout{1};
    bool bar_visible{true};
};

// The tenth slot stores the all-tags view; normal tags retain their own settings.
class Workspace {
  public:
    explicit Workspace(WorkspaceSettings defaults = {}) {
        settings_.fill(defaults);
    }
    [[nodiscard]] TagMask active_tags() const noexcept {
        return active_;
    }
    [[nodiscard]] unsigned primary_tag() const noexcept {
        return primary_ == 9 ? 0 : primary_;
    }
    WorkspaceSettings &settings() noexcept {
        return settings_[primary_];
    }
    const WorkspaceSettings &settings() const noexcept {
        return settings_[primary_];
    }

    bool view(TagMask tags) {
        tags &= all_tags;
        if (tags == active_)
            return false;
        if (tags == 0) {
            std::swap(active_, previous_);
            std::swap(primary_, previous_primary_);
        } else {
            previous_ = active_;
            previous_primary_ = primary_;
            active_ = tags;
            primary_ = tags == all_tags ? 9 : first_tag(tags);
        }
        return true;
    }
    bool toggle(TagMask tags) {
        const TagMask next = active_ ^ (tags & all_tags);
        if (next == 0)
            return false;
        active_ = next;
        if (next == all_tags)
            primary_ = 9;
        else if (primary_ == 9 || !(next & (1u << primary_)))
            primary_ = first_tag(next);
        return true;
    }
    void select_layout(std::size_t index) {
        auto &current = settings();
        if (index != current.layout_index) {
            current.previous_layout = current.layout_index;
            current.layout_index = index;
        }
    }
    void toggle_layout() {
        auto &current = settings();
        std::swap(current.layout_index, current.previous_layout);
    }

  private:
    TagMask active_{1}, previous_{1};
    unsigned primary_{}, previous_primary_{};
    std::array<WorkspaceSettings, 10> settings_{};
};
} // namespace ppwm
