/**
 * @file http_client.hpp
 * @brief HTTP client for network operations
 */

#pragma once

#include "core/types.hpp"
#include <QString>
#include <QByteArray>
#include <functional>

namespace vt2 {

/**
 * @brief Modern HTTP client using cpp-httplib
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    /**
     * @brief Perform GET request
     */
    Result<QByteArray> get(const QString& url);
    
    /**
     * @brief Perform POST request
     */
    Result<QByteArray> post(const QString& url, const QByteArray& data, 
                            const QString& contentType = "application/json");
    
    /**
     * @brief Download file
     */
    Result<void> download(const QString& url, const QString& savePath);
    
    /**
     * @brief Set timeout in seconds
     */
    void setTimeout(int seconds);
    
private:
    int timeout_ = 30;
};

} // namespace vt2
