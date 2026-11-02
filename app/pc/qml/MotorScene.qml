import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

Item {
    id: root
    property string runState: "normal"
    property string faultSite: "none"
    property int deviceId: 1
    property bool motorRunning: false
    property int motorPwm: 0
    property int motorDirection: 0
    property url meshBase: meshBaseUrl
    property url combinedMesh: combinedMeshUrl
    property url stationMeshBase: stationMeshBaseUrl
    property int animSeq: 0
    readonly property int loadStatus: loadAssembly.status

    // 0=整机组装  1=爆炸分解
    property real explodeT: 0
    property real combinedAlpha: 1
    property real partsAlpha: 0
    property real faultPulse: 1.0
    property bool explodeIntroComplete: false
    property bool pivotLocked: false
    // 沿各部件相对机壳中心的实际方向外抛（随 jichu 导出自动适配）
    property real explodeSpread: 0.28

    // 整台 wanzheng：yaw=90 侧面对观众；五部件拆解后需再转 180° 才正面（不改 mesh，只转相机）
    readonly property real introViewPitch: -14
    readonly property real introCameraYaw: 90
    readonly property real partsCameraYaw: 180
    // 仅在外抛开始时切 180°；淡入淡出阶段不再提前转相机
    readonly property real cameraYaw: explodeT > 0 ? partsCameraYaw : introCameraYaw

    readonly property bool hasCombined: combinedMesh.toString().length > 0
    readonly property string loadMeshName: {
        switch (deviceId) {
        case 1: return "load_fan_three/Load_fan_three.qml"
        case 2: return "load_fan/Load_fan.qml"
        case 3: return "load_gearbox/Load_gearbox.qml"
        case 4: return "load_belt/Load_belt.qml"
        case 5: return "load_pump/Load_pump.qml"
        default: return ""
        }
    }
    readonly property bool hasLoadModel: stationMeshBase.toString().length > 0
                                          && loadMeshName.length > 0
    readonly property bool fanLoad: deviceId === 1 || deviceId === 2
    readonly property string loadLabel: {
        switch (deviceId) {
        case 1: return "1号 · 风机负载"
        case 2: return "2号 · 风机负载"
        case 3: return "3号 · 减速机负载"
        case 4: return "4号 · 皮带传动"
        case 5: return "5号 · 泵组负载"
        default: return "基础电机工位"
        }
    }
    readonly property color loadBaseColor: {
        switch (deviceId) {
        case 1: return "#2b82c7"
        case 2: return "#2b82c7"
        case 3: return "#2d7d4f"
        case 4: return "#b6802b"
        case 5: return "#2f6fb4"
        default: return equipmentGray
        }
    }
    readonly property bool faultActive: runState !== "normal"
                                        && faultSite !== "none"
                                        && faultSite !== ""
    // 仅在五部件分解就位后才在 3D 上标具体故障部位（整台 wanzheng 不着色）
    readonly property bool partsFaultView: root.partsAlpha > 0.82 && root.explodeT > 0.68
    readonly property bool faultBlinkActive: faultActive && partsFaultView
    // 整台与拆解过程中不转；五部件就位后绕整机组装中心慢速自转
    readonly property bool autoSpinEnabled: root.partsAlpha > 0.85 && root.explodeT > 0.75

    // GB 2682 / 工业 HMI 指示灯色（灰·蓝·黄·橙·红）
    readonly property color equipmentGray: "#646B73"
    readonly property var severityLegend: [
        { level: "normal", color: "#5F8A65", label: "正常", desc: "运行许可·无告警" },
        { level: "notice", color: "#2563C4", label: "注意", desc: "提示信息·加强监视" },
        { level: "warning", color: "#C9A227", label: "预警", desc: "黄灯·超基线点检" },
        { level: "alarm", color: "#D97706", label: "告警", desc: "橙灯·降载限期处理" },
        { level: "fault", color: "#B91C1C", label: "严重", desc: "红灯·建议停机检修" },
        { level: "emergency", color: "#991B1B", label: "紧急", desc: "红灯快闪·立即停机" }
    ]

    property int faultBlinkDuration: 520

    function partSeverity(part) {
        if (!partsFaultView)
            return "normal"
        if (runState === "normal" || faultSite === "none" || faultSite === "")
            return "normal"
        if (faultSite === part)
            return runState
        return "normal"
    }

    function isHighlightedPart(part) {
        return partSeverity(part) !== "normal"
    }

    function severityColor(level) {
        for (var i = 0; i < severityLegend.length; ++i) {
            if (severityLegend[i].level === level)
                return severityLegend[i].color
        }
        return equipmentGray
    }

    function severityLabel(level) {
        for (var j = 0; j < severityLegend.length; ++j) {
            if (severityLegend[j].level === level)
                return severityLegend[j].label
        }
        return "正常"
    }

    function faultSiteLabel(site) {
        switch (site) {
        case "bearing": return "轴承端"
        case "imbalance": return "转子不平衡"
        case "looseness": return "地脚松动"
        case "coupling": return "联轴器"
        case "body": return "机壳"
        default: return "无"
        }
    }

    function modelBoundsSize(model) {
        if (!model || !model.bounds)
            return Qt.vector3d(0, 0, 0)
        var b = model.bounds
        return Qt.vector3d(b.maximum.x - b.minimum.x,
                            b.maximum.y - b.minimum.y,
                            b.maximum.z - b.minimum.z)
    }

    function refreshLoadTransform() {
        if (!hasLoadModel || !combinedModel.bounds || !loadModel.bounds
                || !loadAssembly.item)
            return

        var mb = combinedModel.bounds
        var lb = loadModel.bounds
        var motorSize = modelBoundsSize(combinedModel)
        var loadSize = modelBoundsSize(loadModel)
        var scale = motorSize.x / Math.max(loadSize.x, 1e-6) * 0.72
        scale = Math.max(0.05, Math.min(scale, 4.0))
        loadModel.scale = Qt.vector3d(scale, scale, scale)

        var motorCenter = boundsCenter(combinedModel)
        var loadCenter = boundsCenter(loadModel)
        loadModel.position = Qt.vector3d(
            mb.maximum.x - lb.minimum.x * scale + motorSize.x * 0.03,
            motorCenter.y - loadCenter.y * scale,
            motorCenter.z - loadCenter.z * scale)
        console.log("[station-load]", deviceId, "motor", motorSize,
                    "load", loadSize, "scale", scale)
    }

    function severityBlinkMs(level) {
        switch (level) {
        case "notice": return 880
        case "warning": return 620
        case "alarm": return 460
        case "fault": return 340
        case "emergency": return 220
        default: return 520
        }
    }

    function updateBlinkDuration() {
        faultBlinkDuration = severityBlinkMs(runState)
    }

    onRunStateChanged: updateBlinkDuration()

    function basePartColor(part) {
        var level = partSeverity(part)
        if (level !== "normal")
            return severityColor(level)
        return equipmentGray
    }

    function partColor(part) {
        var c = basePartColor(part)
        if (!isHighlightedPart(part))
            return c
        var level = partSeverity(part)
        var pulse = 1.06 + 0.38 * faultPulse
        if (level === "emergency" || level === "fault")
            pulse = 1.08 + 0.52 * faultPulse
        return Qt.lighter(c, pulse)
    }

    function partModel(part) {
        switch (part) {
        case "looseness": return loosenessModel
        case "body": return bodyModel
        case "bearing": return bearingModel
        case "coupling": return couplingModel
        case "imbalance": return imbalanceModel
        default: return null
        }
    }

    // 拆解外抛：按部件包围盒中心相对机壳方向拉开（不手写轴向，避免与新 jichu 不一致）
    function partExplodeOffset(part) {
        if (part === "body")
            return Qt.vector3d(0, 0, 0)
        var model = partModel(part)
        if (!model || !model.bounds || !bodyModel.bounds)
            return Qt.vector3d(0, 0, 0)
        var bodyC = boundsCenter(bodyModel)
        var c = boundsCenter(model)
        var dx = c.x - bodyC.x
        var dy = c.y - bodyC.y
        var dz = c.z - bodyC.z
        var len = Math.sqrt(dx * dx + dy * dy + dz * dz)
        if (len < 1e-5)
            return Qt.vector3d(0, 0, 0)
        var s = explodeSpread * explodeT / len
        return Qt.vector3d(dx * s, dy * s, dz * s)
    }

    function emissiveFor(part) {
        if (!isHighlightedPart(part))
            return Qt.vector3d(0, 0, 0)
        var level = partSeverity(part)
        var p = faultPulse
        switch (level) {
        case "notice":
            return Qt.vector3d(0.06 * p, 0.28 * p, 0.72 * p)
        case "warning":
            return Qt.vector3d(0.72 * p, 0.58 * p, 0.05 * p)
        case "alarm":
            return Qt.vector3d(0.78 * p, 0.38 * p, 0.04 * p)
        case "fault":
            return Qt.vector3d(0.72 * p, 0.1 * p, 0.06 * p)
        case "emergency":
            return Qt.vector3d(0.85 * p, 0.12 * p, 0.08 * p)
        default:
            return Qt.vector3d(0, 0, 0)
        }
    }

    function partMetalness(part) {
        return isHighlightedPart(part) ? 0.18 : 0.42
    }

    function partRoughness(part) {
        return isHighlightedPart(part) ? 0.35 : 0.48
    }

    property vector3d meshCenter: Qt.vector3d(0, 0, 0)

    function boundsCenter(model) {
        if (!model || !model.bounds)
            return Qt.vector3d(0, 0, 0)
        var b = model.bounds
        return Qt.vector3d((b.minimum.x + b.maximum.x) * 0.5,
                            (b.minimum.y + b.maximum.y) * 0.5,
                            (b.minimum.z + b.maximum.z) * 0.5)
    }

    function refreshMeshCenter() {
        if (pivotLocked)
            return
        if (!hasCombined || !combinedModel.bounds)
            return
        meshCenter = boundsCenter(combinedModel)
        pivotLocked = true
    }

    NumberAnimation {
        id: faultPulseAnim
        target: root
        property: "faultPulse"
        running: root.faultBlinkActive
        loops: Animation.Infinite
        from: 0.25
        to: 1.0
        duration: root.faultBlinkDuration
        easing.type: Easing.InOutSine
    }

    SequentialAnimation {
        id: explodeSequence
        PauseAnimation { duration: 400 }
        ScriptAction {
            script: {
                // 整台→五部件瞬时切换，与外抛+相机 180° 同一时刻开始
                root.combinedAlpha = 0
                root.partsAlpha = 1
            }
        }
        NumberAnimation {
            target: root
            property: "explodeT"
            from: 0
            to: 1
            duration: 1800
            easing.type: Easing.OutCubic
        }
        ScriptAction {
            script: {
                root.explodeIntroComplete = true
                root.combinedAlpha = 0
                root.partsAlpha = 1
                root.explodeT = 1
            }
        }
    }

    function startIntroExplodeOnce() {
        if (explodeIntroComplete || explodeSequence.running)
            return
        explodeT = 0
        explodeSequence.restart()
    }

    // 仅首次进入：在当前视角下拆解，不重置相机/朝向
    Timer {
        id: startExplodeTimer
        interval: root.hasCombined ? 3200 : 1200
        repeat: false
        running: !root.explodeIntroComplete
        onTriggered: root.startIntroExplodeOnce()
    }

    // animSeq 仅预留「整段重播」；故障刷新不再触发
    onAnimSeqChanged: {
        if (animSeq <= 0)
            return
        explodeIntroComplete = false
        explodeSequence.stop()
        explodeT = 0
        combinedAlpha = 1
        partsAlpha = 0
        spinRoot.spin = 0
        startExplodeTimer.interval = 600
        startExplodeTimer.restart()
    }

    Component.onCompleted: {
        updateBlinkDuration()
        startExplodeTimer.start()
    }

    View3D {
        id: view3d
        anchors.fill: parent
        camera: sceneCamera
        environment: SceneEnvironment {
            clearColor: "#0a101a"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.NoAA
        }

        Node {
            id: orbitOrigin
            eulerRotation.x: root.introViewPitch
            eulerRotation.y: root.cameraYaw

            PerspectiveCamera {
                id: sceneCamera
                property real pull: root.explodeT * root.partsAlpha
                z: 5.2 + 0.55 * pull
                clipNear: 0.01
                clipFar: 500
            }
        }

        DirectionalLight { brightness: 1.45; eulerRotation: Qt.vector3d(-50, 35, 0); castsShadow: false }
        DirectionalLight { brightness: 0.6; eulerRotation: Qt.vector3d(20, -120, 0); castsShadow: false }
        DirectionalLight { brightness: 0.38; eulerRotation: Qt.vector3d(90, 0, 0); castsShadow: false }

        Node {
            id: spinRoot
            property real spin: 0
            eulerRotation.y: spin

            NumberAnimation on spin {
                running: root.autoSpinEnabled
                loops: Animation.Infinite
                duration: 48000
                from: 0
                to: 360
            }

            // 把 mesh 几何中心挪到原点，绕 Y 轴 / 鼠标轨道都过机体中心
            Node {
                id: centeredContent
                position: Qt.vector3d(-root.meshCenter.x,
                                       -root.meshCenter.y,
                                       -root.meshCenter.z)

                Model {
                    id: combinedModel
                    visible: root.hasCombined && root.combinedAlpha > 0.02
                    opacity: root.combinedAlpha
                    source: combinedMesh
                    materials: PrincipledMaterial {
                        baseColor: root.equipmentGray
                        metalness: 0.44
                        roughness: 0.52
                        emissiveFactor: Qt.vector3d(0, 0, 0)
                    }
                    onBoundsChanged: {
                        root.refreshMeshCenter()
                        root.refreshLoadTransform()
                    }
                }

                Node {
                    id: partsNode
                    opacity: root.partsAlpha
                    visible: root.partsAlpha > 0.02

                    Node {
                        position: root.partExplodeOffset("looseness")
                        Model {
                            id: loosenessModel
                            source: meshBase + "looseness.mesh"
                            materials: PrincipledMaterial {
                                baseColor: root.partColor("looseness")
                                metalness: root.partMetalness("looseness")
                                roughness: root.partRoughness("looseness")
                                emissiveFactor: root.emissiveFor("looseness")
                            }
                        }
                    }
                    Node {
                        position: root.partExplodeOffset("body")
                        Model {
                            id: bodyModel
                            source: meshBase + "body.mesh"
                            materials: PrincipledMaterial {
                                baseColor: root.partColor("body")
                                metalness: root.partMetalness("body")
                                roughness: root.partRoughness("body")
                                emissiveFactor: root.emissiveFor("body")
                            }
                        }
                    }
                    Node {
                        position: root.partExplodeOffset("bearing")
                        Model {
                            id: bearingModel
                            source: meshBase + "bearing.mesh"
                            materials: PrincipledMaterial {
                                baseColor: root.partColor("bearing")
                                metalness: root.partMetalness("bearing")
                                roughness: root.partRoughness("bearing")
                                emissiveFactor: root.emissiveFor("bearing")
                            }
                        }
                    }
                    Node {
                        position: root.partExplodeOffset("coupling")
                        Model {
                            id: couplingModel
                            source: meshBase + "coupling.mesh"
                            materials: PrincipledMaterial {
                                baseColor: root.partColor("coupling")
                                metalness: root.partMetalness("coupling")
                                roughness: root.partRoughness("coupling")
                                emissiveFactor: root.emissiveFor("coupling")
                            }
                        }
                    }
                    Node {
                        position: root.partExplodeOffset("imbalance")
                        Model {
                            id: imbalanceModel
                            source: meshBase + "imbalance.mesh"
                            materials: PrincipledMaterial {
                                baseColor: root.partColor("imbalance")
                                metalness: root.partMetalness("imbalance")
                                roughness: root.partRoughness("imbalance")
                                emissiveFactor: root.emissiveFor("imbalance")
                            }
                        }
                    }
                }

                Node {
                    id: loadModel
                    readonly property var bounds: loadAssembly.item
                                                 ? loadAssembly.item.assemblyBounds : null
                    visible: root.hasLoadModel
                    Loader3D {
                        id: loadAssembly
                        objectName: "stationAssemblyLoader"
                        source: root.hasLoadModel
                            ? root.stationMeshBase + root.loadMeshName
                            : ""
                        onLoaded: root.refreshLoadTransform()
                    }
                    Binding {
                        target: loadAssembly.item
                        property: "rotorRunning"
                        value: root.deviceId === 1
                               && root.motorRunning && root.motorPwm > 0
                        when: loadAssembly.item !== null && root.fanLoad
                        restoreMode: Binding.RestoreNone
                    }
                    Binding {
                        target: loadAssembly.item
                        property: "rotorPwm"
                        value: root.deviceId === 1 ? root.motorPwm : 0
                        when: loadAssembly.item !== null && root.fanLoad
                        restoreMode: Binding.RestoreNone
                    }
                    Binding {
                        target: loadAssembly.item
                        property: "rotorDirection"
                        value: root.deviceId === 1 ? root.motorDirection : 0
                        when: loadAssembly.item !== null && root.fanLoad
                        restoreMode: Binding.RestoreNone
                    }
                    onBoundsChanged: root.refreshLoadTransform()
                }
            }
        }

        OrbitCameraController {
            id: orbitCtrl
            anchors.fill: parent
            origin: orbitOrigin
            camera: sceneCamera
            xSpeed: 0.35
            ySpeed: 0.35
            panEnabled: true
            mouseEnabled: true
        }
    }

    Text {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        color: "#64748b"
        font.pixelSize: 10
        text: "左键旋转 · 滚轮缩放 · Ctrl+拖动平移"
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        anchors.bottomMargin: severityLegendBox.height + 18
        visible: root.hasLoadModel
        radius: 6
        color: "#0f172acc"
        border.color: root.loadBaseColor
        border.width: 1
        width: loadLabelText.implicitWidth + 18
        height: loadLabelText.implicitHeight + 10
        Text {
            id: loadLabelText
            anchors.centerIn: parent
            color: "#dbe7f5"
            font.pixelSize: 11
            font.bold: true
            text: root.loadLabel
        }
    }

    // 当前告警条（顶部）
    Rectangle {
        id: faultBanner
        z: 50
        visible: root.faultActive && root.partsFaultView
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 6
        width: Math.min(parent.width - 24, faultBannerText.implicitWidth + 28)
        height: 30
        radius: 6
        color: "#0c1220ee"
        border.width: 2
        border.color: root.severityColor(root.runState)

        Text {
            id: faultBannerText
            anchors.centerIn: parent
            color: root.severityColor(root.runState)
            font.pixelSize: 12
            font.bold: true
            text: "【" + root.severityLabel(root.runState) + "】"
                  + root.faultSiteLabel(root.faultSite)
                  + " · 故障部位已定位"
        }
    }

    // 工业告警等级图例（始终显示）
    Item {
        id: severityLegendBox
        z: 50
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: 228
        height: legendColumn.implicitHeight + 20

        Rectangle {
            anchors.fill: parent
            color: "#0c1220ee"
            border.color: "#475569"
            radius: 8
        }

        Column {
            id: legendColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 5

            Text {
                text: "故障等级图例"
                color: "#94a3b8"
                font.pixelSize: 11
                font.bold: true
            }

            Repeater {
                model: root.severityLegend
                delegate: Row {
                    spacing: 6
                    width: legendColumn.width
                    opacity: (modelData.level === "normal" && root.runState === "normal")
                             || modelData.level === root.runState ? 1.0 : 0.72

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 2
                        color: modelData.color
                        border.width: modelData.level === root.runState && root.runState !== "normal" ? 1 : 0
                        border.color: "#e2e8f0"
                        anchors.verticalCenter: parent.verticalCenter

                        SequentialAnimation on opacity {
                            running: modelData.level === root.runState
                                     && root.faultBlinkActive
                                     && root.partsFaultView
                                     && root.runState !== "normal"
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.45; to: 1.0; duration: root.faultBlinkDuration / 2; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 1.0; to: 0.45; duration: root.faultBlinkDuration / 2; easing.type: Easing.InOutSine }
                        }
                    }

                    Text {
                        width: parent.width - 20
                        wrapMode: Text.WordWrap
                        color: modelData.level === root.runState && root.runState !== "normal"
                               ? modelData.color : "#94a3b8"
                        font.pixelSize: 9
                        text: modelData.label + " · " + modelData.desc
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Text {
                color: "#64748b"
                font.pixelSize: 8
                wrapMode: Text.WordWrap
                width: parent.width
                text: "先整台 wanzheng → 拆解后仅五部件外抛；故障色标在对应件上"
            }
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 28
        color: "#475569"
        font.pixelSize: 11
        visible: root.explodeT > 0.02 || root.partsAlpha > 0.02
        text: {
            if (root.combinedAlpha > 0.5)
                return "① 完整装配 · wanzheng 整台（3D 暂不标故障部位）"
            if (root.partsAlpha > 0.02 && root.explodeT < 0.98)
                return "② 拆解中 · 整台淡出 · 五部件沿轴向拉开"
            if (root.faultBlinkActive)
                return "③ 部件视图 · 仅高亮 " + root.faultSiteLabel(root.faultSite)
            if (root.faultActive)
                return "③ 部件视图 · 可切换工况等级与故障部位"
            return "③ 部件视图 · 切换工况场景进行故障定位"
        }
    }

    Text {
        anchors.centerIn: parent
        color: "#64748b"
        font.pixelSize: 14
        visible: startExplodeTimer.running
        text: hasCombined ? "正在加载 wanzheng 整台 mesh…" : "正在加载 jichu 五部件…"
        horizontalAlignment: Text.AlignHCenter
    }
}
