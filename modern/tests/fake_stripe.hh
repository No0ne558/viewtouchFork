#pragma once

// A fake Stripe for the countertop-reader tests (PosShared::stripeCall).

#include "app/pos_service.hh"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

#include <functional>

namespace vt::test {

using namespace Qt::StringLiterals;
using vt::app::PosService;

// Stripe, as far as a countertop reader goes: one location, readers, payment
// intents, and the reader's action (done by the test-card helper).
struct FakeStripe {
    QStringList calls;
    QString location;
    QString action = u"none"_s;   // none | in_progress | succeeded | failed
    QString failureCode, failureMessage;
    qint64 amount = 0, readerTip = 0;
    bool canceled = false, busy = false;

    void install(vt::app::PosService &pos)
    {
        pos.shared()->stripeCall = [this](const QString &method, const QString &path, const QString &form,
                                          std::function<void(const QJsonObject &, const QString &)> done) {
            calls << method + u' ' + path + (form.isEmpty() ? QString() : u" ?"_s + form);
            const QUrlQuery q(form);
            if (path == u"/v1/terminal/locations" && method == u"GET")
                return done({{u"data"_s, location.isEmpty() ? QJsonArray() : QJsonArray{QJsonObject{{u"id"_s, location}}}}}, {});
            if (path == u"/v1/terminal/locations") {
                location = u"tml_1"_s;
                return done({{u"id"_s, location}}, {});
            }
            if (path == u"/v1/terminal/readers")
                return q.queryItemValue(u"registration_code"_s) == u"simulated-wpe"
                           ? done({{u"id"_s, u"tmr_1"_s}, {u"device_type"_s, u"simulated_wisepos_e"_s},
                                   {u"label"_s, q.queryItemValue(u"label"_s)}}, {})
                           : done({}, u"Invalid registration code"_s);
            if (path == u"/v1/payment_intents" && method == u"POST") {
                amount = q.queryItemValue(u"amount"_s).toLongLong();
                canceled = false;
                return done({{u"id"_s, u"pi_1"_s}}, {});
            }
            if (path == u"/v1/terminal/readers/tmr_1/process_payment_intent") {
                action = u"in_progress"_s;
                return done({}, {});
            }
            const QJsonObject act{{u"status"_s, action}, {u"failure_code"_s, failureCode},
                                  {u"failure_message"_s, failureMessage},
                                  {u"process_payment_intent"_s, QJsonObject{{u"payment_intent"_s, u"pi_1"_s}}}};
            if (path == u"/v1/terminal/readers/tmr_1")
                return done({{u"action"_s, act}}, {});
            if (path == u"/v1/test_helpers/terminal/readers/tmr_1/present_payment_method") {
                const bool decline = q.queryItemValue(u"card_present[number]"_s) == u"4000000000000002";
                action = decline ? u"failed"_s : u"succeeded"_s;
                failureCode = decline ? u"card_declined"_s : QString();
                failureMessage = decline ? u"Your card was declined."_s : QString();
                return done({}, {});
            }
            if (path == u"/v1/terminal/readers/tmr_1/cancel_action") {
                if (busy)
                    return done({}, u"Reader is currently busy processing another request."_s);
                action = u"none"_s;
                return done({}, {});
            }
            if (path == u"/v1/payment_intents/pi_1/cancel") {
                canceled = true;
                return done({}, {});
            }
            if (path == u"/v1/payment_intents/pi_1")
                return done({{u"status"_s, u"succeeded"_s}, {u"amount"_s, amount + readerTip},
                             {u"amount_details"_s, QJsonObject{{u"tip"_s, QJsonObject{{u"amount"_s, readerTip}}}}},
                             {u"latest_charge"_s, QJsonObject{{u"payment_method_details"_s, QJsonObject{
                                 {u"card_present"_s, QJsonObject{{u"brand"_s, u"mastercard"_s}, {u"last4"_s, u"4444"_s}}}}}}}},
                            {});
            done({}, u"unexpected call"_s);
        };
    }
};

inline void waitFor(const std::function<bool()> &ready, int ms = 6000)
{
    for (int i = 0; i < ms / 20 && !ready(); ++i)
        QTest::qWait(20);
}

} // namespace vt::test