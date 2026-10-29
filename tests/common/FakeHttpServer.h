// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>
#include <memory>

// A minimal HTTP/1.1 server on 127.0.0.1 for tests: one request per
// connection, answered by a handler, then the connection is closed.
class FakeHttpServer : public QObject
{
public:
    struct Request {
        QByteArray method;
        QByteArray target;  // path + query, e.g. "/api/states/sensor.x"
        QHash<QByteArray, QByteArray> headers;  // lower-case names
        QByteArray body;
        QByteArray path() const
        {
            return target.left(target.indexOf('?') < 0 ? target.size() : target.indexOf('?'));
        }
    };
    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray contentType = "application/json";
    };
    using Handler = std::function<Response(const Request&)>;

    explicit FakeHttpServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection())
                serve(socket);
        });
    }

    quint16 port() const { return m_server.serverPort(); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(port()); }
    void setHandler(Handler handler) { m_handler = std::move(handler); }
    const QList<Request>& requests() const { return m_requests; }
    void clearRequests() { m_requests.clear(); }

private:
    void serve(QTcpSocket* socket)
    {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            Request request;
            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            request.method = first.value(0);
            request.target = first.value(1);
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const qsizetype colon = lines[i].indexOf(':');
                if (colon > 0)
                    request.headers.insert(
                        lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
            }
            const qsizetype length = request.headers.value("content-length", "0").toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            request.body = buffer->mid(headerEnd + 4, length);
            buffer->clear();
            m_requests << request;

            const Response response
                = m_handler ? m_handler(request) : Response { 404, "{}", "application/json" };
            QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + " Test\r\n";
            out += "Content-Type: " + response.contentType + "\r\n";
            out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
            out += "Connection: close\r\n\r\n";
            out += response.body;
            socket->write(out);
            socket->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    Handler m_handler;
    QList<Request> m_requests;
};
