#pragma once
#include "Theme.h"
#include "../presets/Locks.h"
#include "../dsp/DspCommon.h"
#include <fjord_base/ui/LockMenu.h>

namespace fjord::ui
{
    // Fjord's menu behind the padlock in the preset bar: parts of the sound that stay when switching presets
    // (oscillators, filters, envelopes, modulation, effects, rhythm …). The menu itself, the tooltip, "Unlock all" and the
    // standard options are fjord-base (LockMenu::attach).
    inline fjord::base::ui::MenuLayout lockMenuLayout()
    {
        using L = fjord::base::ui::MenuLayout;
        std::vector<int> oscillators;
        for (int i = 0; i < dsp::kNumOsc; ++i) oscillators.push_back (locks::oscGroup (i));
        L layout;
        layout.header ("Keep when switching presets")
              .submenu ("Oscillators", oscillators, "All oscillators")
              .item (locks::kSubNoise)
              .item (locks::kFilters)
              .submenu ("Envelopes", { locks::kAmpEnv, locks::kFilterEnv, locks::kModEnv }, "All envelopes")
              .submenu ("Modulation", { locks::lfoGroup (0), locks::lfoGroup (1), locks::lfoGroup (2), locks::kMatrix })
              .item (locks::kVoice)
              .item (locks::kMacros)
              .separator()
              .effects (locks::set())
              .separator()
              .item (locks::kArp).item (locks::kGate).item (locks::kStutter).item (locks::kPump)
              .all ("All rhythm (arp, gate, stutter, pump)", { locks::kArp, locks::kGate, locks::kStutter, locks::kPump });
        return layout;
    }
}
