// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/shell/TrayIconEngine.h"

#include <QIcon>
#include <QImage>
#include <QTest>

#include <cstdlib>

using namespace nutty;

namespace {

TrayIconState state(TrayIconStyle style, int charge, const QString& health = QStringLiteral("ok"))
{
    TrayIconState s;
    s.style = style;
    s.charge = charge;
    s.health = health;
    return s;
}

// The box around every pixel that isn't transparent.
QRect inkBounds(const QImage& image)
{
    QRect box;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) > 40)
                box |= QRect(x, y, 1, 1);
    return box;
}

bool isLevel(QRgb pixel, const QColor& level)
{
    const QColor c = QColor::fromRgba(pixel);
    return c.alpha() > 200 && std::abs(c.red() - level.red()) < 24 && std::abs(c.green() - level.green()) < 24
        && std::abs(c.blue() - level.blue()) < 24;
}

// The box around the pixels in the charge colour.
QRect levelBounds(const TrayIconState& s, int size)
{
    const QImage image = TrayIconEngine::render(s, size);
    const QColor level = TrayIconEngine::levelColor(s);
    QRect box;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (isLevel(image.pixel(x, y), level))
                box |= QRect(x, y, 1, 1);
    return box;
}

}  // namespace

class TestTrayIcon : public QObject
{
    Q_OBJECT

private slots:
    void fillsTheSquare_data()
    {
        QTest::addColumn<TrayIconStyle>("style");
        QTest::addColumn<int>("size");
        QTest::addColumn<double>("minHeight");
        for (int size : { 16, 22, 32 }) {
            QTest::addRow("acorn %d", size) << TrayIconStyle::Acorn << size << 0.95;
            QTest::addRow("battery %d", size) << TrayIconStyle::Battery << size << 0.6;
        }
    }

    // Panels draw tray icons at a fixed size, so the glyph uses all of it.
    void fillsTheSquare()
    {
        QFETCH(TrayIconStyle, style);
        QFETCH(int, size);
        QFETCH(double, minHeight);
        const QRect box = inkBounds(TrayIconEngine::render(state(style, 50), size));
        QVERIFY2(box.width() >= size - 1, qPrintable(QStringLiteral("%1 px wide").arg(box.width())));
        QVERIFY2(
            box.height() >= size * minHeight, qPrintable(QStringLiteral("%1 px tall").arg(box.height())));
    }

    // The acorn fills from the bottom up, the battery from the left.
    void chargeFillsUp_data()
    {
        QTest::addColumn<TrayIconStyle>("style");
        QTest::addRow("acorn") << TrayIconStyle::Acorn;
        QTest::addRow("battery") << TrayIconStyle::Battery;
    }

    void chargeFillsUp()
    {
        QFETCH(TrayIconStyle, style);
        const int size = 32;
        int previous = -1;
        int floor = -1;
        for (int charge : { 5, 30, 60, 100 }) {
            const QRect level = levelBounds(state(style, charge), size);
            QVERIFY2(!level.isEmpty(), qPrintable(QStringLiteral("no charge drawn at %1 %").arg(charge)));
            const int extent = style == TrayIconStyle::Acorn ? size - level.top() : level.right();
            QVERIFY2(extent > previous,
                qPrintable(QStringLiteral("%1 % doesn't reach past the charge below it").arg(charge)));
            previous = extent;
            // ...from the same place every time.
            const int start = style == TrayIconStyle::Acorn ? level.bottom() : level.left();
            if (floor < 0)
                floor = start;
            QVERIFY(std::abs(start - floor) <= 1);
        }
    }

    void unknownChargeDrawsNoLevel()
    {
        for (auto style : { TrayIconStyle::Acorn, TrayIconStyle::Battery }) {
            TrayIconState s = state(style, -1, QStringLiteral("ok"));
            QVERIFY(levelBounds(s, 32).isEmpty());
        }
    }

    // The level colour follows the state.
    void levelColourFollowsHealth()
    {
        const QColor ok = TrayIconEngine::levelColor(state(TrayIconStyle::Acorn, 80, QStringLiteral("ok")));
        const QColor onBattery
            = TrayIconEngine::levelColor(state(TrayIconStyle::Acorn, 80, QStringLiteral("onBattery")));
        const QColor low = TrayIconEngine::levelColor(state(TrayIconStyle::Acorn, 10, QStringLiteral("low")));
        QVERIFY(ok != onBattery && onBattery != low && ok != low);
        QVERIFY(!levelBounds(state(TrayIconStyle::Acorn, 50, QStringLiteral("onBattery")), 32).isEmpty());
    }

    // A macOS template image: nothing but black and transparency.
    void maskIsBlackOnly()
    {
        for (auto style : { TrayIconStyle::Acorn, TrayIconStyle::Battery }) {
            TrayIconState s = state(style, 60, QStringLiteral("onBattery"));
            s.mask = true;
            s.charging = true;
            s.pending = true;
            const QImage image = TrayIconEngine::render(s, 22);
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QRgb p = image.pixel(x, y);
                    QVERIFY(qRed(p) == 0 && qGreen(p) == 0 && qBlue(p) == 0);
                }
            QVERIFY(!inkBounds(image).isEmpty());
        }
    }

    void monochromeHasNoLevelColour()
    {
        for (auto style : { TrayIconStyle::Acorn, TrayIconStyle::Battery }) {
            TrayIconState s = state(style, 60, QStringLiteral("onBattery"));
            s.monochrome = true;
            const QColor level = TrayIconEngine::levelColor(s);
            const QImage image = TrayIconEngine::render(s, 32);
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    QVERIFY(!isLevel(image.pixel(x, y), level));
        }
    }

    void stylesLookDifferent()
    {
        QVERIFY(TrayIconEngine::render(state(TrayIconStyle::Acorn, 50), 22)
            != TrayIconEngine::render(state(TrayIconStyle::Battery, 50), 22));
    }

    // Panels sent pixmaps pick the nearest size at or above their own, so
    // the common panel sizes are there at 1x and 2x, and render at that size.
    void offersPanelSizes()
    {
        QIcon icon(new TrayIconEngine(state(TrayIconStyle::Acorn, 50)));
        const QList<QSize> sizes = icon.availableSizes();
        for (int s : { 16, 20, 22, 24, 32, 40, 44, 48 }) {
            QVERIFY2(sizes.contains(QSize(s, s)), qPrintable(QStringLiteral("no %1 px").arg(s)));
            QCOMPARE(icon.pixmap(QSize(s, s), 1.0).size(), QSize(s, s));
        }
    }
};

QTEST_MAIN(TestTrayIcon)
#include "tst_trayicon.moc"
