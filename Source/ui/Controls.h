#pragma once
#include "Theme.h"
#include "../presets/Locks.h"

namespace fjord::ui
{
    // Lock switch for one part of Fjord's sound (see presets/Locks.h): the group is Fjord's lock index.
    class LockButton : public fjord::base::ui::LockButton
    {
    public:
        LockButton (APVTS& state, int lockGroup, bool showLabel = true)
            : fjord::base::ui::LockButton (state, locks::set(), lockGroup, showLabel) {}
    };
}
