# ViewTouch V2

A modern rewrite of the ViewTouch POS system using **Qt6** and **C++23**.

## Features

- **Modern UI Framework**: Qt6 replaces the legacy X11/Motif stack
- **Preserved Architecture**: Same Zone/Page/Button concepts as classic ViewTouch
- **Modern C++23**: Uses `std::expected`, `std::format`, concepts, and more
- **JSON Serialization**: Pages and zones can be saved/loaded as JSON
- **Theming Support**: Built-in theme system with dark/light modes
- **Property System**: Edit zone properties dynamically (like classic ViewTouch)

## Building

### Prerequisites

Install Qt6 development packages:

```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-tools-dev cmake g++

# Fedora
sudo dnf install qt6-qtbase-devel qt6-qttools-devel cmake gcc-c++

# Arch Linux
sudo pacman -S qt6-base qt6-tools cmake gcc
```

### Build Steps

```bash
cd v2
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Run

```bash
./viewtouch2
```

## Architecture

```
v2/
├── src/
│   ├── core/           # Core application framework
│   │   ├── application.hpp/cpp   # Main app controller
│   │   ├── config.hpp/cpp        # Configuration management
│   │   ├── logger.hpp/cpp        # Logging system
│   │   └── types.hpp             # Type definitions
│   │
│   ├── ui/             # UI framework
│   │   ├── zone.hpp/cpp          # Base Zone class
│   │   ├── page.hpp/cpp          # Page container
│   │   ├── main_window.hpp/cpp   # Main window
│   │   ├── theme.hpp/cpp         # Theming system
│   │   └── property_editor.hpp   # Zone property editing
│   │
│   ├── zones/          # Zone implementations
│   │   ├── button_zone.hpp/cpp   # Button zones
│   │   ├── order_zone.hpp/cpp    # Order display
│   │   ├── payment_zone.hpp/cpp  # Payment processing
│   │   └── ...
│   │
│   ├── data/           # Business data
│   │   ├── check.hpp/cpp         # Check/bill
│   │   ├── order.hpp/cpp         # Orders
│   │   ├── menu_item.hpp/cpp     # Menu items
│   │   └── employee.hpp/cpp      # Employees
│   │
│   ├── network/        # Network operations
│   │   └── http_client.hpp/cpp   # HTTP client
│   │
│   └── main.cpp        # Entry point
│
├── resources/          # Assets and config
│   └── viewtouch.toml  # Default config
│
└── CMakeLists.txt      # Build configuration
```

## Concepts Preserved from Classic ViewTouch

### Zones
Zones are the fundamental UI building blocks. Every interactive element is a Zone:
- `ButtonZone` - Clickable buttons
- `OrderZone` - Order display
- `PaymentZone` - Payment processing
- `TableZone` - Table selection
- etc.

### Pages
Pages are containers for zones. Users navigate between pages.

### Properties
Zones have editable properties (colors, fonts, sizes, behaviors) that can be configured in design mode.

## Dependencies

| Library | Purpose | Version |
|---------|---------|---------|
| Qt6 | UI Framework | 6.x |
| spdlog | Logging | 1.13.0 |
| nlohmann/json | JSON handling | 3.11.3 |
| magic_enum | Enum reflection | 0.9.5 |
| toml++ | Config files | 3.4.0 |
| cpp-httplib | HTTP client | 0.15.3 |
| Catch2 | Testing | 3.5.2 |

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Escape` | Go back |
| `Ctrl+Escape` | Exit application |
| `F11` | Toggle fullscreen |

## License

Same license as ViewTouch - see LICENSE file in repository root.
