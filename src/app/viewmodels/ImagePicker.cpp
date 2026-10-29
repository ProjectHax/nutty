// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "app/viewmodels/ImagePicker.h"

#include "app/viewmodels/AppContext.h"

#include <QDir>
#include <QFileDialog>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>

namespace nutty {

ImagePicker::ImagePicker(QObject* parent)
    : ImagePicker(AppContext::instance(), parent)
{
}

ImagePicker::ImagePicker(AppContext* context, QObject* parent)
    : QObject(parent)
    , m_context(context)
{
}

ImagePicker::~ImagePicker()
{
    cancel();
}

void ImagePicker::setDeviceId(const QString& id)
{
    if (id == m_deviceId)
        return;
    m_deviceId = id;
    emit deviceIdChanged();
}

void ImagePicker::setError(const QString& error)
{
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void ImagePicker::chooseFile()
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    const QString file = QFileDialog::getOpenFileName(nullptr, tr("Choose a picture"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        tr("Images (%1)").arg(patterns.join(u' ')));
    if (!file.isEmpty())
        setFromFile(QUrl::fromLocalFile(file));
}

bool ImagePicker::setFromFile(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        setError(tr("That file isn't a picture nutty can read."));
        return false;
    }
    return !store(image).isEmpty();
}

void ImagePicker::download(const QUrl& url)
{
    cancel();
    if (!url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http")) {
        setError(tr("Enter an http:// or https:// address of a picture."));
        return;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("nutty/" NUTTY_VERSION));
    m_reply = m_network->get(request);
    setError({});
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        if (received > kMaxDownloadBytes && m_reply) {
            setError(tr("That picture is too large."));
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (reply != m_reply)
            return;
        m_reply = nullptr;
        emit busyChanged();
        if (reply->error() != QNetworkReply::NoError) {
            if (m_error.isEmpty())
                setError(tr("Download failed: %1").arg(reply->errorString()));
            return;
        }
        QImage image;
        if (!image.loadFromData(reply->readAll())) {
            setError(tr("That address didn't return a picture."));
            return;
        }
        store(image);
    });
    emit busyChanged();
}

void ImagePicker::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    emit busyChanged();
}

QString ImagePicker::store(const QImage& source)
{
    if (!m_context || m_context->imagesDir().isEmpty() || m_deviceId.isEmpty()) {
        setError(tr("There is nowhere to keep the picture."));
        return {};
    }
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    if (image.width() > kMaxSize || image.height() > kMaxSize)
        image = image.scaled(kMaxSize, kMaxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QString dir = m_context->imagesDir();
    QDir().mkpath(dir);
    const QString fileName = m_deviceId + QStringLiteral(".png");
    QSaveFile file(dir + u'/' + fileName);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        setError(tr("Could not save the picture."));
        return {};
    }
    setError({});
    emit picked(fileName);
    return fileName;
}

}  // namespace nutty
