#include "cardreader.hh"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

using namespace Qt::StringLiterals;

namespace {

CardReader *g_reader = nullptr;   // the one the Stripe bridge reports to

#ifdef Q_OS_ANDROID
constexpr const char *kBridge = "org/viewtouch/pos/StripeBridge";

QString fromJava(JNIEnv *env, jstring s)
{
    if (!s)
        return {};
    const char *chars = env->GetStringUTFChars(s, nullptr);
    QString out = QString::fromUtf8(chars);
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

void JNICALL onNeedToken(JNIEnv *, jclass)
{
    if (g_reader)
        QMetaObject::invokeMethod(g_reader, [] { if (g_reader) g_reader->bridgeNeedsToken(); }, Qt::QueuedConnection);
}

void JNICALL onStatus(JNIEnv *env, jclass, jstring text, jboolean ready)
{
    const QString t = fromJava(env, text);
    const bool r = ready;
    if (g_reader)
        QMetaObject::invokeMethod(g_reader, [t, r] { if (g_reader) g_reader->bridgeStatus(t, r); }, Qt::QueuedConnection);
}

void JNICALL onResult(JNIEnv *env, jclass, jstring json)
{
    const QString j = fromJava(env, json);
    if (g_reader)
        QMetaObject::invokeMethod(g_reader, [j] { if (g_reader) g_reader->bridgeResult(j); }, Qt::QueuedConnection);
}

bool registerBridge()
{
    static bool done = false;
    if (done)
        return true;
    QJniEnvironment env;
    const JNINativeMethod methods[] = {
        {"nativeNeedToken", "()V", reinterpret_cast<void *>(onNeedToken)},
        {"nativeStatus", "(Ljava/lang/String;Z)V", reinterpret_cast<void *>(onStatus)},
        {"nativeResult", "(Ljava/lang/String;)V", reinterpret_cast<void *>(onResult)},
    };
    done = env.registerNativeMethods(kBridge, methods, 3);
    return done;
}
#endif

} // namespace

CardReader::CardReader(QObject *parent) : QObject(parent)
{
    g_reader = this;
    simulated_.setSingleShot(true);
    connect(&simulated_, &QTimer::timeout, this, [this] {
        const qint64 total = charging_.value(u"amountCents"_s).toLongLong() + charging_.value(u"tipCents"_s).toLongLong();
        // Like Stripe's test cards: cents ending in 05 are declined.
        if (total % 100 == 5) {
            finish({{u"declined"_s, true}, {u"message"_s, tr("Card declined (simulated).")}});
            return;
        }
        const QString ref = u"sim_"_s + QString::number(QRandomGenerator::global()->generate64(), 16);
        finish({{u"approved"_s, true}, {u"reference"_s, ref}, {u"brand"_s, u"visa"_s}, {u"last4"_s, u"4242"_s},
                {u"amountCents"_s, total}});
    });
}

CardReader::~CardReader()
{
    if (g_reader == this)
        g_reader = nullptr;
}

void CardReader::setStatus(const QString &text, bool ready)
{
    if (text == status_ && ready == ready_)
        return;
    status_ = text;
    ready_ = ready;
    emit changed();
}

void CardReader::setKind(const QString &kind)
{
    if (kind == kind_)
        return;
    if (busy())
        cancel();
    kind_ = kind;
    if (kind_.isEmpty()) {
        setStatus({}, false);
    } else if (kind_ == u"simulated") {
        setStatus(tr("Simulated card reader (practice)"), true);
    } else if (kind_ == u"stripe") {
#ifdef Q_OS_ANDROID
        setStatus(tr("Connecting to the card reader…"), false);
        if (registerBridge())
            QJniObject::callStaticMethod<void>(kBridge, "start", "(Landroid/content/Context;)V",
                                               QNativeInterface::QAndroidApplication::context());
        else
            setStatus(tr("This app was built without the Stripe card reader."), false);
#else
        setStatus(tr("Stripe card readers: run ViewTouch on the Stripe Reader S700 itself."), false);
#endif
    }
    emit changed();
}

void CardReader::charge(const QVariantMap &charge)
{
    if (busy()) {
        emit finished({{u"message"_s, tr("The card reader is busy.")}});
        return;
    }
    charging_ = charge;
    emit changed();
    if (kind_ == u"simulated") {
        status_ = tr("Tap, insert or swipe (simulated)…");
        simulated_.start(simulatedDelay_);
        emit changed();
        return;
    }
#ifdef Q_OS_ANDROID
    if (kind_ == u"stripe" && ready_) {
        const qint64 total = charge.value(u"amountCents"_s).toLongLong() + charge.value(u"tipCents"_s).toLongLong();
        QJniObject::callStaticMethod<void>(
            kBridge, "charge", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", jlong(total),
            QJniObject::fromString(charge.value(u"currency"_s).toString()).object<jstring>(),
            QJniObject::fromString(charge.value(u"description"_s).toString()).object<jstring>(),
            QJniObject::fromString(QString::number(charge.value(u"checkId"_s).toLongLong())).object<jstring>());
        return;
    }
#endif
    finish({{u"message"_s, kind_ == u"stripe" ? tr("The card reader isn't connected yet: %1").arg(status_)
                                              : tr("This screen has no card reader.")}});
}

void CardReader::cancel()
{
    if (!busy())
        return;
    if (kind_ == u"simulated") {
        simulated_.stop();
        finish({{u"canceled"_s, true}, {u"message"_s, tr("Card payment canceled.")}});
        return;
    }
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kBridge, "cancel", "()V");   // answers through nativeResult
#endif
}

