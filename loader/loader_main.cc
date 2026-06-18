/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * loader_main.cc - Qt6 replacement for Xlib/Motif splash window
 * Shows a startup splash while vt_main initialises, then exits.
 */

#include "basic.hh"
#include "logger.hh"
#include "utility.hh"
#include "src/utils/vt_logger.hh"
#include "version/vt_version_info.hh"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>
#include <QSocketNotifier>
#include <QInputDialog>
#include <QByteArray>
#include <QString>
#include <QScreen>
#include <QFont>
#include <QFontMetrics>
#include <QPalette>
#include <QStyle>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <array>

static const std::string SOCKET_FILE  = "/tmp/vt_main";
static const std::string COMMAND_FILE = VIEWTOUCH_PATH "/bin/.vtpos_command";

static int SocketNo = 0;
static QApplication *g_app = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Qt6 splash/status dialog
// ─────────────────────────────────────────────────────────────────────────────
class LoaderDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoaderDialog(int socket_fd)
        : QDialog(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
        , socket_fd_(socket_fd)
    {
        setFixedSize(640, 200);
        setStyleSheet(
            "QDialog { background-color: #1a1a2e; border: 2px solid #4a9eff; border-radius: 8px; }"
            "QLabel#title { color: #4a9eff; font-size: 22px; font-weight: bold; }"
            "QLabel#status { color: #e0e0e0; font-size: 13px; }"
            "QLabel#version { color: #888888; font-size: 11px; }");

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(32, 24, 32, 24);
        layout->setSpacing(12);

        title_label_ = new QLabel("ViewTouch POS", this);
        title_label_->setObjectName("title");
        title_label_->setAlignment(Qt::AlignCenter);
        layout->addWidget(title_label_);

        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #4a9eff;");
        layout->addWidget(sep);

        status_label_ = new QLabel("Starting…", this);
        status_label_->setObjectName("status");
        status_label_->setAlignment(Qt::AlignCenter);
        status_label_->setWordWrap(true);
        layout->addWidget(status_label_);

        QString ver = QString::fromStdString(viewtouch::get_version_short());
        auto *version_label = new QLabel("Version " + ver, this);
        version_label->setObjectName("version");
        version_label->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        layout->addWidget(version_label);

        // Centre on primary screen
        if (QScreen *screen = QApplication::primaryScreen()) {
            QRect sg = screen->geometry();
            move((sg.width() - width()) / 2, (sg.height() - height()) / 2);
        }

        if (socket_fd_ > 0) {
            notifier_ = new QSocketNotifier(socket_fd_, QSocketNotifier::Read, this);
            connect(notifier_, &QSocketNotifier::activated, this, &LoaderDialog::onSocketData);
        }
    }

public slots:
    void onSocketData()
    {
        char byte;
        while (::read(socket_fd_, &byte, 1) == 1) {
            if (byte == '\0') {
                // End of message
                QString msg = QString::fromUtf8(recv_buf_);
                recv_buf_.clear();
                if (msg == "done") {
                    vt::Logger::info("Loader: received 'done', exiting");
                    if (g_app) g_app->quit();
                    return;
                }
                // Show status text; '\' is used as newline separator
                msg.replace('\\', '\n');
                status_label_->setText(msg);
            } else if (byte == '\r') {
                // Server is asking for temporary key input
                recv_buf_.clear();
                bool ok = false;
                QString key = QInputDialog::getText(
                    this, "ViewTouch – Temporary Key",
                    "Enter temporary activation key:",
                    QLineEdit::Normal, QString(), &ok);
                if (ok && !key.isEmpty()) {
                    QByteArray ba = key.toUtf8();
                    ::write(socket_fd_, ba.constData(), ba.size());
                    ::write(socket_fd_, "\0", 1);
                } else {
                    ::write(socket_fd_, "quit", 4);
                }
            } else {
                recv_buf_.append(byte);
            }
        }
    }

private:
    int socket_fd_;
    QSocketNotifier *notifier_{nullptr};
    QLabel *title_label_{nullptr};
    QLabel *status_label_{nullptr};
    QByteArray recv_buf_;
};

#include "loader_main.moc"

