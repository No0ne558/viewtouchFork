package org.viewtouch.pos

import android.content.Context
import android.os.Handler
import android.os.Looper
import com.stripe.stripeterminal.Terminal
import com.stripe.stripeterminal.external.callable.AppsOnDevicesListener
import com.stripe.stripeterminal.external.callable.Callback
import com.stripe.stripeterminal.external.callable.Cancelable
import com.stripe.stripeterminal.external.callable.ConnectionTokenCallback
import com.stripe.stripeterminal.external.callable.ConnectionTokenProvider
import com.stripe.stripeterminal.external.callable.DiscoveryListener
import com.stripe.stripeterminal.external.callable.PaymentIntentCallback
import com.stripe.stripeterminal.external.callable.ReaderCallback
import com.stripe.stripeterminal.external.callable.TerminalListener
import com.stripe.stripeterminal.external.models.CaptureMethod
import com.stripe.stripeterminal.external.models.CollectPaymentIntentConfiguration
import com.stripe.stripeterminal.external.models.ConnectionConfiguration
import com.stripe.stripeterminal.external.models.ConnectionStatus
import com.stripe.stripeterminal.external.models.ConnectionTokenException
import com.stripe.stripeterminal.external.models.DisconnectReason
import com.stripe.stripeterminal.external.models.DiscoveryConfiguration
import com.stripe.stripeterminal.external.models.LocaleConfig
import com.stripe.stripeterminal.external.models.PaymentIntent
import com.stripe.stripeterminal.external.models.PaymentIntentParameters
import com.stripe.stripeterminal.external.models.Reader
import com.stripe.stripeterminal.external.models.ReaderEvent
import com.stripe.stripeterminal.external.models.TerminalException
import com.stripe.stripeterminal.log.LogLevel
import org.json.JSONObject

// ViewTouch running on a Stripe smart reader (Apps on Devices): connects to
// the reader it runs on, and takes a card for an amount. Stripe's own app
// shows the payment screens; card data never reaches ViewTouch. The C++
// side (ui/cpp/cardreader.cpp) calls the @JvmStatic functions and hears back
// through the native ones. Connection tokens come from the store's computer,
// which holds the Stripe secret key.
object StripeBridge {
    @JvmStatic external fun nativeNeedToken()
    @JvmStatic external fun nativeStatus(text: String, ready: Boolean)
    @JvmStatic external fun nativeResult(json: String)

    private val main = Handler(Looper.getMainLooper())
    private var pendingToken: ConnectionTokenCallback? = null
    private var collecting: Cancelable? = null
    private var current: PaymentIntent? = null
    private var connecting = false

    private val tokens = object : ConnectionTokenProvider {
        override fun fetchConnectionToken(callback: ConnectionTokenCallback) {
            pendingToken = callback
            nativeNeedToken()
        }
    }

    private val listener = object : TerminalListener {
        override fun onConnectionStatusChange(status: ConnectionStatus) {
            if (status == ConnectionStatus.CONNECTED)
                nativeStatus("Card reader ready", true)
        }
    }

    @JvmStatic
    fun start(context: Context) {
        main.post {
            try {
                if (!Terminal.isInitialized()) {
                    Terminal.init(
                        context = context.applicationContext,
                        logLevel = LogLevel.NONE,
                        tokenProvider = tokens,
                        listener = listener,
                        offlineListener = null,
                        localeConfig = LocaleConfig.CardLanguagePreferenceIfAvailable,
                    )
                }
                connect()
            } catch (e: Throwable) {
                nativeStatus("Card reader: ${e.message}", false)
            }
        }
    }

    // The token the store's computer got from Stripe (or why it couldn't).
    @JvmStatic
    fun provideToken(token: String, error: String) {
        main.post {
            val callback = pendingToken ?: return@post
            pendingToken = null
            if (token.isNotEmpty())
                callback.onSuccess(token)
            else
                callback.onFailure(ConnectionTokenException(error.ifEmpty { "No connection token" }))
        }
    }

    private fun retryLater(why: String) {
        connecting = false
        nativeStatus("Card reader: $why", false)
        main.postDelayed({ connect() }, 10_000)
    }

