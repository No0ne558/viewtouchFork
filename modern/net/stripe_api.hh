#pragma once

// The store's computer talking to Stripe (api.stripe.com) with the store's
// secret key: connection tokens for the card readers, and refunds. Never
// blocks: each call answers once, later, with the result or an error text.

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

namespace vt::net {

class StripeApi : public QObject {
    Q_OBJECT

public:
    using KeyFn = std::function<QString()>;
    explicit StripeApi(KeyFn secretKey, QObject *parent = nullptr);

    // A connection token for a card reader (its secret).
    void connectionToken(std::function<void(const QString &secret, const QString &error)> done);
    // Money back on a payment: `cents` of PaymentIntent `paymentId` ("pi_...").
    void refund(const QString &paymentId, qint64 cents,
                std::function<void(const QString &refundId, const QString &error)> done);

    // Where Stripe is (a test server in the tests).
    void setBaseUrl(const QUrl &url) { base_ = url; }

private:
    void post(const QString &path, const QByteArray &form,
              std::function<void(const QJsonObject &reply, const QString &error)> done);

    KeyFn key_;
    QUrl base_{QStringLiteral("https://api.stripe.com")};
    QNetworkAccessManager net_;
};

} // namespace vt::net
