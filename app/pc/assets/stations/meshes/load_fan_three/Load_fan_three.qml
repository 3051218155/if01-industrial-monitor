import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.06000000238418579, -0.35499998927116394, -0.34411755204200745), maximum: Qt.vector3d(0.5, 0.2797134816646576, 0.2530907094478607)})
    id: node

    property bool rotorRunning: false
    property int rotorPwm: 0
    property int rotorDirection: 0
    property real rotorAngle: 0

    PrincipledMaterial {
        id: three_fan_dark_material
        objectName: "three_fan_dark"
        baseColor: "#ff55616c"
        metalness: 0.550000011920929
        roughness: 0.4000000059604645
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: three_fan_steel_material
        objectName: "three_fan_steel"
        baseColor: "#ffa8b3ba"
        metalness: 0.6499999761581421
        roughness: 0.3499999940395355
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: three_fan_hub_material
        objectName: "three_fan_hub"
        baseColor: "#ffeccb6f"
        metalness: 0.550000011920929
        roughness: 0.3199999928474426
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: three_fan_blades_material
        objectName: "three_fan_blades"
        baseColor: "#ff50bfd7"
        metalness: 0.3799999952316284
        roughness: 0.30000001192092896
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: fan_three_base
            objectName: "fan_three_base"
            position: Qt.vector3d(0.22, -0.32, 0)
            source: "meshes/cube_mesh.mesh"
            materials: [
                three_fan_dark_material
            ]
        }
        Model {
            id: fan_three_bearing
            objectName: "fan_three_bearing"
            position: Qt.vector3d(0.18, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_mesh.mesh"
            materials: [
                three_fan_dark_material
            ]
        }
        Model {
            id: fan_three_pedestal
            objectName: "fan_three_pedestal"
            position: Qt.vector3d(0.13, -0.17, 0)
            source: "meshes/cube_001_mesh.mesh"
            materials: [
                three_fan_steel_material
            ]
        }
        Node {
            eulerRotation.x: node.rotorAngle
            Model {
                id: fan_three_rotor
                objectName: "fan_three_rotor"
                position: Qt.vector3d(0.43, 0, 0)
                rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
                scale: Qt.vector3d(1, 1, 1)
                source: "meshes/fan_three_rotor_mesh.mesh"
                materials: [
                    three_fan_hub_material,
                    three_fan_dark_material,
                    three_fan_blades_material
                ]
            }
        }
        Model {
            id: fan_three_shaft
            objectName: "fan_three_shaft"
            position: Qt.vector3d(0.31, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_001_mesh.mesh"
            materials: [
                three_fan_steel_material
            ]
        }
    }

    // Rotate only while the motor is enabled and PWM is positive.
    FrameAnimation {
        running: node.rotorRunning && node.rotorPwm > 0
        onTriggered: {
            const direction = node.rotorDirection === 0 ? 1 : -1
            const speedDegPerSec = (180 + node.rotorPwm * 0.72) * direction
            node.rotorAngle = (node.rotorAngle + speedDegPerSec * frameTime) % 360
        }
    }

}