void CardReader::provideToken(const QString &token, const QString &error)
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kBridge, "provideToken", "(Ljava/lang/String;Ljava/lang/String;)V",
                                       QJniObject::fromString(token).object<jstring>(),
                                       QJniObject::fromString(error).object<jstring>());
#else
    Q_UNUSED(token)
    Q_UNUSED(error)
#endif
}

void CardReader::bridgeNeedsToken()
{
    emit needToken();
}

void CardReader::bridgeStatus(const QString &text, bool ready)
{
    setStatus(text, ready);
}

void CardReader::bridgeResult(const QString &json)
{
    const QJsonObject r = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString status = r.value(u"status").toString();
    QVariantMap out{{u"message"_s, r.value(u"message").toString()}};
    if (status == u"approved") {
        out.insert(u"approved"_s, true);
        out.insert(u"reference"_s, r.value(u"id").toString());
        out.insert(u"brand"_s, r.value(u"brand").toString());
        out.insert(u"last4"_s, r.value(u"last4").toString());
        out.insert(u"amountCents"_s, r.value(u"amount").toInteger());
        // A tip chosen on the reader's own screen comes on top.
        if (r.contains(u"tip"))
            out.insert(u"readerTipCents"_s, r.value(u"tip").toInteger());
    } else if (status == u"declined") {
        out.insert(u"declined"_s, true);
    } else if (status == u"canceled") {
        out.insert(u"canceled"_s, true);
    }
    finish(out);
}

void CardReader::finish(QVariantMap result)
{
    const QVariantMap was = charging_;
    charging_.clear();
    // What it was for, and how it was taken.
    result.insert(u"checkId"_s, was.value(u"checkId"_s));
    result.insert(u"tenderId"_s, was.value(u"tenderId"_s));
    result.insert(u"processor"_s, kind_);
    if (!result.contains(u"amountCents"_s))
        result.insert(u"amountCents"_s, was.value(u"amountCents"_s).toLongLong() + was.value(u"tipCents"_s).toLongLong());
    result.insert(u"tipCents"_s, was.value(u"tipCents"_s).toLongLong() + result.value(u"readerTipCents"_s).toLongLong());
    if (kind_ == u"simulated")
        status_ = tr("Simulated card reader (practice)");
    emit changed();
    emit finished(result);
}
