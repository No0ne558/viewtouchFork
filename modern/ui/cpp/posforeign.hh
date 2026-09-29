#pragma once

// Registers the app-layer PosService with QML without making app/ depend on
// QtQml: widgets see it as the type `PosService`.

#include "app/pos_service.hh"

#include <QtQml/qqmlregistration.h>

struct PosServiceForeign {
    Q_GADGET
    QML_FOREIGN(vt::app::PosService)
    QML_NAMED_ELEMENT(PosService)
    QML_UNCREATABLE("Provided by LayoutController.pos")
};