    private fun connect() {
        if (connecting || Terminal.getInstance().connectedReader != null)
            return
        connecting = true
        nativeStatus("Connecting to the card reader…", false)
        var found = false
        Terminal.getInstance().discoverReaders(
            DiscoveryConfiguration.AppsOnDevicesDiscoveryConfiguration(),
            object : DiscoveryListener {
                override fun onUpdateDiscoveredReaders(readers: List<Reader>) {
                    val reader = readers.firstOrNull() ?: return
                    if (found)
                        return
                    found = true
                    Terminal.getInstance().connectReader(
                        reader,
                        ConnectionConfiguration.AppsOnDevicesConnectionConfiguration(
                            object : AppsOnDevicesListener {
                                override fun onDisconnect(reason: DisconnectReason) {
                                    nativeStatus("Card reader disconnected", false)
                                    main.postDelayed({ connect() }, 3_000)
                                }

                                override fun onReportReaderEvent(event: ReaderEvent) {}
                            }
                        ),
                        object : ReaderCallback {
                            override fun onSuccess(reader: Reader) {
                                connecting = false
                                nativeStatus("Card reader ready", true)
                            }

                            override fun onFailure(e: TerminalException) = retryLater(e.errorMessage)
                        }
                    )
                }
            },
            object : Callback {
                override fun onSuccess() {}
                override fun onFailure(e: TerminalException) = retryLater(e.errorMessage)
            }
        )
    }

    // Take a card for `amount` (cents, tip included).
    @JvmStatic
    fun charge(amount: Long, currency: String, description: String, checkRef: String) {
        main.post {
            val params = PaymentIntentParameters.Builder()
                .setAmount(amount)
                .setCurrency(currency)
                .setCaptureMethod(CaptureMethod.AutomaticAsync)
                .setDescription(description)
                .setMetadata(mapOf("viewtouch_check" to checkRef))
                .build()
            Terminal.getInstance().createPaymentIntent(params, object : PaymentIntentCallback {
                override fun onSuccess(paymentIntent: PaymentIntent) = collect(paymentIntent)
                override fun onFailure(e: TerminalException) = finish("error", e.errorMessage)
            })
        }
    }

    private fun collect(intent: PaymentIntent) {
        current = intent
        collecting = Terminal.getInstance().collectPaymentMethod(
            intent,
            object : PaymentIntentCallback {
                override fun onSuccess(paymentIntent: PaymentIntent) = confirm(paymentIntent)
                override fun onFailure(e: TerminalException) = failed(e)
            },
            // ViewTouch asks for the tip (the customer display); not again on the reader.
            CollectPaymentIntentConfiguration.Builder().skipTipping(true).build()
        )
    }

    private fun confirm(intent: PaymentIntent) {
        collecting = null
        Terminal.getInstance().confirmPaymentIntent(intent, object : PaymentIntentCallback {
            override fun onSuccess(paymentIntent: PaymentIntent) = approved(paymentIntent)
            override fun onFailure(e: TerminalException) = failed(e)
        })
    }

    private fun approved(intent: PaymentIntent) {
        current = null
        // The card, from the charge (or the payment method, when the charge isn't there).
        val charged = intent.latestCharge?.paymentMethodDetails?.cardPresentDetails
        val presented = intent.paymentMethod?.cardPresentDetails
        nativeResult(
            JSONObject()
                .put("status", "approved")
                .put("id", intent.id ?: "")
                .put("amount", intent.amount)
                .put("brand", (charged?.brand ?: presented?.brand)?.toString() ?: "")
                .put("last4", charged?.last4 ?: presented?.last4 ?: "")
                .toString()
        )
    }

    private fun failed(e: TerminalException) {
        collecting = null
        val status = when {
            e.errorCode.name.startsWith("CANCELED") -> "canceled"
            e.paymentIntent?.status?.name == "REQUIRES_PAYMENT_METHOD" -> "declined"
            else -> "error"
        }
        // Nothing is left half-done on Stripe's side.
        current?.let { intent ->
            Terminal.getInstance().cancelPaymentIntent(intent, object : PaymentIntentCallback {
                override fun onSuccess(paymentIntent: PaymentIntent) {}
                override fun onFailure(e: TerminalException) {}
            })
        }
        current = null
        finish(status, e.errorMessage)
    }

    private fun finish(status: String, message: String) {
        nativeResult(JSONObject().put("status", status).put("message", message).toString())
    }

    // Stop waiting for the card (answers "canceled" through failed()).
    @JvmStatic
    fun cancel() {
        main.post {
            collecting?.cancel(object : Callback {
                override fun onSuccess() {}
                override fun onFailure(e: TerminalException) {}
            })
        }
    }
}
