#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>

namespace vt::layout::schema {

// Editable-field descriptions that drive the editor's inspector. Each field is
// a JSON object:
//
//   path         dotted path into the zone/page/theme JSON ("style.normal.fill")
//   label        human-readable name
//   type         string|text|int|bool|enum|color|texture|font|page|pageList|actions
//   group        section heading in the inspector
//   options      [{value, text}] for enum
//   min, max     for int
//   inheritable  true when "unset" means "inherit from page/template/theme"
//   hint         short help text
//
// Adding a property to the UI means adding a line here; no QML changes.

// Kinds that render as plain elements; everything else is a widget panel.
QStringList basicKinds();
QStringList widgetKinds();
QStringList allKinds();
bool isWidgetKind(const QString &kind);

QStringList pageKinds();
QStringList pageRoles();

QJsonArray zoneFields(const QString &kind);
QJsonArray pageFields();
QJsonArray themeFields();

// Action types for the action-list editor: [{type, label, fields: [...]}].
QJsonArray actionTypes();

} // namespace vt::layout::schema