// ─────────────────────────────────────────────────────────────────────────────
// POSIX helpers (unchanged from original – no X11 dependency)
// ─────────────────────────────────────────────────────────────────────────────
static bool WriteArgList(int argc, char *argv[])
{
    std::ofstream fout(COMMAND_FILE, std::fstream::trunc);
    if (!fout.is_open()) {
        std::cerr << "vtpos: failed to open command file '" << COMMAND_FILE << "'\n";
        return false;
    }
    for (int i = 0; i < argc; i++)
        fout << argv[i] << " ";
    fout.close();

    if (chmod(COMMAND_FILE.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        std::cerr << "vtpos: failed to chmod '" << COMMAND_FILE << "'\n";
        return false;
    }
    return true;
}

static void SetPerms()
{
    genericChar emp_data[] = VIEWTOUCH_PATH "/dat/employee.dat.bak";
    int fd = open(emp_data, O_RDONLY);
    if (fd >= 0) {
        fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        close(fd);
    }
}

static int SetupConnection(const std::string &socket_file)
{
    struct sockaddr_un server_adr{}, client_adr{};
    server_adr.sun_family = AF_UNIX;
    strncpy(server_adr.sun_path, socket_file.c_str(), sizeof(server_adr.sun_path) - 1);

    unlink(socket_file.c_str());

    const int dev = socket(AF_UNIX, SOCK_STREAM, 0);
    if (dev <= 0) {
        logmsg(LOG_ERR, "vtpos: failed to create socket");
        return 0;
    }

    if (bind(dev, reinterpret_cast<struct sockaddr *>(&server_adr), SUN_LEN(&server_adr)) < 0) {
        logmsg(LOG_ERR, "vtpos: failed to bind socket '%s'", socket_file.c_str());
        close(dev);
        return 0;
    }

    const std::string cmd = std::string(VIEWTOUCH_PATH) + "/bin/vt_main " + socket_file + "&";
    system(cmd.c_str());  // NOLINT(cert-env33-c)

    listen(dev, 1);
    socklen_t len = sizeof(client_adr);
    int sock = accept(dev, reinterpret_cast<struct sockaddr *>(&client_adr), &len);
    close(dev);
    unlink(socket_file.c_str());

    if (sock <= 0) {
        logmsg(LOG_ERR, "vtpos: failed to accept connection from vt_main");
        return 0;
    }
    return sock;
}

static void SignalFn(int sig)
{
    logmsg(LOG_ERR, "vtpos: caught signal %d, exiting", sig);
    if (SocketNo > 0) close(SocketNo);
    _exit(1);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, genericChar *argv[])
{
    SetPerms();

#ifdef DEBUG
    vt::Logger::Initialize("/var/log/viewtouch", "debug", true, true);
#else
    vt::Logger::Initialize("/var/log/viewtouch", "info", false, true);
#endif
    vt::Logger::info("ViewTouch Loader (vtpos) starting - Version {}",
                     viewtouch::get_version_short());

    signal(SIGINT, SignalFn);
    signal(SIGTERM, SignalFn);

    // Parse command line
    int net_off = 0;
    int purge   = 0;
    int notrace = 0;
    const char *data_path = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "help") == 0) {
            printf("Command line options:\n"
                   "  -p <dir>  specify data directory\n"
                   "  -n        no network devices\n"
                   "  -h        this help\n"
                   "  -v        print version\n\n");
            return 0;
        } else if (strcmp(arg, "-p") == 0 || strcmp(arg, "path") == 0) {
            if (++i < argc) data_path = argv[i];
        } else if (strcmp(arg, "-n") == 0 || strcmp(arg, "netoff") == 0) {
            net_off = 1;
        } else if (strcmp(arg, "purge") == 0) {
            purge = 1;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "notrace") == 0) {
            notrace = 1;
        } else if (strcmp(arg, "version") == 0) {
            printf("1\n");
            return 0;
        } else if (strcmp(arg, "-v") == 0) {
            std::cout << viewtouch::get_project_name() << " "
                      << viewtouch::get_version_info() << '\n';
            return 0;
        }
    }

    if (!WriteArgList(argc, argv)) {
        std::cerr << "vtpos: failed to write argument file\n";
        return 1;
    }

    // Build Qt application (reads DISPLAY / Wayland env vars automatically)
    QApplication app(argc, argv);
    g_app = &app;
    app.setApplicationName("ViewTouch POS");
    app.setApplicationVersion(QString::fromStdString(viewtouch::get_version_short()));

    // Connect to vt_main
    SocketNo = SetupConnection(SOCKET_FILE);
    if (SocketNo <= 0) {
        std::cerr << "vtpos: could not start vt_main\n";
        return 1;
    }

    // Show splash window
    LoaderDialog dialog(SocketNo);
    dialog.show();

    // Send startup parameters to vt_main
    // Protocol: null-terminated strings, "display <name>\0", "done\0" etc.
    const char *displaystr = getenv("DISPLAY");
    if (!displaystr) displaystr = ":0";
    const int displaylen = static_cast<int>(strlen(displaystr));

    ::write(SocketNo, "display ", 8);
    ::write(SocketNo, displaystr, displaylen + 1);  // includes '\0'

    if (data_path) {
        ::write(SocketNo, "datapath ", 9);
        ::write(SocketNo, data_path, strlen(data_path) + 1);
    }
    if (net_off) ::write(SocketNo, "netoff", 7);
    if (purge)   ::write(SocketNo, "purge",  6);
    if (notrace) ::write(SocketNo, "notrace", 8);
    ::write(SocketNo, "done", 5);  // signals end of startup options

    // Run Qt event loop; the QSocketNotifier in LoaderDialog drives everything
    int ret = app.exec();

    if (SocketNo > 0) close(SocketNo);
    vt::Logger::Shutdown();
    return ret;
}
