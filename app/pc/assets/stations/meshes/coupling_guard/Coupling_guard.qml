import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.10000000894069672, -0.054999999701976776, -0.18000000715255737), maximum: Qt.vector3d(0.25999999046325684, 0.19999998807907104, 0.18000000715255737)})
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
        id: dark_material
        objectName: "dark"
        baseColor: "#ff455059"
        metalness: 0.6499999761581421
        roughness: 0.3799999952316284
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: coupling_guard_left
            objectName: "coupling_guard_left"
            position: Qt.vector3d(0.08, 0.08, 0.145)
            source: "meshes/cube_001_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: coupling_guard_mount
            objectName: "coupling_guard_mount"
            position: Qt.vector3d(0.08, -0.04, 0)
            source: "meshes/cube_003_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: coupling_guard_right
            objectName: "coupling_guard_right"
            position: Qt.vector3d(0.08, 0.08, -0.145)
            source: "meshes/cube_002_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: coupling_guard_top
            objectName: "coupling_guard_top"
            position: Qt.vector3d(0.08, 0.18, 0)
            source: "meshes/cube_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
    }

}
