# ViewTouch V2

Modern Qt6-based implementation of ViewTouch POS system.

## Current Status

✅ **Completed:**
- Basic Qt6 QML application structure
- Fullscreen terminal window (1920x1080)
- **Page System**: Default page -1 with zone management
- **Edit Mode**: Press F1 to enter edit mode with toolbar
- **Page Creation**: Create new System Pages with full property configuration
- **Auto-Save**: Changes automatically saved when exiting edit mode
- CMake build system with Qt6

🚧 **In Progress:**
- Zone system migration from X11/Motif to QML
- Integration with existing business logic
- Touch gesture support
- Page persistence and loading

## Building

```bash
cd v2
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Running

```bash
./ViewTouchV2
```

### Keyboard Shortcuts
- **F1**: Toggle edit mode
- **F11**: Toggle fullscreen/windowed mode
- **Ctrl+F**: Force fullscreen
- **Ctrl+Q**: Quit application

Use Ctrl+Q to quit, F11 to toggle fullscreen.

## Page and Zone System

### Pages
- **Default Page**: -1 (starts empty, ready for custom design)
- **Page Management**: Stored in JavaScript object (future: persistent storage)
- **Page Switching**: Planned for future implementation

### Zones
- **Types**: Button, Label, Input
- **Properties**: Position, size, text, type
- **Edit Mode**: Press **F1** to enter edit mode
  - **Create**: Click empty area to create new zone
  - **Edit**: Right-click zone to edit properties
  - **Resize**: Drag bottom-right corner to resize
  - **Move**: Zones can be repositioned (future feature)

### Edit Mode Features
- Status bar turns red when in edit mode
- Visual indicators show zone IDs
- Resize handles appear on zones
- Click outside zones to create new ones
- Press **F1** to toggle edit mode
- **Edit Toolbar** appears with page management tools

### Edit Toolbar
- **Create New Page**: Opens comprehensive page properties dialog
- **Auto-Save**: Changes are automatically saved when exiting edit mode (F1)

### Page Properties Dialog
When creating a new page, configure:
- **Page Type**: System Page (currently only option)
- **Page Name**: Display name for the page
- **Page Number**: Unique identifier (0-9999)
- **Title Bar Color**: Custom status bar color
- **Background Color**: Page background color
- **Button Styling**: Font, colors, edges, shadows for all buttons
- **Spacing**: Layout spacing between elements
- **Parent Page**: Hierarchical page relationships

## Architecture

### Files
- `main.cpp` - Qt application entry point
- `Main.qml` - Main window with fullscreen setup
- `Terminal.qml` - Terminal interface with page/zone management
- `Zone.qml` - Reusable zone component with editing capabilities

### Key Components
- **Qt6 Quick** - Declarative UI framework
- **QML** - UI markup language with JavaScript integration
- **C++23** - Modern C++ with Qt integration
- **Page System** - Hierarchical organization of UI elements
- **Zone Components** - Modular, editable UI elements

## Next Steps

### Phase 1: Enhanced Editing (Week 1)
1. **Page Styling Application**
   - Apply page properties to UI (colors, fonts, spacing)
   - Button styling based on page settings
   - Dynamic theme switching

2. **Zone Property Dialog**
   - Full property editor dialog for individual zones
   - Zone-specific overrides from page defaults
   - Advanced styling options per zone

3. **Page Management**
   - Switch between created pages
   - Page navigation controls
   - Page hierarchy (parent/child relationships)

### Phase 2: Persistence (Week 2)
1. **Data Storage**
   - Save/load pages to JSON files
   - Zone configuration persistence
   - Page metadata storage

2. **Import/Export**
   - Export page configurations
   - Import from existing ViewTouch setups
   - Backup and restore functionality

### Phase 3: Business Logic Integration (Week 3-4)
1. **Zone Actions**
   - Connect zones to business logic
   - Menu item zones
   - Order management zones

2. **Data Binding**
   - Real-time data updates
   - Qt models for dynamic content
   - State synchronization

### Phase 4: Advanced Features (Month 2+)
1. **Touch Gestures**
   - Multi-touch support
   - Gesture recognition
   - Advanced interactions

2. **Themes and Styling**
   - Custom themes
   - Branding support
   - Accessibility features

## Development Guidelines

- Use Qt's model-view architecture
- Prefer QML for UI, C++ for business logic
- Follow Qt coding conventions
- Write unit tests for C++ components
- Use QML's built-in testing for UI

## Dependencies

- Qt6 Core, Quick, QML
- CMake 3.20+
- C++23 compiler (GCC 13+)

## Compatibility

- Designed for 1920x1080 touchscreen displays
- Linux first, Windows/Mac support planned
- Maintains network transparency features