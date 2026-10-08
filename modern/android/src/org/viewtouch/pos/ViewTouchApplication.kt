package org.viewtouch.pos

import com.stripe.stripeterminal.TerminalApplicationDelegate
import org.qtproject.qt.android.bindings.QtApplication

// Qt's application, telling the Stripe Terminal SDK about the app's
// lifecycle (required for Apps on Devices on a Stripe smart reader).
class ViewTouchApplication : QtApplication() {
    override fun onCreate() {
        super.onCreate()
        TerminalApplicationDelegate.onCreate(this)
    }
}
