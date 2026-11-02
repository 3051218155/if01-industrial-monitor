import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.07000000029802322, -0.04749999940395355, -0.07000000029802322), maximum: Qt.vector3d(0.07000000029802322, 0.4025000035762787, 0.07000000029802322)})
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
        id: alarm_green_material
        objectName: "alarm_green"
        baseColor: "#ff27dc76"
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
        id: alarm_yellow_material
        objectName: "alarm_yellow"
        baseColor: "#fff9d727"
        metalness: 0.10000000149011612
        roughness: 0.25
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: alarm_base
            objectName: "alarm_base"
            position: Qt.vector3d(0, -0.03, 0)
            source: "meshes/cylinder_mesh.mesh"
            materials: [
                it_dark_material
            ]
        }
        Model {
            id: alarm_green
            objectName: "alarm_green"
            position: Qt.vector3d(0, 0.2, 0)
            source: "meshes/cylinder_002_mesh.mesh"
            materials: [
                alarm_green_material
            ]
        }
        Model {
            id: alarm_pole
            objectName: "alarm_pole"
            position: Qt.vector3d(0, 0.08, 0)
            source: "meshes/cylinder_001_mesh.mesh"
            materials: [
                it_dark_material
            ]
        }
        Model {
            id: alarm_red
            objectName: "alarm_red"
            position: Qt.vector3d(0, 0.36, 0)
            source: "meshes/cylinder_004_mesh.mesh"
            materials: [
                alarm_red_material
            ]
        }
        Model {
            id: alarm_yellow
            objectName: "alarm_yellow"
            position: Qt.vector3d(0, 0.28, 0)
            source: "meshes/cylinder_003_mesh.mesh"
            materials: [
                alarm_yellow_material
            ]
        }
    }

}
