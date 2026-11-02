import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.02000001072883606, -0.3400000035762787, -0.32500001788139343), maximum: Qt.vector3d(0.6599999666213989, 0.32500001788139343, 0.32500001788139343)})
    id: node
    property bool rotorRunning: false
    property int rotorPwm: 0
    property int rotorDirection: 0
    property real rotorAngle: 0

    // Resources
    PrincipledMaterial {
        id: steel_material
        objectName: "steel"
        baseColor: "#ff9da8b4"
        metalness: 0.800000011920929
        roughness: 0.30000001192092896
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: dark_material
        objectName: "dark"
        baseColor: "#ff455059"
        metalness: 0.6499999761581421
        roughness: 0.3799999952316284
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: fan_blue_material
        objectName: "fan_blue"
        baseColor: "#ff59a6dc"
        metalness: 0.44999998807907104
        roughness: 0.3199999928474426
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: safety_yellow_material
        objectName: "safety_yellow"
        baseColor: "#fff9c845"
        metalness: 0.25
        roughness: 0.4000000059604645
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }

    // Nodes:
    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: fan_base
            objectName: "fan_base"
            position: Qt.vector3d(0.32, -0.3, 0)
            source: "meshes/cube_008_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_bearing
            objectName: "fan_bearing"
            position: Qt.vector3d(0.08, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_014_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_0
            objectName: "fan_flange_bolt_0"
            position: Qt.vector3d(0.485, 0, -0.303)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_1
            objectName: "fan_flange_bolt_1"
            position: Qt.vector3d(0.485, 0.1515, -0.262406)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_001_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_2
            objectName: "fan_flange_bolt_2"
            position: Qt.vector3d(0.485, 0.262406, -0.1515)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_002_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_3
            objectName: "fan_flange_bolt_3"
            position: Qt.vector3d(0.485, 0.303, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_003_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_4
            objectName: "fan_flange_bolt_4"
            position: Qt.vector3d(0.485, 0.262406, 0.1515)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_004_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_5
            objectName: "fan_flange_bolt_5"
            position: Qt.vector3d(0.485, 0.1515, 0.262406)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_005_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_6
            objectName: "fan_flange_bolt_6"
            position: Qt.vector3d(0.485, 0, 0.303)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_006_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_7
            objectName: "fan_flange_bolt_7"
            position: Qt.vector3d(0.485, -0.1515, 0.262406)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_007_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_8
            objectName: "fan_flange_bolt_8"
            position: Qt.vector3d(0.485, -0.262406, 0.1515)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_008_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_9
            objectName: "fan_flange_bolt_9"
            position: Qt.vector3d(0.485, -0.303, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_009_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_10
            objectName: "fan_flange_bolt_10"
            position: Qt.vector3d(0.485, -0.262406, -0.1515)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_010_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_flange_bolt_11
            objectName: "fan_flange_bolt_11"
            position: Qt.vector3d(0.485, -0.1515, -0.262406)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_011_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_foot_0_5_0_13
            objectName: "fan_foot_0.5_0.13"
            position: Qt.vector3d(0.5, -0.24, -0.13)
            source: "meshes/cube_012_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_foot_0_5__0_13
            objectName: "fan_foot_0.5_-0.13"
            position: Qt.vector3d(0.5, -0.24, 0.13)
            source: "meshes/cube_011_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_foot_0_14_0_13
            objectName: "fan_foot_0.14_0.13"
            position: Qt.vector3d(0.14, -0.24, -0.13)
            source: "meshes/cube_010_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_foot_0_14__0_13
            objectName: "fan_foot_0.14_-0.13"
            position: Qt.vector3d(0.14, -0.24, 0.13)
            source: "meshes/cube_009_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: fan_front_flange
            objectName: "fan_front_flange"
            source: "meshes/fan_front_flange_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_ring_0_11
            objectName: "fan_grille_ring_0.11"
            position: Qt.vector3d(0.49, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/torus_001_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_ring_0_19
            objectName: "fan_grille_ring_0.19"
            position: Qt.vector3d(0.49, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/torus_002_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_ring_0_265
            objectName: "fan_grille_ring_0.265"
            position: Qt.vector3d(0.49, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/torus_003_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_0
            objectName: "fan_grille_spoke_0"
            position: Qt.vector3d(0.49, 0, -0.135)
            source: "meshes/cube_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_1
            objectName: "fan_grille_spoke_1"
            position: Qt.vector3d(0.49, 0.0954594, -0.0954594)
            rotation: Qt.quaternion(0.92388, 0.382683, 0, 0)
            source: "meshes/cube_001_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_2
            objectName: "fan_grille_spoke_2"
            position: Qt.vector3d(0.49, 0.135, 0)
            rotation: Qt.quaternion(0.707107, 0.707107, 0, 0)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cube_002_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_3
            objectName: "fan_grille_spoke_3"
            position: Qt.vector3d(0.49, 0.0954594, 0.0954594)
            rotation: Qt.quaternion(0.382683, 0.92388, 0, 0)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cube_003_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_4
            objectName: "fan_grille_spoke_4"
            position: Qt.vector3d(0.49, 0, 0.135)
            rotation: Qt.quaternion(-4.37114e-08, 1, 0, 0)
            source: "meshes/cube_004_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_5
            objectName: "fan_grille_spoke_5"
            position: Qt.vector3d(0.49, -0.0954594, 0.0954594)
            rotation: Qt.quaternion(-0.382683, 0.92388, 0, 0)
            source: "meshes/cube_005_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_6
            objectName: "fan_grille_spoke_6"
            position: Qt.vector3d(0.49, -0.135, 0)
            rotation: Qt.quaternion(0.707107, -0.707107, 0, 0)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cube_006_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_grille_spoke_7
            objectName: "fan_grille_spoke_7"
            position: Qt.vector3d(0.49, -0.0954594, -0.0954594)
            rotation: Qt.quaternion(0.92388, -0.382683, 0, 0)
            source: "meshes/cube_007_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: fan_housing
            objectName: "fan_housing"
            source: "meshes/fan_housing_mesh.mesh"
            materials: [
                fan_blue_material,
                steel_material
            ]
        }
        Node {
            id: fan_rotor
            eulerRotation.x: node.rotorAngle

            Model {
                id: fan_impeller
                objectName: "fan_impeller"
                position: Qt.vector3d(0.34, 0, 0)
                rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
                scale: Qt.vector3d(1, 1, 1)
                source: "meshes/cylinder_012_mesh.mesh"
                materials: [
                    safety_yellow_material
                ]
            }
            Model {
                id: fan_shaft
                objectName: "fan_shaft"
                position: Qt.vector3d(0.22, 0, 0)
                rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
                scale: Qt.vector3d(1, 1, 1)
                source: "meshes/cylinder_013_mesh.mesh"
                materials: [
                    steel_material
                ]
            }
        }
    }

    // Animations:
    FrameAnimation {
        running: node.rotorRunning && node.rotorPwm > 0
        onTriggered: {
            const direction = node.rotorDirection === 0 ? 1 : -1
            const speedDegPerSec = (180 + node.rotorPwm * 0.72) * direction
            node.rotorAngle = (node.rotorAngle + speedDegPerSec * frameTime) % 360
        }
    }
}
