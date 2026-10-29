// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

namespace nutty {

// Registers the NUT, apcupsd, EcoFlow and HTTP/JSON source factories (and the
// NUT/EcoFlow discoveries) with SourceRegistry and Discovery. Safe to call
// more than once.
void registerCoreSources();

}  // namespace nutty
