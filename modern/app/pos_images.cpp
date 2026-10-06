// PosService: the store's pictures - a logo, button pictures, backgrounds.
// Kept in the database (so backups, the standby and paired screens have
// them) and written out as files for this computer's screens to show.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {
constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;
const QStringList kImageTypes = {u"png"_s, u"jpg"_s, u"jpeg"_s, u"webp"_s, u"gif"_s, u"svg"_s, u"bmp"_s};
const QStringList kFontTypes = {u"ttf"_s, u"otf"_s};

bool isFont(const QString &name)
{
    return kFontTypes.contains(QFileInfo(name).suffix().toLower());
}

// By the first bytes: PNG, JPEG, GIF, WebP, BMP or SVG.
bool looksLikePicture(const QByteArray &d)
{
    return d.startsWith(QByteArray("\x00\x01\x00\x00", 4)) || d.startsWith("OTTO") || d.startsWith("true")   // fonts
           || d.startsWith("\x89PNG") || d.startsWith("\xff\xd8") || d.startsWith("GIF8") || d.startsWith("BM")
           || (d.startsWith("RIFF") && d.mid(8, 4) == "WEBP")
           || d.left(512).contains("<svg");
}

QString contentHash(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(16));
}
} // namespace

QString PosShared::imageFile(const QString &ref)
{
    const QString name = ref.startsWith(u"store:") ? ref.mid(6) : ref;
    const auto it = images.find(ss(name));
    if (it == images.end())
        return {};
    // Named by content: a picture replaced under the same name is a new file.
    const QString dir = imageCacheDir.isEmpty() ? QDir::temp().filePath(u"vtm-store-images"_s) : imageCacheDir;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(contentHash(it->second) + u'.' + QFileInfo(name).suffix().toLower());
    if (!QFileInfo::exists(path)) {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(it->second) != it->second.size() || !f.commit())
            return {};
    }
    return path;
}

QString PosService::imageUrl(const QString &ref) const
{
    if (ref.isEmpty())
        return {};
    if (ref == u"logo:") {   // the store's logo, wherever it is
        const QString logo = qs(s_->settings.displayLogo);
        return logo == u"logo:" ? QString() : imageUrl(logo);
    }
    if (ref.startsWith(u"store:")) {
        const QString file = s_->imageFile(ref);
        return file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();
    }
    if (ref.startsWith(u'/'))
        return QUrl::fromLocalFile(ref).toString();
    return ref;   // qrc:, file:, http...
}

QVariantList PosService::storeImages() const
{
    QVariantList out;
    for (const auto &[name, data] : s_->images)
        out.append(QVariantMap{{u"name"_s, qs(name)}, {u"ref"_s, u"store:"_s + qs(name)},
                               {u"kind"_s, isFont(qs(name)) ? u"font"_s : u"picture"_s},
                               {u"hash"_s, contentHash(data)}, {u"bytes"_s, qint64(data.size())},
                               {u"url"_s, imageUrl(u"store:"_s + qs(name))}});
    return out;
}

bool PosService::addStoreImage(const QString &fileName, const QString &base64)
{
    if (!require(perm::Manager, tr("Adding pictures")))
        return false;
    const QString ref = storeImageRef(fileName);
    if (ref.isEmpty())
        return fail(tr("Pictures can be PNG, JPEG, WebP, GIF, BMP or SVG files; fonts TTF or OTF."));
    const QString name = ref.mid(6);
    const QString suffix = QFileInfo(name).suffix();
    const QByteArray data = QByteArray::fromBase64(base64.toLatin1());
    if (data.isEmpty())
        return fail(tr("That file is empty."));
    if (data.size() > kMaxImageBytes)
        return fail(tr("That picture is too big (8 MB at most)."));
    if (!looksLikePicture(data))
        return fail(isFont(name) ? tr("That file isn't a font this program can use.")
                                 : tr("That file isn't a picture this program can show."));
    const bool replaced = s_->images.contains(ss(name));
    s_->images[ss(name)] = data;
    if (s_->sink)
        s_->sink->saveImage(ss(name), data);
    ++s_->adminRevision;
    emit s_->adminChanged();
    emit notice(replaced ? tr("Picture %1 replaced").arg(name) : tr("Picture %1 added").arg(name));
    return true;
}

bool PosService::removeStoreImage(const QString &name)
{
    if (!require(perm::Manager, tr("Removing pictures")))
        return false;
    const QString plain = name.startsWith(u"store:") ? name.mid(6) : name;
    if (!s_->images.erase(ss(plain)))
        return fail(tr("There is no picture %1.").arg(plain));
    if (s_->sink)
        s_->sink->deleteImage(ss(plain));
    ++s_->adminRevision;
    emit s_->adminChanged();
    emit notice(tr("Picture %1 removed").arg(plain));
    return true;
}

QString PosSession::storeImageRef(const QString &fileName)
{
    // A plain name: "Logo Final.PNG" -> "store:logo-final.png".
    const QFileInfo info(fileName);
    const QString suffix = info.suffix().toLower();
    if (!kImageTypes.contains(suffix) && !kFontTypes.contains(suffix))
        return {};
    static const QRegularExpression unsafe(u"[^a-z0-9]+"_s);
    QString base = info.completeBaseName().toLower();
    base.replace(unsafe, u"-"_s);
    while (base.startsWith(u'-')) base.remove(0, 1);
    while (base.endsWith(u'-')) base.chop(1);
    if (base.isEmpty())
        base = u"picture"_s;
    return u"store:"_s + base.left(40) + u'.' + (suffix == u"jpeg" ? u"jpg"_s : suffix);
}

QString PosSession::addImageFile(const QString &fileOrUrl)
{
    // Read here (this computer's file), kept by the store.
    const QString path = fileOrUrl.startsWith(u"file:") ? QUrl(fileOrUrl).toLocalFile() : fileOrUrl;
    QFile f(path);
    if (f.size() > kMaxImageBytes) {
        emit notice(tr("That picture is too big (8 MB at most)."));
        return {};
    }
    if (!f.open(QIODevice::ReadOnly)) {
        emit notice(tr("Can't open %1.").arg(QFileInfo(path).fileName()));
        return {};
    }
    const QString ref = storeImageRef(QFileInfo(path).fileName());
    if (ref.isEmpty()) {
        emit notice(tr("Pictures can be PNG, JPEG, WebP, GIF, BMP or SVG files; fonts TTF or OTF."));
        return {};
    }
    invoke(u"addStoreImage"_s, {QFileInfo(path).fileName(), QString::fromLatin1(f.readAll().toBase64())});
    return ref;
}

} // namespace vt::app
