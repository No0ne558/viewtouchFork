#include "net/build_info.hh"

#include "vtm_build_number.hh"   // generated: VTM_BUILD_NUMBER, VTM_BUILD_COMMIT, VTM_BUILD_VERSION

#include <QCoreApplication>
#include <QSysInfo>

namespace vt::net {

AppBuild appBuild()
{
#if defined(Q_OS_ANDROID)
    const QString os = QStringLiteral("android");
#elif defined(Q_OS_WIN)
    const QString os = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    const QString os = QStringLiteral("macos");
#else
    const QString os = QStringLiteral("linux");
#endif
    return {QStringLiteral(VTM_BUILD_VERSION), VTM_BUILD_NUMBER, QStringLiteral(VTM_BUILD_COMMIT),
            os + u'-' + QSysInfo::buildCpuArchitecture()};
}

QString buildText(const QString &version, int number, const QString &commit)
{
    QString text = QCoreApplication::translate("Build", "%1, build %2").arg(version).arg(number);
    if (!commit.isEmpty())
        text += QStringLiteral(" (%1)").arg(commit);
    return text;
}

} // namespace vt::net
