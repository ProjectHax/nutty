// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

import QtQuick
import Nutty

// One setting: a title and description on the left, its control on the
// right (or below, in a narrow window). Rows in a Card get a divider
// between them via `first`.
ListRow {
    property bool first: false

    divider: !first
    stacked: width < 520
}
