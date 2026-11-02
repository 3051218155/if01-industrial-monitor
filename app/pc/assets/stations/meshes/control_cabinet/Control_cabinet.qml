import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.2800000011920929, -0.04500000178813934, -0.1899999976158142), maximum: Qt.vector3d(0.2800000011920929, 0.6800000071525574, 0.1899999976158142)})
    id: node

    PrincipledMaterial {
        id: it_dark_material
        objectName: "it_dark"
        baseColor: "#ff3f454b"
        metalness: 0.699999988079071
        roughness: 0.3499999940395355
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: cabinet_steel_material
        objectName: "cabinet_steel"
        baseColor: "#ff99a2ab"
        metalness: 0.75
        roughness: 0.3499999940395355
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: alarm_green_material
        objectName: "alarm_green"
        baseColor: "#ff27dc76"
        metalness: 0.10000000149011612
        roughness: 0.25
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: alarm_yellow_material
        objectName: "alarm_yellow"
        baseColor: "#fff9d727"
        metalness: 0.10000000149011612
        roughness: 0.25
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: alarm_red_material
        objectName: "alarm_red"
        baseColor: "#fff33030"
        metalness: 0.10000000149011612
        roughness: 0.25
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: screen_blue_material
        objectName: "screen_blue"
        baseColor: "#ff27a0e0"
        metalness: 0.20000000298023224
        roughness: 0.25
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: cabinet_base
            objectName: "cabinet_base"
            position: Qt.vector3d(0, -0.02, 0)
            source: "meshes/cube_003_mesh.mesh"
            materials: [
                it_dark_material
            ]
        }
        Model {
            id: cabinet_body
            objectName: "cabinet_body"
            position: Qt.vector3d(0, 0.34, 0)
            source: "meshes/cube_mesh.mesh"
            materials: [
                cabinet_steel_material
            ]
        }
        Model {
            id: cabinet_door
            objectName: "cabinet_door"
            position: Qt.vector3d(0.17, 0.34, 0)
            source: "meshes/cube_001_mesh.mesh"
            materials: [
                it_dark_material
            ]
        }
        Model {
            id: cabinet_lamp_0
            objectName: "cabinet_lamp_0"
            position: Qt.vector3d(0.205, 0.25, 0.08)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_mesh.mesh"
            materials: [
                alarm_green_material
            ]
        }
        Model {
            id: cabinet_lamp_1
            objectName: "cabinet_lamp_1"
            position: Qt.vector3d(0.205, 0.25, 0.03)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_001_mesh.mesh"
            materials: [
                alarm_yellow_material
            ]
        }
        Model {
            id: cabinet_lamp_2
            objectName: "cabinet_lamp_2"
            position: Qt.vector3d(0.205, 0.25, -0.02)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_002_mesh.mesh"
            materials: [
                alarm_red_material
            ]
        }
        Model {
            id: cabinet_screen
            objectName: "cabinet_screen"
            position: Qt.vector3d(0.19, 0.44, 0)
            source: "meshes/cube_002_mesh.mesh"
            materials: [
                screen_blue_material
            ]
        }
    }

}
