#pragma once

#include <QString>

namespace vt::net {

// This program's build: its version (0.7.0), build number (commits so far:
// a newer build is a higher number), commit, and what it runs on
// ("android-arm64", "linux-x86_64"), to tell screens that need an update.
struct AppBuild {
    QString version;
    int number = 0;
    QString commit;
    QString platform;
};

AppBuild appBuild();
// "0.7.0, build 812 (a13bde2)".
QString buildText(const QString &version, int number, const QString &commit = {});

} // namespace vt::net
