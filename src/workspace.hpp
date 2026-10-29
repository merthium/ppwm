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

struct WorkspaceSnapshot {
    TagMask active{1}, previous{1};
    unsigned primary{}, previous_primary{};
    std::array<WorkspaceSettings, 10> settings{};
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

    WorkspaceSnapshot snapshot() const {
        return {active_, previous_, primary_, previous_primary_, settings_};
    }
    bool restore(const WorkspaceSnapshot &saved, std::size_t layout_count) {
        auto valid_view = [](TagMask mask, unsigned primary) {
            return mask && !(mask & ~all_tags) && primary < 10 &&
                   (primary == 9 ? mask == all_tags : (mask & (1u << primary)) != 0);
        };
        if (!valid_view(saved.active, saved.primary) ||
            !valid_view(saved.previous, saved.previous_primary))
            return false;
        for (const auto &settings : saved.settings)
            if (!(settings.master_ratio >= 0.1 && settings.master_ratio <= 0.9) ||
                settings.master_count < 0 || settings.layout_index >= layout_count ||
                settings.previous_layout >= layout_count)
                return false;
        active_ = saved.active;
        previous_ = saved.previous;
        primary_ = saved.primary;
        previous_primary_ = saved.previous_primary;
        settings_ = saved.settings;
        return true;
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
