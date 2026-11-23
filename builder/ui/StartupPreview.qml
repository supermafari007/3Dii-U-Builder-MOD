import QtQuick

// Scaled view of the generated start screen; clicking its parent opens this at full size.
Item {
    id: root
    property string kind: "tv" // tv, pad, icon
    property bool enlarged: false
    signal activated(string kind)
    clip: true
    Rectangle { anchors.fill: parent; color: "#e8e8e8" }

    Image {
        anchors.fill: parent
        source: root.kind === "icon" ? (app.iconArtUrl || app.artUrl || app.icon)
              : root.kind === "pad" && app.padArtUrl ? app.padArtUrl
              : app.artStyle === "console" ? app.templateUrl : (app.artUrl || app.icon)
        fillMode: root.kind === "icon" ? Image.PreserveAspectCrop
                : root.kind === "pad" && app.padArtUrl ? Image.PreserveAspectCrop
                : app.artStyle === "console" ? Image.Stretch : Image.PreserveAspectCrop
        visible: source !== ""
        smooth: true
        mipmap: true
    }

    // Generated console-style screen: laid out at 1280x720 (matching make_game_title.py) and scaled along both axes
    // of root so the cover frame, logo, and text always stay locked to the template backdrop at any preview size.
    Item {
        width: 1280
        height: 720
        transform: Scale { xScale: root.width / 1280; yScale: root.height / 720 }
        visible: root.kind !== "icon" && !(root.kind === "pad" && app.padArtUrl) && app.artStyle === "console"
        Rectangle {
            x: 1280 * 0.072; y: 720 * 0.289; width: 1280 * 0.348; height: 720 * 0.505
            color: "#f2f4f7"; radius: 1280 * 0.006; clip: true
            Image { anchors.fill: parent; source: app.artUrl || app.icon; fillMode: app.artUrl ? Image.Stretch : Image.PreserveAspectFit; smooth: true; mipmap: true }
        }
        Image {
            x: 1280 * 0.455; y: 720 * 0.295; width: 1280 * 0.255; height: 720 * 0.068
            source: app.logoUrl; fillMode: Image.PreserveAspectFit; horizontalAlignment: Image.AlignLeft; verticalAlignment: Image.AlignTop
            smooth: true; mipmap: true
        }
        Text {
            x: 1280 * 0.455; y: 720 * 0.385; width: 1280 * 0.465; height: 720 * 0.115
            text: app.gameName; color: "#202124"; font.family: "Arial"; font.weight: Font.Medium
            font.pixelSize: 37; fontSizeMode: Text.VerticalFit; minimumPixelSize: 27
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
        Text {
            x: 1280 * 0.455; y: 720 * 0.525; width: 1280 * 0.465; height: 720 * 0.06
            text: app.maker; color: "#3b3d40"; font.family: "Arial"; font.pixelSize: 23
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
        Text {
            x: 1280 * 0.455; y: 720 * 0.600; width: 1280 * 0.465; height: 720 * 0.06
            text: app.releaseText; color: "#3b3d40"; font.family: "Arial"; font.pixelSize: 23
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#66000000"; border.width: 1; radius: root.kind === "icon" ? parent.width*.1 : 6 }
    MouseArea {
        anchors.fill: parent
        enabled: !root.enlarged
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated(root.kind)
        Accessible.role: Accessible.Button
        Accessible.name: root.kind === "tv" ? "Ampliar vista previa de TV" : root.kind === "pad" ? "Ampliar vista previa de GamePad" : "Ampliar icono de Wii U"
    }
}
