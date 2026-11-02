import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.17999997735023499, -0.39499998092651367, -0.30000001192092896), maximum: Qt.vector3d(0.7799999713897705, -0.22750000655651093, 0.30000001192092896)})
    id: node

    PrincipledMaterial {
        id: safety_yellow_material
        objectName: "safety_yellow"
        baseColor: "#fff9c845"
        metalness: 0.25
        roughness: 0.4000000059604645
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: base_dark_material
        objectName: "base_dark"
        baseColor: "#ff555d69"
        metalness: 0.75
        roughness: 0.3199999928474426
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

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: anchor_bolt_0_02_0_24
            objectName: "anchor_bolt_0.02_0.24"
            position: Qt.vector3d(0.1, -0.245, -0.24)
            source: "meshes/cylinder_001_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: anchor_bolt_0_02__0_24
            objectName: "anchor_bolt_0.02_-0.24"
            position: Qt.vector3d(0.1, -0.245, 0.24)
            source: "meshes/cylinder_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: anchor_bolt_0_58_0_24
            objectName: "anchor_bolt_0.58_0.24"
            position: Qt.vector3d(0.66, -0.245, -0.24)
            source: "meshes/cylinder_003_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: anchor_bolt_0_58__0_24
            objectName: "anchor_bolt_0.58_-0.24"
            position: Qt.vector3d(0.66, -0.245, 0.24)
            source: "meshes/cylinder_002_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: station_base
            objectName: "station_base"
            position: Qt.vector3d(0.3, -0.35, 0)
            source: "meshes/cube_mesh.mesh"
            materials: [
                base_dark_material
            ]
        }
        Model {
            id: station_rails
            objectName: "station_rails"
            position: Qt.vector3d(0.3, -0.28, 0.24)
            source: "meshes/cube_001_mesh.mesh"
            materials: [
                steel_material
            ]
        }
    }

}
