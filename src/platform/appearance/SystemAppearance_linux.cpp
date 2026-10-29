// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/appearance/SystemAppearance.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

#include <functional>

// Reads the XDG settings portal (org.freedesktop.portal.Settings), which
// GNOME, KDE and others implement, and works inside Flatpak.

namespace nutty {

namespace {

const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kSettingsInterface = QStringLiteral("org.freedesktop.portal.Settings");

const QString kAppearance = QStringLiteral("org.freedesktop.appearance");
const QString kGnomeInterface = QStringLiteral("org.gnome.desktop.interface");
const QString kKdeGlobals = QStringLiteral("org.kde.kdeglobals.KDE");

struct Setting {
    QString ns;
    QString key;
};

const Setting kSettings[] = {
    { kAppearance, QStringLiteral("color-scheme") },
    { kAppearance, QStringLiteral("accent-color") },
    { kAppearance, QStringLiteral("reduced-motion") },
    { kGnomeInterface, QStringLiteral("enable-animations") },
    { kKdeGlobals, QStringLiteral("AnimationDurationFactor") },
};

// Read() wraps the value in an extra variant; unwrap any depth.
QVariant unwrap(QVariant value)
{
    while (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

// accent-color is (ddd) sRGB in [0, 1]; out-of-range means "not set".
QColor accentFrom(const QVariant& value)
{
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return {};
    const auto argument = qvariant_cast<QDBusArgument>(value);
    if (argument.currentType() != QDBusArgument::StructureType)
        return {};
    double red = -1;
    double green = -1;
    double blue = -1;
    argument.beginStructure();
    argument >> red >> green >> blue;
    argument.endStructure();
    const auto inRange = [](double c) { return c >= 0.0 && c <= 1.0; };
    if (!inRange(red) || !inRange(green) || !inRange(blue))
        return {};
    return QColor::fromRgbF(float(red), float(green), float(blue));
}

// Forwards SettingChanged; QtDBus needs a real slot for it.
class PortalListener : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void(const QString& ns, const QString& key, const QVariant& value)>;

    PortalListener(Handler handler, QObject* parent)
        : QObject(parent)
        , m_handler(std::move(handler))
    {
    }

public slots:
    void settingChanged(const QString& ns, const QString& key, const QDBusVariant& value)
    {
        m_handler(ns, key, unwrap(value.variant()));
    }

private:
    Handler m_handler;
};

}  // namespace

struct SystemAppearance::Private {
    PortalListener* listener = nullptr;
    // The portal's reduced-motion key wins over the desktop-specific ones.
    bool haveReducedMotionKey = false;

    void read(SystemAppearance* q, const Setting& setting, bool readOne = true);
    void apply(SystemAppearance* q, const QString& ns, const QString& key, const QVariant& value);
};

void SystemAppearance::Private::read(SystemAppearance* q, const Setting& setting, bool readOne)
{
    auto message = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kSettingsInterface,
        readOne ? QStringLiteral("ReadOne") : QStringLiteral("Read"));
    message.setArguments({ setting.ns, setting.key });
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), q);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q,
        [this, q, setting, readOne](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusVariant> reply = *call;
            if (reply.isError()) {
                // Portals before version 2 only have Read().
                if (readOne && reply.error().type() == QDBusError::UnknownMethod)
                    read(q, setting, false);
                return;  // otherwise the key doesn't exist on this desktop
            }
            apply(q, setting.ns, setting.key, unwrap(reply.argumentAt(0)));
        });
}

void SystemAppearance::Private::apply(
    SystemAppearance* q, const QString& ns, const QString& key, const QVariant& value)
{
    if (ns == kAppearance && key == u"color-scheme") {
        // 0 = no preference, 1 = prefer dark, 2 = prefer light
        const uint scheme = value.toUInt();
        q->updateDark(scheme == 1 ? std::optional<bool>(true)
                : scheme == 2     ? std::optional<bool>(false)
                                  : std::nullopt);
    } else if (ns == kAppearance && key == u"accent-color") {
        q->updateAccentColor(accentFrom(value));
    } else if (ns == kAppearance && key == u"reduced-motion") {
        // 0 = no preference, 1 = reduce
        haveReducedMotionKey = true;
        q->updateReduceMotion(value.toUInt() == 1);
    } else if (haveReducedMotionKey) {
        return;
    } else if (ns == kGnomeInterface && key == u"enable-animations") {
        q->updateReduceMotion(!value.toBool());
    } else if (ns == kKdeGlobals && key == u"AnimationDurationFactor") {
        q->updateReduceMotion(qFuzzyIsNull(value.toDouble()));
    }
}

SystemAppearance::SystemAppearance(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    watchQtColorScheme();

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    d->listener = new PortalListener([this](const QString& ns, const QString& key,
                                         const QVariant& value) { d->apply(this, ns, key, value); },
        this);
    bus.connect(kPortalService, kPortalPath, kSettingsInterface, QStringLiteral("SettingChanged"),
        d->listener, SLOT(settingChanged(QString, QString, QDBusVariant)));
    // Async, so a slow or missing portal never delays startup; Qt's own
    // scheme covers the first frames.
    for (const auto& setting : kSettings)
        d->read(this, setting);
}

SystemAppearance::~SystemAppearance() = default;

}  // namespace nutty

#include "SystemAppearance_linux.moc"
