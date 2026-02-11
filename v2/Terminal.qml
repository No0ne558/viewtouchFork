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

        onCreateNewPage: function(properties) {
            createNewPage(properties)
        }

        onCreateNewZone: function(properties) {
            createNewZone(properties)
        }
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
                    zoneColor: modelData.color || "#81A1C1"
                    zoneBorderColor: modelData.borderColor || "#4C566A"
                    zoneBorderWidth: modelData.borderWidth || 1
                    zoneRadius: modelData.radius || 8

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
                        createNewZoneAtPosition(mouse.x, mouse.y)
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

    function createNewZoneAtPosition(x, y) {
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

    function createNewZone(properties) {
        // Ensure we have a current page
        if (currentPage === -1) {
            messageText.text = "No active page! Create a page first."
            return
        }

        // Initialize page zones array if it doesn't exist
        if (!pages[currentPage]) {
            pages[currentPage] = []
        }

        // Generate a unique zone ID
        var zoneId = 1
        var existingIds = pages[currentPage].map(function(zone) { return zone.id })
        while (existingIds.includes(zoneId)) {
            zoneId++
        }

        // Create new zone object
        var newZone = {
            id: zoneId,
            type: properties.zoneType,
            text: properties.text,
            x: properties.x,
            y: properties.y,
            width: properties.width,
            height: properties.height,
            color: properties.color,
            borderColor: properties.borderColor,
            borderWidth: properties.borderWidth,
            radius: properties.radius
        }

        // Add to current page
        pages[currentPage].push(newZone)

        // Refresh the zone repeater
        zoneRepeater.model = getCurrentPageZones()

        messageText.text = "Created new " + properties.zoneType + " zone (ID: " + zoneId + ")"
        console.log("Created zone", zoneId, "with properties:", JSON.stringify(properties))
    }

    function updateZoneProperties(zoneId, newProperties) {
        if (newProperties.action === "openPropertiesDialog") {
            // Open zone properties dialog for editing
            openZonePropertiesDialog(zoneId, newProperties)
        } else {
            // Update zone properties directly
            var pageZones = pages[currentPage] || []
            for (var i = 0; i < pageZones.length; i++) {
                if (pageZones[i].id === zoneId) {
                    // Update properties
                    if (newProperties.text !== undefined) pageZones[i].text = newProperties.text
                    if (newProperties.width !== undefined) pageZones[i].width = newProperties.width
                    if (newProperties.height !== undefined) pageZones[i].height = newProperties.height
                    if (newProperties.x !== undefined) pageZones[i].x = newProperties.x
                    if (newProperties.y !== undefined) pageZones[i].y = newProperties.y
                    if (newProperties.color !== undefined) pageZones[i].color = newProperties.color
                    if (newProperties.borderColor !== undefined) pageZones[i].borderColor = newProperties.borderColor
                    if (newProperties.borderWidth !== undefined) pageZones[i].borderWidth = newProperties.borderWidth
                    if (newProperties.radius !== undefined) pageZones[i].radius = newProperties.radius

                    // Refresh the zone repeater
                    zoneRepeater.model = getCurrentPageZones()
                    messageText.text = "Updated zone " + zoneId + " properties"
                    console.log("Updated zone", zoneId, "with properties:", JSON.stringify(newProperties))
                    return
                }
            }
        }
    }

    function openZonePropertiesDialog(zoneId, currentProperties) {
        // Find the zone
        var pageZones = pages[currentPage] || []
        for (var i = 0; i < pageZones.length; i++) {
            if (pageZones[i].id === zoneId) {
                // Open zone properties dialog with current values
                // For now, just show a message - full dialog implementation would be more complex
                messageText.text = "Zone properties dialog for zone " + zoneId + " (not yet implemented)"
                return
            }
        }
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