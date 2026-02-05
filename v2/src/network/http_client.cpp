/**
 * @file http_client.cpp
 */

#include "network/http_client.hpp"
#include "core/logger.hpp"

// cpp-httplib is header-only
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <QUrl>
#include <QFile>

namespace vt2 {

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() = default;

Result<QByteArray> HttpClient::get(const QString& url) {
    QUrl qurl(url);
    
    std::string host = qurl.host().toStdString();
    std::string path = qurl.path().toStdString();
    if (path.empty()) path = "/";
    
    httplib::Client cli(host);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);
    
    auto res = cli.Get(path);
    
    if (!res) {
        return std::unexpected("HTTP request failed");
    }
    
    if (res->status != 200) {
        return std::unexpected(QString("HTTP error: %1").arg(res->status).toStdString());
    }
    
    return QByteArray::fromStdString(res->body);
}

Result<QByteArray> HttpClient::post(const QString& url, const QByteArray& data, 
                                     const QString& contentType) {
    QUrl qurl(url);
    
    std::string host = qurl.host().toStdString();
    std::string path = qurl.path().toStdString();
    if (path.empty()) path = "/";
    
    httplib::Client cli(host);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);
    
    auto res = cli.Post(path, data.toStdString(), contentType.toStdString());
    
    if (!res) {
        return std::unexpected("HTTP request failed");
    }
    
    if (res->status != 200 && res->status != 201) {
        return std::unexpected(QString("HTTP error: %1").arg(res->status).toStdString());
    }
    
    return QByteArray::fromStdString(res->body);
}

Result<void> HttpClient::download(const QString& url, const QString& savePath) {
    auto result = get(url);
    if (!result) {
        return std::unexpected(result.error());
    }
    
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected("Failed to open file for writing");
    }
    
    file.write(*result);
    return {};
}

void HttpClient::setTimeout(int seconds) {
    timeout_ = seconds;
}

} // namespace vt2
