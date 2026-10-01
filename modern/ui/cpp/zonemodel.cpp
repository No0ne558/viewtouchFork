#include "zonemodel.hh"

int ZoneModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(rows_.size());
}

QVariant ZoneModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    return rows_[index.row()].value(role);
}

QHash<int, QByteArray> ZoneModel::roleNames() const
{
    return {
        {ZoneIdRole, "zoneId"},
        {KindRole, "kind"},
        {ZoneNameRole, "zoneName"},
        {LabelRole, "label"},
        {ZoneXRole, "zoneX"},
        {ZoneYRole, "zoneY"},
        {ZoneWRole, "zoneW"},
        {ZoneHRole, "zoneH"},
        {ShapeRole, "shape"},
        {BehaviorRole, "behavior"},
        {ZoneEnabledRole, "zoneEnabled"},
        {InheritedRole, "inherited"},
        {CurrentRole, "current"},
        {HotkeyRole, "hotkey"},
        {GroupRole, "group"},
        {ImagePathRole, "imagePath"},
        {StyleNormalRole, "styleNormal"},
        {StyleSelectedRole, "styleSelected"},
        {StyleDisabledRole, "styleDisabled"},
        {PropsRole, "props"},
        {SoldOutRole, "soldOut"},
    };
}

void ZoneModel::setRows(QList<Row> rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}
