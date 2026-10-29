// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QImage>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;

namespace nutty {

class AppContext;

// Gets a picture of a device: from a file (a file dialog, or a path) or by
// downloading a URL. The image is scaled to at most 512 px and stored as
// <dataDir>/images/<deviceId>.png; picked(fileName) hands the name to the
// DeviceEditor.
class ImagePicker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY deviceIdChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr int kMaxSize = 512;
    static constexpr qint64 kMaxDownloadBytes = 16 * 1024 * 1024;

    explicit ImagePicker(QObject* parent = nullptr);
    ImagePicker(AppContext* context, QObject* parent);
    ~ImagePicker() override;

    QString deviceId() const { return m_deviceId; }
    void setDeviceId(const QString& id);
    bool busy() const { return m_reply != nullptr; }
    QString error() const { return m_error; }

    // Opens the system file dialog.
    Q_INVOKABLE void chooseFile();
    // Takes a local file (path or file: URL).
    Q_INVOKABLE bool setFromFile(const QUrl& file);
    // Downloads an http(s) image.
    Q_INVOKABLE void download(const QUrl& url);
    Q_INVOKABLE void cancel();

    // Scales and stores the image; returns the file name or empty on failure.
    QString store(const QImage& image);

signals:
    void deviceIdChanged();
    void busyChanged();
    void errorChanged();
    void picked(const QString& fileName);

private:
    void setError(const QString& error);

    QPointer<AppContext> m_context;
    QString m_deviceId;
    QString m_error;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
};

}  // namespace nutty
