import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ViewTouch

Rectangle {
    id: terminal
    color: "#2E3440"  // Dark background like original ViewTouch

    // Properties
    property var mainWindow

    // Page management
    property int currentPage: -1  // Default page -1
    property bool editMode: false
    property var pages: ({})  // Object to store page data
    property var pageProperties: ({})  // Object to store page properties

    // Status bar at top
    Rectangle {
        id: statusBar
        height: 40
        width: parent.width
        color: editMode ? "#BF616A" : "#4C566A"  // Red when in edit mode
        anchors.top: parent.top

        Text {
            anchors.centerIn: parent
            text: "ViewTouch V2 - Page " + currentPage + (editMode ? " (EDIT MODE - F1 to exit)" : " (F1 for edit mode)")
            color: "white"
            font.pixelSize: 16
            font.bold: true
        }
    }

    // F1 shortcut for edit mode
    Shortcut {
        sequence: "F1"
        onActivated: {
            editMode = !editMode
            if (editMode) {
                enterEditMode()
            } else {
                exitEditMode()
            }
        }
    }

    // Edit toolbar (visible in edit mode)
    EditToolbar {
        id: editToolbar
        anchors.top: statusBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: editMode
        mainWindow: terminal.mainWindow
    }

    // Main content area
    Rectangle {
        id: contentArea
        anchors.top: editMode ? editToolbar.bottom : statusBar.bottom
        anchors.bottom: messageArea.top
        anchors.left: parent.left
        anchors.right: parent.right
        color: "#3B4252"

        // Page container
        Item {
            id: pageContainer
            anchors.fill: parent

            // Current page zones
            Repeater {
                id: zoneRepeater
                model: getCurrentPageZones()

                delegate: Zone {
                    x: modelData.x
                    y: modelData.y
                    width: modelData.width
                    height: modelData.height
                    zoneType: modelData.type
                    zoneText: modelData.text || ""
                    zoneId: modelData.id
                    editMode: terminal.editMode

                    onZoneClicked: function(zoneId) {
                        if (terminal.editMode) {
                            editZone(zoneId)
                        } else {
                            handleZoneClick(zoneId)
                        }
                    }

                    onZoneEdited: function(zoneId, newProperties) {
                        updateZoneProperties(zoneId, newProperties)
                    }
                }
            }

            // Edit mode overlay
            Rectangle {
                id: editOverlay
                anchors.fill: parent
                color: "transparent"
                visible: editMode

                // Click to create new zone
                MouseArea {
                    anchors.fill: parent
                    onClicked: function(mouse) {
                        createNewZone(mouse.x, mouse.y)
                    }
                }

                // Edit instructions
                Text {
                    anchors.centerIn: parent
                    text: "Click to create new zone\nRight-click zone to edit properties"
                    color: "white"
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.7
                }
            }
        }
    }

    // Message area at bottom
    Rectangle {
        id: messageArea
        height: 60
        width: parent.width
        color: "#4C566A"
        anchors.bottom: parent.bottom

        Text {
            id: messageText
            anchors.centerIn: parent
            text: editMode ? "Edit Mode: Click to create zones, right-click to edit. F1 to exit" : "Welcome to ViewTouch V2 - Page " + currentPage + " - F1 for edit mode"
            color: "white"
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Timer to update status
    Timer {
        interval: 1000
        running: !editMode
        repeat: true
        onTriggered: {
            var date = new Date()
            if (!editMode) {
                messageText.text = "ViewTouch V2 - Page " + currentPage + " - " + date.toLocaleTimeString()
            }
        }
    }

    // Page and zone management functions
    function getCurrentPageZones() {
        if (!pages[currentPage]) {
            // Initialize page with empty zones (no default zones)
            pages[currentPage] = []
        }
        return pages[currentPage]
    }

    function createNewZone(x, y) {
        var pageZones = pages[currentPage] || []
        var newId = Math.max(...pageZones.map(z => z.id), 0) + 1

        var newZone = {
            id: newId,
            type: "button",
            text: "New Zone " + newId,
            x: Math.max(0, x - 50), // Center on click
            y: Math.max(0, y - 25),
            width: 150,
            height: 80
        }

        pageZones.push(newZone)
        pages[currentPage] = pageZones
        zoneRepeater.model = getCurrentPageZones() // Refresh
        messageText.text = "Created new zone " + newId
    }

    function editZone(zoneId) {
        // This will be implemented when we add zone editing dialog
        messageText.text = "Editing zone " + zoneId + " (not implemented yet)"
    }

    function updateZoneProperties(zoneId, newProperties) {
        var pageZones = pages[currentPage] || []
        var zoneIndex = pageZones.findIndex(z => z.id === zoneId)
        if (zoneIndex >= 0) {
            pageZones[zoneIndex] = Object.assign(pageZones[zoneIndex], newProperties)
            pages[currentPage] = pageZones
            zoneRepeater.model = getCurrentPageZones() // Refresh
        }
    }

    function handleZoneClick(zoneId) {
        messageText.text = "Zone " + zoneId + " clicked"
        // Here we would handle the actual zone action
    }

    function enterEditMode() {
        messageText.text = "Edit Mode: Click to create zones, right-click zones to edit"
    }

    function createNewPage(properties) {
        // Create new page with properties
        var newPageNumber = properties.pageNumber
        if (pages[newPageNumber]) {
            messageText.text = "Page " + newPageNumber + " already exists!"
            return
        }

        // Initialize empty page
        pages[newPageNumber] = []

        // Store page properties
        if (!pageProperties) {
            pageProperties = {}
        }
        pageProperties[newPageNumber] = properties

        // Switch to the new page
        currentPage = newPageNumber

        // Update UI colors if this page has custom colors
        updatePageStyling(properties)

        messageText.text = "Created new page " + newPageNumber + ": " + properties.pageName
        console.log("Created page", newPageNumber, "with properties:", JSON.stringify(properties))
    }

    function updatePageStyling(properties) {
        if (properties.titleBarColor) {
            statusBar.color = editMode ? "#BF616A" : properties.titleBarColor
        }
        if (properties.backgroundColor) {
            contentArea.color = properties.backgroundColor
        }
    }

    function exitEditMode() {
        // Auto-save current page when exiting edit mode
        saveCurrentPage()
        messageText.text = "Exited edit mode - Page " + currentPage + " saved"
    }

    function saveCurrentPage() {
        // Save page data and properties to persistent storage
        var pageData = {
            pageNumber: currentPage,
            zones: pages[currentPage] || [],
            properties: pageProperties[currentPage] || {}
        }

        // For now, just log the save operation (proper file persistence to be implemented)
        console.log("Auto-saved page", currentPage, "with", pageData.zones.length, "zones and properties:", JSON.stringify(pageData.properties))

        // TODO: Implement proper file-based persistence
        // This would save to JSON files in the user's data directory
    }

    // Initialize
    Component.onCompleted: {
        // Load saved pages from storage (future implementation)
        console.log("Terminal initialized with page", currentPage)
    }
}