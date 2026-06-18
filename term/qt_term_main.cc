/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 * qt_term_main.cc
 * Entry point for the Qt6 vt_term binary.
 *
 * argv[1] = socket_file (Unix domain socket to connect to vt_main)
 * argv[2] = term_hardware (int, passed on but unused by Qt6 backend)
 * argv[3] = display string (set as DISPLAY env var if not already set)
 * argv[4] = is_local (1 = local, 0 = remote)
 * argv[5] = set_width  (optional override)
 * argv[6] = set_height (optional override)
 *
 * Qt6 reads DISPLAY automatically via the xcb platform plugin, so
 * XSDL and SSH X11 forwarding work unchanged. For Wayland:
 *   QT_QPA_PLATFORM=wayland vt_term ...
 */

#include "qt_term_view.hh"
#include "basic.hh"
#include "src/utils/vt_logger.hh"

#include <QApplication>
#include <QScreen>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <string>

static int ConnectToServer(const std::string &socket_file)
{
    struct sockaddr_un adr{};
    adr.sun_family = AF_UNIX;
    strncpy(adr.sun_path, socket_file.c_str(), sizeof(adr.sun_path) - 1);

    const int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock <= 0) {
        std::cerr << "vt_term: socket() failed: " << strerror(errno) << '\n';
        return 0;
    }

    // vt_main takes a moment to start its listener
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (connect(sock, reinterpret_cast<struct sockaddr *>(&adr), SUN_LEN(&adr)) == 0)
            return sock;
        usleep(100000);   // 100 ms
    }

    std::cerr << "vt_term: could not connect to '" << socket_file << "': "
              << strerror(errno) << '\n';
    close(sock);
    return 0;
}

int main(int argc, genericChar *argv[])
{
    vt::Logger::Initialize("/var/log/viewtouch", "info", false, true);
    vt::Logger::info("vt_term (Qt6) starting");

    if (argc < 2) {
        std::cerr << "Usage: vt_term <socket_file> [term_hw] [display] [is_local] [w] [h]\n";
        return 1;
    }

    const std::string socket_file = argv[1];
    // int  term_hw   = (argc > 2) ? std::atoi(argv[2]) : 0;  // unused
    const char *display_arg      = (argc > 3) ? argv[3] : nullptr;
    const int  is_local          = (argc > 4) ? std::atoi(argv[4]) : 1;
    // int  set_width  = (argc > 5) ? std::atoi(argv[5]) : 0;  // Qt reads screen
    // int  set_height = (argc > 6) ? std::atoi(argv[6]) : 0;

    // Set DISPLAY if supplied and not already in environment
    if (display_arg && display_arg[0] && !getenv("DISPLAY"))
        setenv("DISPLAY", display_arg, 1);

    // Connect to vt_main BEFORE creating QApplication (pure POSIX)
    int sock = ConnectToServer(socket_file);
    if (sock <= 0) {
        std::cerr << "vt_term: failed to connect to vt_main\n";
        return 1;
    }
    vt::Logger::info("vt_term: connected to {}", socket_file);

    // Qt6 reads DISPLAY / WAYLAND_DISPLAY automatically
    QApplication app(argc, argv);
    app.setApplicationName("ViewTouch Terminal");
    app.setApplicationDisplayName("ViewTouch POS");

    TermWidget terminal(sock, is_local);
    terminal.showFullScreen();   // frameless full-screen on the display
    terminal.start();            // sends SrvTermInfo, begins receiving commands

    int ret = app.exec();
    close(sock);
    vt::Logger::Shutdown();
    return ret;
}
