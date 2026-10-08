#pragma once

// This terminal's card reader. "stripe": the app runs on a Stripe smart
// reader (Apps on Devices); Stripe's own payment screen takes the card and
// hands back (android/src/.../StripeBridge.kt). "simulated": approves after
// a moment (amounts whose cents end in 05 are declined), for practice and
// tests. The store's server prepares the charge and records the result;
// card numbers never reach ViewTouch.

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class CardReader : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString amount READ amount NOTIFY changed)   // being charged: "$13.53"
    // A Stripe test-mode charge on a reader beside the screen: test cards can be "tapped".
    Q_PROPERTY(bool testCards READ testCards NOTIFY changed)

public:
    explicit CardReader(QObject *parent = nullptr);
    ~CardReader() override;

    QString kind() const { return kind_; }
    bool ready() const { return ready_; }
    bool busy() const { return !charging_.isEmpty(); }
    QString status() const { return status_; }
    QString amount() const { return charging_.value(QStringLiteral("amount")).toString(); }
    bool testCards() const { return kind_ == QStringLiteral("counter") && charging_.value(QStringLiteral("test")).toBool()
                                    && charging_.value(QStringLiteral("status")).toString() == QStringLiteral("waiting"); }

    // "", "stripe", "simulated" or "counter:tmr_..." (the terminal's setting;
    // kind() is then "counter").
    void setKind(const QString &kind);
    // A reader beside the screen is run by the store's computer: what it's doing.
    void setCounter(const QVariantMap &state);
    Q_INVOKABLE void presentTestCard(bool decline) { emit testCardRequested(decline); }
    // Take a card for a charge the server prepared (cardCharge: amountCents,
    // tipCents, currency, description, checkId, tenderId, amount).
    void charge(const QVariantMap &charge);
    Q_INVOKABLE void cancel();
    // The answer to needToken() (from the store's server).
    void provideToken(const QString &token, const QString &error);
    void setSimulatedDelay(int ms) { simulatedDelay_ = ms; }

    // From the Stripe bridge (any thread).
    void bridgeNeedsToken();
    void bridgeStatus(const QString &text, bool ready);
    void bridgeResult(const QString &json);

signals:
    void changed();
    void needToken();
    // {approved, declined, canceled, message, reference, brand, last4,
    //  amountCents, tipCents, checkId, tenderId, processor}
    void finished(const QVariantMap &result);
    // A reader beside the screen: the store's computer does these.
    void cancelRequested();
    void testCardRequested(bool decline);

private:
    void finish(QVariantMap result);
    void setStatus(const QString &text, bool ready);

    QString kind_;
    bool ready_ = false;
    QString status_;
    QVariantMap charging_;
    QTimer simulated_;
    int simulatedDelay_ = 1500;
};
