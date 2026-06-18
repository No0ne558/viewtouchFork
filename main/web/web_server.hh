/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
 *
 * main/web/web_server.hh
 *
 * Embedded HTTP server for the ViewTouch web admin dashboard.
 * Runs in a background thread inside vt_main; serves JSON snapshots of
 * live POS data and allows light management operations.
 *
 * Default port: 9090 (override with env var VIEWTOUCH_WEB_PORT).
 *
 * Endpoint map:
 *   GET  /                    → single-page HTML dashboard
 *   GET  /cdu                 → customer-facing display page
 *   GET  /api/health          → system uptime, version
 *   GET  /api/sales           → today's top-line sales totals
 *   GET  /api/checks          → open checks with table / guests / balance
 *   GET  /api/employees       → employees currently clocked in
 *   GET  /api/menu            → all menu items (id, name, family, price)
 *   POST /api/menu/price      → update an item's price  { "id":N, "price":P }
 *   GET  /api/reports/today   → today's sales broken down by family + top items
 */

#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <string>

class WebAdminServer
{
public:
    explicit WebAdminServer(int port = 9090);
    ~WebAdminServer();

    void Start();
    void Stop();

    bool IsRunning() const noexcept { return running_.load(); }
    int  Port()      const noexcept { return port_; }

private:
    int  port_;
    int  server_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex  write_mtx_;   // guards menu/employee write operations

    void serve();
    void handle(int client_fd) noexcept;

    // Route handlers — return a full HTTP/1.1 response string.
    std::string routeRoot();
    std::string routeCdu();
    std::string routeHealth();
    std::string routeSales();
    std::string routeChecks();
    std::string routeEmployees();
    std::string routeMenu();
    std::string routeMenuPriceUpdate(const std::string &body);
    std::string routeReportsToday();
    std::string routeInventory();
    std::string routeInventorySetCount(const std::string &body);
    std::string route404();
    std::string route405();

    static std::string jsonResponse(const std::string &body);
    static std::string htmlResponse(const std::string &body);
    static std::string errorJson(const std::string &msg, int code = 400);
};
