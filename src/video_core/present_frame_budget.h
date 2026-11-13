// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace VideoCommon {

/// Caps generated presents to a FIFO display's available refreshes. It never raises the
/// backend's requested count or changes the user's target/multiplier. Zero means no known cap.
class PresentFrameBudget {
public:
    size_t Limit(size_t requested, double interval_seconds, double refresh_hz) {
        if (!std::isfinite(refresh_hz) || refresh_hz < 20.0 || refresh_hz > 1000.0) {
            Reset();
            return requested;
        }
        if (refresh_hz != refresh) {
            Reset();
            refresh = refresh_hz;
        }
        if (requested == 0 || !std::isfinite(interval_seconds) || interval_seconds <= 0.0 ||
            interval_seconds > 0.1) {
            credit = 0.0;
            return 0;
        }
        // One refresh belongs to the real frame. Fractional refreshes carry to later frames.
        const double extra = std::max(0.0, interval_seconds * refresh_hz - 1.0);
        if (extra == 0.0) {
            credit = 0.0;
            return 0;
        }
        credit += extra;
        const size_t allowed = std::min(requested, static_cast<size_t>(std::floor(credit + 1e-6)));
        credit = std::max(0.0, credit - static_cast<double>(allowed));
        if (credit >= 1.0) {
            credit = std::fmod(credit, 1.0);
        }
        return allowed;
    }

    void Reset() noexcept {
        credit = 0.0;
        refresh = 0.0;
    }

private:
    double credit{};
    double refresh{};
};

} // namespace VideoCommon
