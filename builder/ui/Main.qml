// The 3Dii U Builder window. `app` (gui.py's App) holds what it shows and does what its buttons ask; `T` is the theme: fonts, and a light and a dark set of colours.
// The Wii U Menu's look: grey glass cards with a curved gloss, pill buttons, status lights. Everything is laid out on a 1280x720 surface scaled to the window,
// so the picture is the same at any size; questions come up inside the window.
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Effects

ApplicationWindow {
    id: win
    width: 1280
    height: 720
    minimumWidth: 880
    minimumHeight: 495
    visible: false  // gui.run() sizes it to its screen and shows it, so a check can load it unseen
    title: "3Dii-U Builder MOD " + app.version
    font.family: T.font
    font.letterSpacing: 0.4

    readonly property var pal: app.dark ? T.dark : T.light
    readonly property real s: Math.min(width / 1280, height / 720)
    // device pixels per design pixel, what layers render at to stay sharp: at most 2 (memory on old integrated GPUs), in half steps so a
    // resize reallocates them a few times, not at every pixel
    readonly property real px: Math.min(2, Math.ceil(s * Screen.devicePixelRatio * 2) / 2)
    readonly property bool blocked: dialogLayer.visible || cardMenu.visible || artMenu.visible || previewDialog.visible  // the shortcuts wait while a dialog, menu, or preview is open
    property string page: "build"
    readonly property bool restarting: app.updateState === "restarting"
    readonly property bool busy: app.building || restarting  // the game controls wait while a build runs or the new version starts
    property bool showLog: false
    property int openFaq: -1
    property string previewKind: "tv"
    function showPreview(kind) { previewKind = kind; previewDialog.visible = true; closePreviewPill.focusIn() }
    function hidePreview() { previewDialog.visible = false }
    property Item lastFocus: null
    onActiveFocusItemChanged: if (activeFocusItem && !blocked) lastFocus = activeFocusItem
    onBlockedChanged: if (!blocked) Qt.callLater(() => { if (lastFocus && lastFocus.visible && lastFocus.enabled) lastFocus.forceActiveFocus() })
    // the page's first control is one Tab away after a page change
    onPageChanged: Qt.callLater(() => (page === "build" ? buildStart : page === "games" ? gamesStart : helpStart).forceActiveFocus())

    color: pal.pageBottom
    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0; color: pal.pageTop }
            GradientStop { position: 1; color: pal.pageBottom }
        }
    }

    function mix(a, b, t) {
        a = Qt.color(a); b = Qt.color(b)
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1)
    }
    function stageTone(i) { return i < app.stage ? "green" : i === app.stage ? (app.failed ? "red" : "cyan") : "grey" }  // done, now, to come

    // ---------------------------------------------------------------- pieces
    // rounded glass: a base with a lighter top whose lower edge bulges down in the middle, and a soft shadow; `content` sits under the gloss.
    // A rim (the big tile) makes it a block of glass: a lighter edge all round, the face inset in it, the gloss's edge feathered over `feather` layers
    component GlassFace: Item {
        id: gf
        default property alias content: inner.data
        property real radius: 36
        property color base: pal.card
        property color gloss: pal.gloss
        property real glossOpacity: 1
        property real glossEdge: 85
        property real bulge: 42
        property bool shadow: true
        property real shadowOffset: 5
        property real rim: 0
        property color rimColor: pal.tileRim
        property color rimEdge: pal.tileEdge
        property int feather: 1
        readonly property real r: Math.max(0, radius - rim * 0.6)  // the face's own corner, inside the rim
        readonly property real fw: width - 2 * rim
        readonly property real edge: Math.min(glossEdge, height - radius)
        MultiEffect {
            source: outer
            anchors.fill: outer
            visible: gf.shadow
            shadowEnabled: true
            shadowColor: pal.shadow
            shadowBlur: gf.rim > 0 ? 1 : 0.6
            shadowVerticalOffset: gf.shadowOffset
            blurMax: gf.rim > 0 ? 36 : 20
        }
        Rectangle {
            id: outer
            anchors.fill: parent
            radius: gf.radius
            color: gf.base
            gradient: gf.rim > 0 ? rimFill : null
            Gradient {
                id: rimFill
                GradientStop { position: 0; color: Qt.lighter(gf.rimColor, 1.03) }
                GradientStop { position: 1; color: gf.rimColor }
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: gf.rim
            visible: gf.rim > 0
            radius: gf.r
            color: gf.base
            border.color: gf.rimEdge
            border.width: 1.5
        }
        Item { id: inner; anchors.fill: parent }
        Repeater {
            model: gf.feather
            Item {
                id: layer
                x: gf.rim; y: gf.rim
                width: gf.fw; height: gf.height - 2 * gf.rim
                // stacked layers whose edges step down a few pixels: together they reach glossOpacity, and their edge fades
                opacity: 1 - Math.pow(1 - gf.glossOpacity, 1 / gf.feather)
                // plain rounded rectangles, not a Shape: no texture to keep per card, and colours follow the theme at once
                // (Qt 6.12's Shape renderers left the gloss out or kept its old colour after some state changes)
                readonly property real e: gf.edge - gf.rim + index * 3  // where the curve meets the sides
                readonly property real b: Math.max(1, gf.bulge)
                readonly property real big: (width * width / 4 + b * b) / (2 * b)  // the circle whose arc is the curve: width wide, b deep
                Item {
                    width: layer.width; height: gf.r
                    clip: true
                    Rectangle { width: layer.width; height: 2 * gf.r + 1; radius: gf.r; color: gf.gloss }
                }
                Item {
                    y: gf.r
                    width: layer.width; height: layer.e + layer.b - gf.r
                    clip: true
                    Rectangle { x: layer.width / 2 - layer.big; y: layer.e + layer.b - gf.r - 2 * layer.big; width: 2 * layer.big; height: width; radius: layer.big; color: gf.gloss }
                }
            }
        }
    }

    // what makes a control pressable: the mouse, Tab focus, Return and Space, a cyan ring when it has focus, and a name for screen readers
    component Hit: MouseArea {
        id: h
        property string name
        property string description
        property real ringRadius: 14
        property int role: Accessible.Button
        property bool checked: false
        signal activated
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        activeFocusOnTab: enabled
        onClicked: h.activated()
        Keys.onReturnPressed: h.activated()
        Keys.onEnterPressed: h.activated()
        Keys.onSpacePressed: h.activated()
        Accessible.role: h.role
        Accessible.name: h.name
        Accessible.description: h.description
        Accessible.focusable: true
        Accessible.checkable: h.role === Accessible.RadioButton || h.role === Accessible.PageTab
        Accessible.checked: h.checked
        Accessible.onPressAction: h.activated()
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: h.ringRadius + 4
            color: "transparent"
            border.color: pal.lightCyan
            border.width: 3
            visible: h.activeFocus
        }
    }

    // a themed tooltip
    component Tip: ToolTip {
        id: tip
        delay: 500
        contentItem: Label { text: tip.text; color: pal.label; font.pixelSize: 14; wrapMode: Text.Wrap }
        background: Rectangle { radius: 10; color: pal.gloss; border.color: pal.card }
    }

    // a glossy pill: lighter upper half, darker lower edge; cyan for choosing, red for removing, grey for the rest
    component Pill: Item {
        id: p
        property string text
        property string tone: "grey"
        property int fontSize: 17
        property real letterSpacing: 0.3
        property string accessibleName: text
        property string tip
        readonly property bool hasFocus: ma.activeFocus
        function focusIn() { ma.forceActiveFocus() }
        objectName: "pill"  // test_window.py checks every pill has a name
        readonly property color base: tone === "cyan" ? pal.cyanBase : tone === "red" ? pal.redBase : pal.greyBase
        property color ink: tone === "cyan" ? pal.cyanText : tone === "red" ? pal.redText : pal.greyText
        readonly property color fill: ma.pressed ? Qt.darker(base, 1.06) : ma.containsMouse ? Qt.lighter(base, 1.05) : base
        signal clicked
        implicitWidth: Math.max(84, ink_.implicitWidth + 24)
        implicitHeight: 36
        opacity: enabled || win.blocked ? 1 : 0.45  // under a dialog or a menu the page is out of reach but keeps its look
        Item {
            anchors.fill: parent
            anchors.topMargin: ma.pressed ? 1 : 0
            anchors.bottomMargin: ma.pressed ? -1 : 0
            Rectangle { anchors.fill: parent; radius: height / 2; color: Qt.darker(p.fill, 1.14) }
            Rectangle { anchors.fill: parent; anchors.bottomMargin: Math.max(2, p.height * 0.05); radius: height / 2; color: p.fill }
            Rectangle {
                x: p.height * 0.18; y: p.height * 0.08
                width: parent.width - 2 * x; height: p.height * 0.4
                radius: height / 2
                color: Qt.lighter(p.fill, 1.1)
            }
            Label { id: ink_; anchors.centerIn: parent; anchors.verticalCenterOffset: -1; text: p.text; color: p.ink; font.pixelSize: p.fontSize; font.letterSpacing: p.letterSpacing }
        }
        Hit { id: ma; name: p.accessibleName; description: p.tip; ringRadius: p.height / 2; onActivated: p.clicked() }
        Tip { visible: ma.containsMouse && p.tip.length > 0; text: p.tip }
    }

    // a line of the Art pill's menu: what it does, and on the right what's chosen now
    component ArtEntry: Rectangle {
        id: e
        property string label
        property string chosen
        property bool danger: false
        signal picked
        function focusIn() { entryHit.forceActiveFocus() }
        width: 452; height: 42; radius: 21
        color: entryHover.hovered ? pal.gloss : "transparent"
        HoverHandler { id: entryHover }
        Label {
            x: 18; anchors.verticalCenter: parent.verticalCenter
            width: e.chosen.length > 0 ? parent.width - chosenLabel.width - 44 : parent.width - 36
            elide: Text.ElideRight
            text: e.label; color: e.danger ? pal.redText : pal.label; font.pixelSize: 16
        }
        Label {
            id: chosenLabel
            anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
            width: e.chosen.length > 0 ? 168 : 0; horizontalAlignment: Text.AlignRight; elide: Text.ElideMiddle
            text: e.chosen; color: pal.subLabel; font.pixelSize: 14
        }
        Hit { id: entryHit; ringRadius: 21; name: e.label; description: e.chosen; role: Accessible.MenuItem; onActivated: e.picked() }
    }

    // a glossy status light in a grey socket ring: red = none or missing, cyan = chosen or found, grey = optional and absent, green = a build's step done
    component Light: Item {
        id: l
        property string tone: "grey"
        property bool pulse: false
        readonly property color hue: tone === "red" ? pal.lightRed : tone === "cyan" ? pal.lightCyan : tone === "green" ? pal.lightGreen : pal.lightGrey
        width: 26; height: 26
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.darker(pal.socket, 1.1) }
                GradientStop { position: 1; color: Qt.lighter(pal.socket, 1.15) }
            }
        }
        Rectangle {
            id: ball
            x: l.width * 0.1; y: l.height * 0.135; width: l.width * 0.8; height: width
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(l.hue, 1.1) }
                GradientStop { position: 1; color: Qt.darker(l.hue, 1.15) }
            }
            Rectangle { x: parent.width * 0.21; y: parent.height * 0.07; width: parent.width * 0.57; height: parent.height * 0.33; radius: height / 2; color: Qt.lighter(l.hue, 1.45); opacity: 0.7 }
        }
        SequentialAnimation {
            running: l.pulse
            loops: Animation.Infinite
            onStopped: ball.opacity = 1
            NumberAnimation { target: ball; property: "opacity"; to: 0.35; duration: 600; easing.type: Easing.InOutSine }
            NumberAnimation { target: ball; property: "opacity"; to: 1; duration: 600; easing.type: Easing.InOutSine }
        }
    }

    // a card: title, a two-line subtitle, then rows
    component Card: Item {
        id: card
        property string title
        property string subtitle
        property color titleColor: pal.title
        default property alias content: rows.data
        width: 560
        height: rows.y + rows.implicitHeight + 18
        GlassFace { anchors.fill: parent }
        Label { x: 24; y: 10; text: card.title; color: card.titleColor; font.pixelSize: 25; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
        Label {
            id: sub
            x: 25; y: 44
            width: card.width - 50
            text: card.subtitle
            textFormat: Text.PlainText  // a release's title comes from GitHub: no markup, so nothing is fetched from elsewhere
            visible: text.length > 0
            color: pal.sub
            font.pixelSize: 15
            lineHeightMode: Text.ProportionalHeight
            lineHeight: 1.18
            wrapMode: Text.WordWrap
        }
        Column { id: rows; y: sub.visible ? Math.max(76, sub.y + sub.implicitHeight + 10) : 50; width: card.width; spacing: 2 }
    }

    // a row in a card: light, label, sub-label, pills on the right
    component CardRow: Item {
        id: row
        property string label
        property string sub
        property string light: "grey"
        property bool pulse: false
        property bool tappable: false
        property int role: Accessible.Button
        property bool checked: false
        property string description
        signal tapped
        default property alias pills: pillRow.data
        width: parent ? parent.width : 560
        height: Math.max(54, subLab.visible ? subLab.y + subLab.implicitHeight + 8 : 50)
        Hit { enabled: row.tappable; visible: row.tappable; anchors.margins: 6; name: row.label; description: row.description; role: row.role; checked: row.checked; onActivated: row.tapped() }
        Light { x: 24; y: (parent.height - height) / 2; tone: row.light; pulse: row.pulse }
        Label {
            id: lab
            x: 60
            y: subLab.visible ? 7 : (parent.height - implicitHeight) / 2
            width: Math.max(110, pillRow.x - 72)
            text: row.label; color: pal.label; font.pixelSize: 17; font.weight: Font.Medium; elide: Text.ElideRight
        }
        Label {
            id: subLab
            x: 60; y: 29
            width: lab.width
            visible: text.length > 0
            text: row.sub; color: pal.subLabel; font.pixelSize: 14
            wrapMode: row.width > 700 ? Text.WordWrap : Text.NoWrap
            maximumLineCount: row.width > 700 ? 2 : 1
            elide: Text.ElideRight
        }
        Row { id: pillRow; anchors.right: parent.right; anchors.rightMargin: 20; y: (parent.height - height) / 2; spacing: 8 }
    }

    // a row whose sub-label wraps: compatibility notes, answers
    component NoteRow: Item {
        id: nr
        property string label
        property string sub
        property string light: "grey"
        width: parent ? parent.width : 560
        height: Math.max(52, col.implicitHeight + 14)
        Light { x: 24; y: 14; tone: nr.light }
        Column {
            id: col
            x: 60; y: 6
            width: nr.width - 60 - 24
            spacing: 2
            Label { width: parent.width; text: nr.label; color: pal.label; font.pixelSize: 17; font.weight: Font.Medium; wrapMode: Text.WordWrap }
            Label { width: parent.width; text: nr.sub; color: pal.subLabel; font.pixelSize: 14; wrapMode: Text.WordWrap; visible: text.length > 0 }
        }
    }

    // a rail key-cap seen from above: a white face on grey sides, and its darker lower side showing under it
    component KeyCap: Item {
        id: kc
        default property alias glyph: capFace.data
        property real inset: 10
        Rectangle { anchors.fill: parent; radius: kc.inset * 1.4; color: pal.keycapSide }
        Rectangle {
            width: kc.width; height: kc.height - 0.8 * kc.inset
            radius: kc.inset * 1.4
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(pal.keycapBase, 1.04) }
                GradientStop { position: 1; color: Qt.darker(pal.keycapBase, 1.06) }
            }
        }
        Rectangle { x: capFace.x; y: capFace.y + 2; width: capFace.width; height: capFace.height; radius: capFace.radius; color: pal.keycapSide; opacity: 0.6 }
        Rectangle {
            id: capFace
            x: kc.inset; y: kc.inset
            width: kc.width - 2 * kc.inset; height: kc.height - 2.8 * kc.inset
            radius: kc.inset
            gradient: Gradient {
                GradientStop { position: 0; color: pal.keycap }
                GradientStop { position: 1; color: Qt.darker(pal.keycap, 1.04) }
            }
        }
    }

    // a word in two tones, lighter above the middle of its letters and darker below
    component TwoTone: Item {
        id: tt
        property string text
        property color color
        property real size: 74
        implicitWidth: upper.implicitWidth
        implicitHeight: upper.implicitHeight
        readonly property real split: upper.baselineOffset - size * 0.34
        Label { id: upper; text: tt.text; color: tt.color; font.family: T.display; font.pixelSize: tt.size; font.weight: Font.DemiBold; font.letterSpacing: 0.5 }
        Item {
            y: tt.split
            width: tt.width; height: tt.height - tt.split
            clip: true
            Label { y: -tt.split; text: tt.text; color: Qt.darker(tt.color, 1.22); font: upper.font }
        }
    }

    component ProgressPill: Rectangle {
        property real value: 0
        height: 18
        radius: 9
        color: pal.greyBase
        Rectangle {
            width: Math.max(parent.height, parent.width * Math.min(1, parent.value))
            height: parent.height
            radius: parent.radius
            color: pal.lightCyan
            visible: parent.value > 0
            Rectangle { x: 5; y: 2; width: parent.width - 10; height: 6; radius: 3; color: Qt.lighter(pal.lightCyan, 1.3); opacity: 0.6 }
        }
    }

    component LogPanel: Rectangle {
        width: parent ? parent.width - 48 : 500
        x: 24
        height: 220
        radius: 18
        color: pal.logBg
        ScrollView {
            anchors.fill: parent
            anchors.margins: 10
            ScrollBar.vertical: Thumb { }
            ScrollBar.horizontal: Thumb { }
            TextArea {
                selectionColor: pal.cyanBase
                selectedTextColor: pal.buildText
                readOnly: true
                text: app.log
                color: pal.logText
                font.family: T.mono
                font.pixelSize: 13
                wrapMode: TextEdit.NoWrap
                background: null
                selectByMouse: true
                onTextChanged: cursorPosition = length
            }
        }
    }

    component Thumb: ScrollBar {
        id: sb
        contentItem: Rectangle {
            implicitWidth: 8; implicitHeight: 8; radius: 4; color: pal.sub
            opacity: sb.size < 1 && (sb.policy === ScrollBar.AlwaysOn || sb.active) ? 0.6 : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
        background: null
    }

    // ---------------------------------------------------------------- the 1280x720 surface
    Item {
        id: surface
        width: 1280
        height: 720
        // placed from the window's own size: a hidden window (a check's screenshot) never sizes its content item
        x: (win.width - width) / 2
        y: (win.height - height) / 2
        scale: win.s

        Item {
        id: pages
        objectName: "pages"  // test_window.py checks it is out of reach under the menus
        anchors.fill: parent
        enabled: !win.blocked  // Tab stays in the dialog or the menu

        // ---- the rail: logo, Build, Games, Help
        // the rail is rounded on its right: each block that meets a rounded corner is a rounded rectangle with its other corner squared off
        Rectangle { x: -60; y: -2; width: 250; height: 724; radius: 60; color: pal.rail }
        Rectangle { x: -60; y: -2; width: 250; height: 168; radius: 60; color: pal.railTop }
        Rectangle { y: 100; width: 190; height: 66; color: pal.railTop }
        Rectangle { y: 170; width: 190; height: 186; color: pal.railActive; visible: win.page === "build" }
        Rectangle { y: 360; width: 190; height: 182; color: pal.railActive; visible: win.page === "games" }
        Rectangle { y: 166; width: 190; height: 4; color: pal.railLine }
        Rectangle { y: 356; width: 190; height: 4; color: pal.railLine }
        Rectangle { x: -60; y: 542; width: 250; height: 180; radius: 60; color: win.page === "help" ? pal.helpActive : pal.help }
        Rectangle { y: 542; width: 190; height: 70; color: win.page === "help" ? pal.helpActive : pal.help }
        Rectangle { x: -60; y: -2; width: 250; height: 724; radius: 60; color: "transparent"; border.color: pal.railEdge; border.width: 2 }

        Image { x: 14; y: 4; width: 162; height: 162; source: appIcon; fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true; Accessible.ignored: true }

        Item {
            y: 170; width: 190; height: 186
            KeyCap {
                x: 42; y: 22; width: 100; height: 108
                Rectangle { anchors.centerIn: parent; width: 60; height: 12; radius: 3; color: pal.keycapGlyph }
                Rectangle { anchors.centerIn: parent; width: 12; height: 60; radius: 3; color: pal.keycapGlyph }
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; y: 136; text: "Compilar"; color: pal.railText; font.pixelSize: 30 }
            Hit { anchors.margins: 10; name: "Página Compilar"; role: Accessible.PageTab; checked: win.page === "build"; onActivated: win.page = "build" }
        }
        Item {
            y: 360; width: 190; height: 182
            Repeater {
                model: 4
                KeyCap { x: 42 + (index % 2) * 53; y: 22 + Math.floor(index / 2) * 53; width: 47; height: 52; inset: 5 }
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; y: 136; text: "Juegos"; color: pal.railText; font.pixelSize: 30 }
            Hit { anchors.margins: 10; name: "Página Juegos"; role: Accessible.PageTab; checked: win.page === "games"; onActivated: win.page = "games" }
        }
        Item {
            y: 542; width: 190; height: 178
            Rectangle {
                x: 42; y: 20; width: 104; height: 104; radius: 52; color: pal.helpRing
                Rectangle {
                    anchors.centerIn: parent; anchors.verticalCenterOffset: -3
                    width: 84; height: 84; radius: 42; color: pal.helpFace
                    Label { anchors.centerIn: parent; text: "?"; color: pal.helpRing; font.pixelSize: 62; font.weight: Font.Bold }
                }
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; y: 130; text: "Ayuda"; color: pal.helpText; font.pixelSize: 28; font.weight: Font.DemiBold }
            Hit { anchors.margins: 10; name: "Página Ayuda"; role: Accessible.PageTab; checked: win.page === "help"; onActivated: win.page = "help" }
        }

        // ---- the title row: 3Dii-U Builder MOD, a 3DS, dots, a GamePad
        Row {
            x: 212; y: 10
            TwoTone { text: "3D"; size: 54; color: pal.red }
            TwoTone { text: "ii-U"; size: 54; color: pal.cyan }
            Item { width: 14; height: 1 }
            TwoTone { text: "Builder"; size: 54; color: pal.builder }
            Item { width: 14; height: 1 }
            TwoTone { text: "MOD"; size: 40; y: 10; color: pal.cyan }
        }
        Rectangle {
            x: 800; y: 8; width: 62; height: 84; radius: 5
            color: pal.ds
            border.color: Qt.darker(pal.ds, 1.08); border.width: 2
            Rectangle { x: 8; y: 7; width: 46; height: 32; radius: 2; color: pal.screen }
            Rectangle { x: 8; y: 46; width: 46; height: 31; radius: 2; color: pal.screen }
        }
        Repeater {
            model: 7
            Rectangle {
                readonly property color hue: index < 3 ? win.mix(pal.dotFrom, pal.dotMid, index / 3) : win.mix(pal.dotMid, pal.dotTo, (index - 3) / 3)
                x: 874 + index * 30; y: 48; width: 18; height: 18; radius: 9
                color: hue
                Rectangle { x: 4; y: 2; width: 10; height: 6; radius: 3; color: Qt.lighter(parent.hue, 1.15) }
            }
        }
        Rectangle {
            x: 1090; y: 8; width: 142; height: 87; radius: 24
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(pal.pad, 1.06) }
                GradientStop { position: 1; color: Qt.darker(pal.pad, 1.04) }
            }
            Rectangle { x: 28; y: 12; width: 86; height: 60; radius: 2; color: pal.screen }
            Rectangle { x: 6; y: 10; width: 16; height: 16; radius: 8; color: pal.screen }
            Rectangle { x: 120; y: 10; width: 16; height: 16; radius: 8; color: pal.screen }
            Rectangle { x: 9; y: 52; width: 12; height: 4; color: pal.screen }
            Rectangle { x: 13; y: 48; width: 4; height: 12; color: pal.screen }
            Repeater {
                model: [[124, 48], [130, 54], [124, 60], [118, 54]]
                Rectangle { x: modelData[0]; y: modelData[1]; width: 5; height: 5; radius: 2.5; color: pal.screen }
            }
            Rectangle { x: 67; y: 76; width: 8; height: 8; radius: 4; color: pal.screen }
        }

        // ---- the Build page
        Item {
            id: buildPage
            anchors.fill: parent
            Item { id: buildStart }
            visible: win.page === "build"
            readonly property string art: app.tileUrl || app.icon

            Rectangle {
                x: 282; y: 410; width: 304; height: 48; radius: 56
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.alpha(pal.tileBase, 0.25) }
                    GradientStop { position: 1; color: Qt.alpha(pal.tileBase, 0) }
                }
            }
            GlassFace {
                id: tile
                x: 282; y: 98; width: 304; height: 304
                radius: 56
                base: pal.tileBase
                gloss: pal.tileGloss
                glossOpacity: 0.3
                glossEdge: 185
                bulge: 42
                rim: 11
                feather: 3
                shadowOffset: 4
                Hit { enabled: !win.busy; ringRadius: 68; name: app.hasGame ? "Cambiar juego: " + app.gameName : "Elegir un juego"; onActivated: app.chooseGame() }
                Item {
                    anchors.fill: parent
                    visible: !app.hasGame || buildPage.art === ""
                    Rectangle { anchors.centerIn: parent; anchors.horizontalCenterOffset: 0; width: 204; height: 42; radius: 21; color: pal.plus }
                    Rectangle { anchors.centerIn: parent; width: 42; height: 204; radius: 21; color: pal.plus }
                }
                Image {
                    id: artImg
                    anchors.fill: parent
                    anchors.margins: 13
                    source: app.hasGame ? buildPage.art : ""
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    mipmap: true
                    visible: false
                    layer.enabled: true
                    layer.textureSize: Qt.size(width * win.px, height * win.px)
                }
                Rectangle { id: artMask; anchors.fill: artImg; radius: 46; visible: false; layer.enabled: true; layer.textureSize: Qt.size(width * win.px, height * win.px) }
                MultiEffect { source: artImg; anchors.fill: artImg; maskEnabled: true; maskSource: artMask; visible: app.hasGame && buildPage.art !== "" }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 16
                    spacing: 8
                    visible: app.hasGame && !app.building
                    enabled: !win.restarting
                    Pill { text: "Cambiar juego"; fontSize: 16; implicitHeight: 34; onClicked: app.chooseGame() }
                    Pill { id: artPill; text: "Arte"; fontSize: 16; implicitHeight: 34; accessibleName: "Imagen e icono"; tip: app.artText; onClicked: artMenu.openAt(artPill) }
                }
                ProgressPill {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 30
                    width: 256
                    value: app.progress
                    visible: app.building
                }
            }

            // under the tile: what to pick, or the game picked
            Item {
                x: 224; y: 422; width: 420; height: 180
                visible: !app.hasGame
                Label { id: choose; anchors.horizontalCenter: parent.horizontalCenter; y: 6; text: "Elegir un juego"; color: pal.title; font.pixelSize: 40; font.letterSpacing: 1.0 }
                Item {
                    x: choose.x; y: choose.y + choose.baselineOffset + 4
                    width: choose.width; height: 20
                    clip: true
                    Label {
                        y: -choose.baselineOffset - 4 + 20 - 8
                        text: choose.text; color: pal.title; font: choose.font; opacity: 0.1
                        transform: Scale { yScale: -1; origin.y: choose.baselineOffset }
                    }
                }
                Label { anchors.horizontalCenter: parent.horizontalCenter; y: 60; text: ".3ds    .cci    .cia    .cxi"; color: pal.sub; font.pixelSize: 19 }
                Label { anchors.horizontalCenter: parent.horizontalCenter; y: 92; text: "cifrado o descifrado"; color: pal.sub; font.pixelSize: 18 }
                Label { anchors.horizontalCenter: parent.horizontalCenter; y: 118; text: "Ctrl + O"; color: pal.sub; font.pixelSize: 17 }
            }
            Column {
                x: 218; y: 414; width: 432
                visible: app.hasGame
                spacing: 4
                Label {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: app.gameName; color: pal.title; font.pixelSize: 27; font.weight: Font.DemiBold
                    fontSizeMode: Text.HorizontalFit; minimumPixelSize: 18; elide: Text.ElideRight
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    Light { tone: app.known ? "cyan" : "grey"; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter }
                    Label { text: app.badge + (app.maker ? "  ·  " + app.maker : ""); color: pal.label; font.pixelSize: 15 }
                }
                Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: app.meta; color: pal.sub; font.pixelSize: 14; elide: Text.ElideMiddle }
                Pill { anchors.horizontalCenter: parent.horizontalCenter; text: "Editar información"; fontSize: 15; implicitHeight: 30; visible: app.hasGame; enabled: !win.busy; onClicked: app.editMetadata() }
                Label {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: app.building ? app.detail : app.note
                    color: pal.sub; font.pixelSize: 14; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                    HoverHandler { id: noteHover }
                    Tip { visible: noteHover.hovered && parent.truncated; text: parent.text; width: 420 }
                }
            }

            Pill {
                x: 230; y: 622; width: 408; height: 68
                text: "¡Compilar!"
                fontSize: 34
                letterSpacing: 1.0
                ink: pal.buildText
                enabled: app.view === "form" && !win.restarting  // build() says what is missing; after a build its own card has the buttons
                onClicked: app.hasGame ? app.build() : app.chooseGame()
            }

            // the right column: the choices, or the build
            Flickable {
                id: column
                x: 660; y: 100; width: 610; height: 620
                contentHeight: rightCol.height + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: Thumb { }
                Column {
                    id: rightCol
                    x: 20; y: 12
                    spacing: 20

                    Card {
                        visible: app.view === "form" && app.hasGame
                        title: "Vista previa"
                        subtitle: "Pantallas de inicio y menú de Wii U · Haz clic en cualquiera para ampliar"
                        Row {
                            x: 24; width: 520; height: 136; spacing: 18
                            Column {
                                width: 190; spacing: 6
                                Label { text: "TV · 1280 × 720"; color: pal.label; font.pixelSize: 14 }
                                StartupPreview { width: 184; height: 104; kind: "tv"; onActivated: win.showPreview(kind) }
                            }
                            Column {
                                width: 156; spacing: 6
                                Label { text: "GamePad · 854 × 480"; color: pal.label; font.pixelSize: 14 }
                                StartupPreview { width: 148; height: 83; kind: "pad"; onActivated: win.showPreview(kind) }
                            }
                            Column {
                                width: 100; spacing: 6
                                Label { text: "Icono Wii U"; color: pal.label; font.pixelSize: 14 }
                                StartupPreview { width: 80; height: 80; kind: "icon"; onActivated: win.showPreview(kind) }
                            }
                        }
                    }

                    // a newer release, from its offer to its being ready (gui.py's updater part)
                    Card {
                        id: updateCard
                        readonly property string st: app.updateState
                        visible: ["available", "downloading", "unpacking", "ready", "restarting", "failed"].indexOf(st) >= 0
                        title: st === "failed" ? "La actualización se detuvo" : "Hay una nueva versión disponible"
                        titleColor: st === "failed" ? pal.redText : pal.title
                        subtitle: app.updateTitle
                        Column {
                            x: 25; width: 510
                            spacing: 8
                            Label {
                                width: parent.width
                                visible: text.length > 0
                                text: updateCard.st === "available" ? app.updateNotes
                                    : updateCard.st === "downloading" ? "Descargando... " + app.updateProgress + "%"
                                    : updateCard.st === "unpacking" ? "Descomprimiendo..."
                                    : updateCard.st === "ready" ? "Lista. Se abrirá la próxima vez que inicies el programa."
                                    : app.updateMessage
                                textFormat: Text.PlainText  // release notes are Markdown from GitHub: shown as text, so no picture is fetched
                                color: pal.label; font.pixelSize: 16; wrapMode: Text.WordWrap
                                maximumLineCount: updateCard.st === "available" ? 3 : 8
                                elide: Text.ElideRight
                            }
                            ProgressPill { width: parent.width; value: app.updateProgress / 100; visible: updateCard.st === "downloading" || updateCard.st === "unpacking" }
                        }
                        Item {
                            width: 560; height: 56
                            visible: updateCard.st !== "unpacking" && updateCard.st !== "restarting"
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: 21; y: 10
                                spacing: 8
                                Pill { text: "Más tarde"; accessibleName: "Actualizar más tarde"; visible: updateCard.st !== "downloading"; onClicked: app.laterUpdate() }
                                Pill { text: "Cancelar"; accessibleName: "Cancelar la descarga"; tone: "red"; visible: updateCard.st === "downloading"; onClicked: app.cancelUpdate() }
                                Pill { text: "Actualizar"; accessibleName: "Descargar la actualización"; tone: "cyan"; visible: updateCard.st === "available"; onClicked: app.startUpdate() }
                                Pill { text: "Reiniciar ahora"; accessibleName: "Reiniciar con la nueva versión"; tone: "cyan"; visible: updateCard.st === "ready"; enabled: !app.building; onClicked: app.restartForUpdate() }
                                Pill { text: "Volver a intentar"; accessibleName: "Volver a intentar la actualización"; tone: "cyan"; visible: updateCard.st === "failed"; onClicked: app.startUpdate() }
                            }
                        }
                    }

                    Card {
                        visible: app.view === "form"
                        enabled: !win.restarting
                        title: "Opciones adicionales"
                        subtitle: "Permite integrar una actualización, DLC, mods o una partida guardada al compilar."
                        CardRow {
                            label: "Actualización"; sub: app.hasUpdate ? app.updateText : "Ninguna seleccionada"; light: app.hasUpdate ? "cyan" : "red"
                            Pill { text: "Seleccionar"; tone: "cyan"; onClicked: app.hasGame ? app.chooseExtra("update") : app.chooseGame() }
                            Pill { text: "Quitar"; tone: "red"; onClicked: app.removeExtra("update") }
                        }
                        CardRow {
                            label: "DLC"; sub: app.hasDlc ? app.dlcText : "Ninguno seleccionado"; light: app.hasDlc ? "cyan" : "red"
                            Pill { text: "Seleccionar"; tone: "cyan"; onClicked: app.hasGame ? app.chooseExtra("dlc") : app.chooseGame() }
                            Pill { text: "Quitar"; tone: "red"; onClicked: app.removeExtra("dlc") }
                        }
                        CardRow {
                            label: "Mod"; sub: app.hasMod ? app.modText : "Ninguno seleccionado"; light: app.hasMod ? "cyan" : "red"
                            Pill { text: ".zip"; onClicked: app.hasGame ? app.chooseMod("zip") : app.chooseGame() }
                            Pill { text: "Carpeta"; tone: "cyan"; onClicked: app.hasGame ? app.chooseMod("folder") : app.chooseGame() }
                            Pill { text: "Quitar"; tone: "red"; onClicked: app.removeExtra("mod") }
                        }
                        CardRow {
                            visible: app.isMk7
                            label: "Online Pretendo 3DS (MK7)"
                            sub: app.mk7Online ? "Activo: redirige MK7 a Pretendo 3DS (sin tocar Inkay) y genera un Friend Code único en cada compilación."
                                               : "Desactivado: modo offline estándar."
                            light: app.mk7Online ? "cyan" : "red"
                            Pill { text: "Activado"; tone: app.mk7Online ? "cyan" : "grey"; onClicked: app.setMk7Online(true) }
                            Pill { text: "Desactivado"; tone: !app.mk7Online ? "cyan" : "grey"; onClicked: app.setMk7Online(false) }
                        }
                        CardRow {
                            label: "Partida de 3DS"; sub: app.saveText; light: app.hasSave ? "cyan" : "grey"
                            Pill { text: "Seleccionar"; accessibleName: "Seleccionar una partida de 3DS para incluirla"; tone: "cyan"; onClicked: app.hasGame ? app.bringSave() : app.chooseGame() }
                            Pill { text: "Quitar"; accessibleName: "Quitar la partida de 3DS"; tone: "red"; onClicked: app.removeSave() }
                        }
                    }

                    Card {
                        visible: app.view === "form"
                        enabled: !win.restarting
                        title: "Archivos de consola"
                        subtitle: "Archivos propios de tu 3DS y Wii U. Permanecen en tu PC y nunca se comparten."
                        CardRow {
                            label: "Claves de 3DS"; sub: app.keysText; light: app.keysOk ? "cyan" : app.keysNeeded ? "red" : "grey"
                            Pill { text: "Info"; onClicked: app.explain("keys") }
                            Pill { text: "Carpeta"; tone: "cyan"; onClicked: app.chooseKeys() }
                        }
                        CardRow {
                            label: "otp.bin de Wii U"; sub: app.otpText; light: app.otpOk ? "cyan" : "red"
                            Pill { text: "Info"; onClicked: app.explain("otp") }
                            Pill { text: "Archivo"; tone: "cyan"; onClicked: app.chooseOtp() }
                        }
                        CardRow {
                            label: "Archivos del sistema 3DS"; sub: app.sysText; light: app.sysOk ? "cyan" : "red"
                            Pill { text: "Info"; onClicked: app.explain("system") }
                            Pill { text: "Carpeta"; tone: "cyan"; onClicked: app.chooseSystem() }
                        }
                    }

                    Card {
                        visible: app.view === "form"
                        enabled: !win.restarting
                        title: "Destino"
                        subtitle: "Elige dónde guardar el paquete WUP listo para instalar: en tu tarjeta SD o en una carpeta."
                        CardRow {
                            label: "Tarjeta SD"; sub: app.target === "card" ? app.destText : "Guardar directamente en la tarjeta SD de la Wii U"
                            light: app.target === "card" ? "cyan" : "grey"
                            tappable: true; role: Accessible.RadioButton; checked: app.target === "card"; onTapped: app.setTarget("card")
                            Pill { text: "Actualizar"; accessibleName: "Volver a buscar tarjetas SD"; onClicked: app.refreshCards() }
                            Pill { id: cardsPill; text: "Seleccionar"; accessibleName: "Seleccionar una tarjeta SD"; tone: "cyan"; onClicked: cardMenu.openAt(cardsPill) }
                        }
                        CardRow {
                            label: "Carpeta en PC"; sub: app.target === "folder" ? app.destText : "Guardar en una carpeta de este equipo"
                            light: app.target === "folder" ? "cyan" : "grey"
                            tappable: true; role: Accessible.RadioButton; checked: app.target === "folder"; onTapped: app.setTarget("folder")
                            Pill { text: "Carpeta"; tone: "cyan"; onClicked: app.chooseFolder() }
                        }
                    }

                    Card {
                        visible: app.view === "building" || app.view === "failed"
                        title: app.view === "failed" ? "La compilación se detuvo" : "Compilando"
                        subtitle: app.view === "failed" ? app.resultText : app.gameName
                        titleColor: app.view === "failed" ? pal.redText : pal.title
                        Repeater {
                            model: app.stages
                            CardRow { label: modelData; light: win.stageTone(index); pulse: index === app.stage && app.building }
                        }
                        Item {
                            width: 560; height: 44
                            visible: app.view === "building"
                            ProgressPill { x: 26; y: 6; width: 448; value: app.progress }
                            Label { x: 482; y: 3; width: 52; text: Math.round(app.progress * 100) + "%"; horizontalAlignment: Text.AlignRight; color: pal.label; font.pixelSize: 17 }
                            Label { x: 26; y: 26; width: 508; text: app.detail; color: pal.subLabel; font.pixelSize: 15; elide: Text.ElideRight }
                        }
                        Item {
                            width: 560; height: 58
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: 21; y: 8
                                spacing: 8
                                Pill { text: win.showLog ? "Ocultar detalles" : "Detalles"; onClicked: win.showLog = !win.showLog }
                                Pill { text: "Cancelar"; tone: "red"; visible: app.view === "building"; enabled: app.building; onClicked: app.cancel() }
                                Pill { text: "Volver"; tone: "cyan"; visible: app.view === "failed"; onClicked: app.back() }
                            }
                        }
                        LogPanel { visible: win.showLog }
                    }

                    Card {
                        visible: app.view === "done"
                        title: "¡Listo para instalar!"
                        subtitle: app.resultText
                        Item {
                            width: 560; height: 58
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: 21; y: 8
                                spacing: 8
                                Pill { text: "Abrir carpeta"; tone: "cyan"; onClicked: app.openFolder() }
                                Pill { text: "Compilar otro"; onClicked: app.back() }
                            }
                        }
                    }
                }
            }
        }

        // ---- the Games page: your library, then what plays where
        Flickable {
            id: gamesPage
            Item { id: gamesStart }
            x: 190; y: 100; width: 1090; height: 620
            visible: win.page === "games"
            contentHeight: gamesCol.height + 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: Thumb { }
            Column {
                id: gamesCol
                x: 20; y: 12
                spacing: 34
                Card {
                    width: 1030
                    title: "Tus juegos"
                    subtitle: app.library.length ? "Los juegos que seleccionaste anteriormente. Selecciona uno para abrirlo en la página Compilar." : "Todavía no hay juegos: los que selecciones en Compilar aparecerán aquí."
                    Flow {
                        x: 24; width: 1030 - 48
                        spacing: 14
                        bottomPadding: 6
                        Repeater {
                            model: app.library
                            FocusScope {
                                id: libTile
                                width: 140; height: 184
                                HoverHandler { id: libHover }
                                // hover, focus or being the open game shows Remove, so touch screens and keyboards reach it too
                                readonly property bool showRemove: (libHover.hovered || libTile.activeFocus || modelData.current) && !win.busy
                                KeyCap {
                                    x: 10; y: 4; width: 120; height: 130; inset: 8
                                    Image {
                                        anchors.fill: parent; anchors.margins: 4
                                        source: modelData.cover || modelData.icon
                                        fillMode: Image.PreserveAspectCrop
                                        smooth: true; mipmap: true
                                    }
                                }
                                Rectangle { x: 10; y: 4; width: 120; height: 130; radius: 11; color: "transparent"; border.color: pal.lightCyan; border.width: 3; visible: modelData.current }
                                Label {
                                    y: 138; width: 140
                                    text: modelData.name; color: pal.label; font.pixelSize: 15
                                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                }
                                Hit {
                                    id: libHit
                                    enabled: !win.busy
                                    name: "Open " + modelData.name
                                    onActivated: { app.openRecent(modelData.path); win.page = "build" }
                                }
                                Pill {
                                    id: forget
                                    x: 66; y: 0; width: 72; height: 30; fontSize: 15
                                    text: "Quitar"; accessibleName: "Quitar " + modelData.name + " de tus juegos"; tone: "red"
                                    opacity: libTile.showRemove ? 1 : 0
                                    enabled: libTile.showRemove  // a hidden one takes no click; focus on the tile shows it, so Tab reaches it next
                                    onClicked: app.forgetGame(modelData.path)
                                }
                            }
                        }
                    }
                }
                Repeater {
                    model: [
                        { title: "Completados", sub: "Confirmados como terminados en una Wii U real.", list: app.games.filter(g => g.status === "Complete"), light: "cyan" },
                        { title: "Jugables", sub: "Probados en una Wii U real, sin sus actualizaciones.", list: app.games.filter(g => g.status === "Plays"), light: "grey" },
                        { title: "Funcionan en Cemu", sub: "Compilados con Builder y ejecutados en el emulador Cemu, todavía no en una Wii U real.", list: app.others, light: "grey" }
                    ]
                    Card {
                        id: tier
                        readonly property string tierLight: modelData.light
                        width: 1030
                        visible: modelData.list.length > 0
                        title: modelData.title
                        titleColor: modelData.light === "cyan" ? pal.cyanText : pal.title
                        subtitle: modelData.sub
                        Repeater {
                            model: modelData.list
                            NoteRow { label: modelData.name; sub: modelData.note; light: tier.tierLight }
                        }
                    }
                }
            }
        }

        // ---- the Help page: questions and answers, about, the theme
        Flickable {
            id: helpPage
            Item { id: helpStart }
            x: 190; y: 100; width: 1090; height: 620
            visible: win.page === "help"
            contentHeight: helpCol.height + 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: Thumb { }
            Column {
                id: helpCol
                x: 20; y: 12
                spacing: 34
                Card {
                    width: 1030
                    title: "Preguntas y respuestas"
                    subtitle: app.faq.length ? "Selecciona una pregunta para ver su respuesta." : "Esta copia de Builder no incluye preguntas y respuestas."
                    Repeater {
                        model: app.faq
                        Column {
                            width: 1030
                            CardRow {
                                label: modelData.q
                                light: win.openFaq === index ? "cyan" : "grey"
                                tappable: true
                                description: win.openFaq === index ? modelData.a : ""
                                onTapped: win.openFaq = win.openFaq === index ? -1 : index
                                Label { text: win.openFaq === index ? "−" : "+"; color: pal.title; font.pixelSize: 30; width: 30; horizontalAlignment: Text.AlignHCenter }
                            }
                            Label {
                                x: 64; width: 1030 - 64 - 40
                                visible: win.openFaq === index
                                text: modelData.a; color: pal.label; font.pixelSize: 17; wrapMode: Text.WordWrap
                                bottomPadding: 14
                            }
                        }
                    }
                }
                Card {
                    width: 1030
                    title: "Imagen y sonido"
                    subtitle: "Lo que muestran y reproducen tus juegos en la Wii U al iniciarse."
                    CardRow {
                        label: "Banners e iconos"
                        sub: app.artKeySet ? "Desde SteamGridDB con tu clave. Las portadas de GameTDB completan lo que no esté disponible."
                                           : "Portadas de GameTDB. Con una clave gratuita de SteamGridDB (steamgriddb.com, Preferences > API), cada juego obtiene allí un banner y un icono."
                        Pill { text: app.artKeySet ? "Cambiar clave" : "Agregar clave"; accessibleName: "Clave de SteamGridDB"; tone: "cyan"; onClicked: app.setArtKey() }
                    }
                    CardRow {
                        label: "Pantallas de inicio"
                        sub: app.artStyle === "console" ? "Como la Consola Virtual de Wii U presenta un juego: imagen, nombre y año de lanzamiento antes de mostrar una ilustración de 3DS."
                                                        : "Solo la imagen del juego, ocupando toda la pantalla."
                        Pill { text: "Arte"; accessibleName: "Solo la imagen"; tone: app.artStyle === "picture" ? "cyan" : "grey"; onClicked: app.setArtStyle("picture") }
                        Pill { text: "Consola virtual"; accessibleName: "Como la Consola Virtual presenta un juego"; tone: app.artStyle === "console" ? "cyan" : "grey"; onClicked: app.setArtStyle("console") }
                    }
                    CardRow {
                        label: "Sonido de inicio"
                        sub: app.soundCustom ? "Tu WAV reemplaza el sonido base en los juegos que compiles." : "Sonido Virtual Console incluido; se usa mientras carga el juego si no asignas uno propio."
                        Pill { text: "Seleccionar"; accessibleName: "Seleccionar el sonido de inicio"; tone: "cyan"; onClicked: app.chooseSound() }
                        Pill { text: "Quitar"; accessibleName: "Quitar el sonido personalizado"; tone: "red"; visible: app.soundCustom; onClicked: app.removeSound() }
                    }
                }
                Card {
                    width: 1030
                    title: "Acerca de"
                    subtitle: "3Dii-U Builder MOD " + app.version
                    CardRow {
                        label: "Versión " + app.version; sub: "Software libre y de código abierto: licencias y componentes incluidos."
                        Pill { text: "Acerca del proyecto y licencias"; onClicked: app.about() }
                    }
                    CardRow {
                        label: "Interfaz"; sub: "Esta ventana moderna o una vista compacta con pestañas y rutas de archivos."
                        Pill { text: "Moderna"; accessibleName: "Mantener esta ventana"; tone: "cyan" }
                        Pill { text: "Compacta"; accessibleName: "Cambiar a la ventana sencilla"; enabled: !app.building; onClicked: app.setLayout("simple") }
                    }
                    CardRow {
                        id: updatesRow
                        readonly property string st: app.updateState
                        readonly property bool offered: ["available", "downloading", "unpacking", "ready", "restarting", "failed"].indexOf(st) >= 0
                        label: app.releaseTag ? "Versión " + app.releaseTag : "Compilación personalizada (MOD)"
                        sub: st === "checking" ? "Buscando actualizaciones..." : st === "none" ? "No se encontró ninguna versión más reciente."
                            : st === "downloading" || st === "unpacking" ? "Descargando una versión más reciente."
                            : st === "ready" || st === "restarting" ? "Hay una nueva versión lista para iniciar."
                            : offered ? "Hay una versión más reciente disponible." : ""
                        Pill { text: "Mostrar"; accessibleName: "Mostrar la actualización en la página Compilar"; tone: "cyan"; visible: updatesRow.offered; onClicked: win.page = "build" }
                        Pill { text: "Buscar actualizaciones"; enabled: app.updateState !== "checking"; onClicked: app.checkUpdate() }
                    }
                    Label {
                        x: 60; width: 1030 - 60 - 40
                        visible: app.updateSetAside
                        text: "Una actualización anterior no pudo iniciarse en este equipo y se descartó. Puedes buscar actualizaciones para intentarlo de nuevo."
                        color: pal.subLabel; font.pixelSize: 15; wrapMode: Text.WordWrap
                        bottomPadding: 10
                    }
                    CardRow {
                        label: "Apariencia"; sub: "Sigue automáticamente el modo claro u oscuro de Windows, o elígelo manualmente."
                        Pill { text: "Sistema"; accessibleName: "Tema del sistema"; tone: app.themeMode === "system" ? "cyan" : "grey"; onClicked: app.setThemeMode("system") }
                        Pill { text: "Claro"; accessibleName: "Tema claro"; tone: app.themeMode === "light" ? "cyan" : "grey"; onClicked: app.setThemeMode("light") }
                        Pill { text: "Oscuro"; accessibleName: "Tema oscuro"; tone: app.themeMode === "dark" ? "cyan" : "grey"; onClicked: app.setThemeMode("dark") }
                    }
                }
            }
        }

        }

        // ---- the SD cards to choose from, under the Choose pill
        Item {
            id: cardMenu
            objectName: "cardMenu"  // test_window.py opens it
            anchors.fill: parent
            visible: false
            property point at
            function openAt(item) { at = item.mapToItem(surface, item.width, item.height + 6); visible = true; lookAgain.forceActiveFocus() }
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onClicked: cardMenu.visible = false; onWheel: (wheel) => wheel.accepted = true }
            Item {
                x: Math.max(10, Math.min(cardMenu.at.x - width, 1270 - width)); y: Math.max(10, Math.min(cardMenu.at.y, 710 - height))
                width: 440; height: menuCol.height + 24
                GlassFace { anchors.fill: parent; radius: 24; glossEdge: 40; bulge: 14 }
                Column {
                    id: menuCol
                    x: 12; y: 12; width: 416
                    Repeater {
                        model: app.cards
                        Rectangle {
                            width: 416; height: 42; radius: 21
                            color: pickHover.hovered ? pal.gloss : "transparent"
                            HoverHandler { id: pickHover }
                            Label { x: 18; width: parent.width - 36; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter; text: modelData.label; color: pal.label; font.pixelSize: 16 }
                            Hit { ringRadius: 21; name: modelData.label; role: Accessible.MenuItem; onActivated: { cardMenu.visible = false; app.pickCard(modelData.path) } }
                        }
                    }
                    Label { x: 18; width: 380; height: 42; verticalAlignment: Text.AlignVCenter; visible: app.cards.length === 0; text: "No se detectó ninguna tarjeta SD"; color: pal.subLabel; font.pixelSize: 16 }
                    Rectangle {
                        width: 416; height: 42; radius: 21
                        color: againHover.hovered ? pal.gloss : "transparent"
                        HoverHandler { id: againHover }
                        Label { x: 18; width: parent.width - 36; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter; text: "Volver a buscar tarjetas SD"; color: pal.cyanText; font.pixelSize: 16 }
                        Hit { id: lookAgain; ringRadius: 21; name: "Volver a buscar tarjetas SD"; role: Accessible.MenuItem; onActivated: app.refreshCards() }
                    }
                }
            }
        }

        // ---- the game's pictures and sound, under the Art pill: the start-up screens' (the GamePad's can have a picture of its own), the Wii
        // U Menu icon's, the game's start-up sound and the GamePad's bezel
        Item {
            id: artMenu
            objectName: "artMenu"  // test_window.py opens it
            anchors.fill: parent
            visible: false
            property point at
            function openAt(item) { at = item.mapToItem(surface, item.width, item.height + 6); visible = true; artFirst.focusIn() }
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onClicked: artMenu.visible = false; onWheel: (wheel) => wheel.accepted = true }
            Item {
                x: Math.max(204, Math.min(artMenu.at.x - width / 2, 1266 - width)); y: Math.max(10, Math.min(artMenu.at.y, 710 - height))
                width: 476; height: artCol.height + 24
                GlassFace { anchors.fill: parent; radius: 24; glossEdge: 40; bulge: 14 }
                Column {
                    id: artCol
                    x: 12; y: 12; width: 452
                    ArtEntry { id: artFirst; label: "Imagen de inicio..."; chosen: app.artText; onPicked: { artMenu.visible = false; app.chooseArt() } }
                    ArtEntry { label: "Icono del menú de Wii U..."; chosen: app.iconText; onPicked: { artMenu.visible = false; app.chooseIcon() } }
                    ArtEntry { label: "Pantalla de inicio del GamePad..."; chosen: app.padArtText; onPicked: { artMenu.visible = false; app.choosePadArt() } }
                    ArtEntry { label: "Sonido de inicio del juego..."; chosen: app.gameSoundText; onPicked: { artMenu.visible = false; app.chooseGameSound() } }
                    ArtEntry { label: "Marco del GamePad..."; chosen: app.bezelText; onPicked: { artMenu.visible = false; app.chooseBezel() } }
                    ArtEntry { label: "Quitar marco del GamePad"; danger: true; visible: app.hasBezel; onPicked: { artMenu.visible = false; app.removeBezel() } }
                    ArtEntry { label: "Quitar sonido del juego"; danger: true; visible: app.hasGameSound; onPicked: { artMenu.visible = false; app.removeGameSound() } }
                    ArtEntry { label: "Quitar imagen del GamePad"; danger: true; visible: app.hasPadArt; onPicked: { artMenu.visible = false; app.removePadArt() } }
                    ArtEntry { label: "Quitar icono personalizado"; danger: true; visible: app.hasIconArt; onPicked: { artMenu.visible = false; app.removeIcon() } }
                    ArtEntry { label: "Restaurar imagen e icono originales"; danger: true; visible: app.hasArt; onPicked: { artMenu.visible = false; app.removeArt() } }
                }
            }
        }
    }

    Item {
        id: previewDialog
        width: win.width
        height: win.height
        visible: false
        Rectangle {
            anchors.fill: parent
            color: pal.dim
            MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.AllButtons; onClicked: previewDialog.visible = false; onWheel: (wheel) => wheel.accepted = true }
        }
        Item {
            width: 1280; height: 720
            x: (win.width - width) / 2
            y: (win.height - height) / 2
            scale: win.s
            Item {
                width: 1020
                height: 656
                anchors.centerIn: parent
                GlassFace { anchors.fill: parent }
                Light { x: 26; y: 18; tone: "cyan" }
                Label {
                    x: 62; y: 14
                    text: win.previewKind === "tv" ? "Pantalla TV · 1280 × 720" : win.previewKind === "pad" ? "Pantalla GamePad · 854 × 480" : "Icono del menú de Wii U · 128 × 128"
                    color: pal.title; font.pixelSize: 24; font.weight: Font.DemiBold
                }
                Pill {
                    id: closePreviewPill
                    anchors.right: parent.right; anchors.rightMargin: 24
                    y: 12
                    text: "Cerrar"
                    tone: "cyan"
                    onClicked: previewDialog.visible = false
                }
                StartupPreview {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 58
                    kind: win.previewKind
                    enlarged: true
                    width: win.previewKind === "icon" ? 540 : 960
                    height: 540
                }
            }
        }
    }

    // ---------------------------------------------------------------- a question or a message, over the dimmed window
    Item {
        id: dialogLayer
        objectName: "dialog"  // test_window.py looks for it
        width: win.width
        height: win.height
        visible: !!app.dialog
        onDChanged: if (app.dialog) Qt.callLater(yesPill.focusIn)  // each question, not just the first, starts on yes
        readonly property var d: app.dialog || ({ title: "", message: "", yes: "OK", no: "", tone: "info" })
        Rectangle {
            anchors.fill: parent
            color: pal.dim
            MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.AllButtons; onWheel: (wheel) => wheel.accepted = true }
        }
        Item {
            width: 1280; height: 720
            x: (win.width - width) / 2
            y: (win.height - height) / 2
            scale: win.s
            Item {
                id: ask
                width: 700
                // never more than 80% of the window: a long message scrolls and the buttons stay in sight
                height: Math.min(576, msgView.y + message.implicitHeight + 92)
                anchors.centerIn: parent
                GlassFace { anchors.fill: parent }
                Light { x: 26; y: 24; tone: dialogLayer.d.tone === "warn" || dialogLayer.d.tone === "error" ? "red" : "cyan" }
                Label {
                    id: askTitle
                    x: 64; y: 12; width: ask.width - 88
                    text: dialogLayer.d.title; color: pal.title; font.pixelSize: 30; wrapMode: Text.WordWrap
                }
                Flickable {
                    id: msgView
                    x: 28; y: askTitle.y + askTitle.height + 14; width: ask.width - 46
                    height: ask.height - y - 88
                    contentHeight: message.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: Thumb { policy: msgView.contentHeight > msgView.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                    Label { id: message; width: msgView.width - 16; text: dialogLayer.d.message; color: pal.label; font.pixelSize: 18; wrapMode: Text.Wrap }
                }
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 24
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 24
                    spacing: 8
                    Pill { id: noPill; visible: dialogLayer.d.no.length > 0; text: dialogLayer.d.no; onClicked: app.answer(false) }
                    Pill { id: yesPill; text: dialogLayer.d.yes; tone: "cyan"; onClicked: app.answer(true) }
                }
            }
        }
    }

    Shortcut { sequences: [StandardKey.Open]; enabled: !win.blocked; onActivated: { win.page = "build"; app.chooseGame() } }
    Shortcut { sequence: "F1"; enabled: !win.blocked; onActivated: win.page = "help" }
    Shortcut { sequences: ["Return", "Enter"]; enabled: dialogLayer.visible && !noPill.hasFocus; onActivated: app.answer(true) }
    Shortcut { sequence: "Escape"; enabled: win.blocked; onActivated: dialogLayer.visible ? app.answer(false) : (cardMenu.visible = artMenu.visible = previewDialog.visible = false) }
}
