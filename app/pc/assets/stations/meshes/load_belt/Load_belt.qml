import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.05499999225139618, -0.29499998688697815, -0.6159999966621399), maximum: Qt.vector3d(0.44999998807907104, 0.17599999904632568, 0.17599999904632568)})
    id: node
    property bool rotorRunning: false
    property int rotorPwm: 0
    property int rotorDirection: 0

    PrincipledMaterial {
        id: belt_rubber_material
        objectName: "belt_rubber"
        baseColor: "#ff3f3f42"
        metalness: 0.05000000074505806
        roughness: 0.800000011920929
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
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
        id: safety_yellow_material
        objectName: "safety_yellow"
        baseColor: "#fff9c845"
        metalness: 0.25
        roughness: 0.4000000059604645
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: belt
            objectName: "belt"
            source: "meshes/belt_mesh.mesh"
            materials: [
                belt_rubber_material
            ]
        }
        Model {
            id: belt_base
            objectName: "belt_base"
            position: Qt.vector3d(0.2, -0.26, -0.22)
            source: "meshes/cube_014_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: belt_bearing
            objectName: "belt_bearing"
            position: Qt.vector3d(0.04, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_004_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: belt_pulleys
            objectName: "belt_pulleys"
            source: "meshes/belt_pulleys_mesh.mesh"
            materials: [
                safety_yellow_material,
                steel_material,
                dark_material
            ]
        }
        Model {
            id: pulley_key_0
            objectName: "pulley_key_0"
            position: Qt.vector3d(0.248, 0.031, 0)
            source: "meshes/cube_006_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_key_0_44
            objectName: "pulley_key_0.44"
            position: Qt.vector3d(0.248, 0.031, -0.44)
            source: "meshes/cube_013_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_rim_0_44_0_137
            objectName: "pulley_rim_0.44_0.137"
            source: "meshes/pulley_rim_0_44_0_137_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_rim_0_44_0_223
            objectName: "pulley_rim_0.44_0.223"
            source: "meshes/pulley_rim_0_44_0_223_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_rim_0_0_137
            objectName: "pulley_rim_0_0.137"
            source: "meshes/pulley_rim_0_0_137_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pulley_rim_0_0_223
            objectName: "pulley_rim_0_0.223"
            source: "meshes/pulley_rim_0_0_223_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pulley_shaft_0
            objectName: "pulley_shaft_0"
            position: Qt.vector3d(0.09, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_002_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_shaft_0_44
            objectName: "pulley_shaft_0.44"
            position: Qt.vector3d(0.09, 0, -0.44)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_003_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_spoke_0_44_0
            objectName: "pulley_spoke_0.44_0"
            position: Qt.vector3d(0.18, 0, -0.525)
            source: "meshes/cube_007_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_spoke_0_44_1
            objectName: "pulley_spoke_0.44_1"
            position: Qt.vector3d(0.18, 0.0808398, -0.466266)
            rotation: Qt.quaternion(0.809017, 0.587785, 0, 0)
            source: "meshes/cube_008_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_spoke_0_44_2
            objectName: "pulley_spoke_0.44_2"
            position: Qt.vector3d(0.18, 0.0499617, -0.371234)
            rotation: Qt.quaternion(0.309017, 0.951057, 0, 0)
            source: "meshes/cube_009_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_spoke_0_44_3
            objectName: "pulley_spoke_0.44_3"
            position: Qt.vector3d(0.18, -0.0499617, -0.371234)
            rotation: Qt.quaternion(-0.309017, 0.951056, 0, 0)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cube_010_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_spoke_0_44_4
            objectName: "pulley_spoke_0.44_4"
            position: Qt.vector3d(0.18, -0.0808398, -0.466266)
            rotation: Qt.quaternion(0.809017, -0.587785, 0, 0)
            source: "meshes/cube_011_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pulley_spoke_0_0
            objectName: "pulley_spoke_0_0"
            position: Qt.vector3d(0.18, 0, -0.085)
            source: "meshes/cube_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pulley_spoke_0_1
            objectName: "pulley_spoke_0_1"
            position: Qt.vector3d(0.18, 0.0808398, -0.0262664)
            rotation: Qt.quaternion(0.809017, 0.587785, 0, 0)
            source: "meshes/cube_001_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pulley_spoke_0_2
            objectName: "pulley_spoke_0_2"
            position: Qt.vector3d(0.18, 0.0499617, 0.0687664)
            rotation: Qt.quaternion(0.309017, 0.951057, 0, 0)
            source: "meshes/cube_002_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pulley_spoke_0_3
            objectName: "pulley_spoke_0_3"
            position: Qt.vector3d(0.18, -0.0499617, 0.0687664)
            rotation: Qt.quaternion(-0.309017, 0.951056, 0, 0)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cube_003_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pulley_spoke_0_4
            objectName: "pulley_spoke_0_4"
            position: Qt.vector3d(0.18, -0.0808398, -0.0262664)
            rotation: Qt.quaternion(0.809017, -0.587785, 0, 0)
            source: "meshes/cube_004_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pulley_support_0
            objectName: "pulley_support_0"
            position: Qt.vector3d(0.015, -0.13, 0)
            source: "meshes/cube_005_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: pulley_support_0_44
            objectName: "pulley_support_0.44"
            position: Qt.vector3d(0.015, -0.13, -0.44)
            source: "meshes/cube_012_mesh.mesh"
            materials: [
                dark_material
            ]
        }
    }

}
