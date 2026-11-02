import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.03999999165534973, -0.32499998807907104, -0.2680000066757202), maximum: Qt.vector3d(0.6220000386238098, 0.36900001764297485, 0.20000000298023224)})
    id: node
    property bool rotorRunning: false
    property int rotorPwm: 0
    property int rotorDirection: 0

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
        id: pump_blue_material
        objectName: "pump_blue"
        baseColor: "#ff5090ce"
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

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: cover_bolt_0
            objectName: "cover_bolt_0"
            position: Qt.vector3d(0.469, 0, -0.127)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_018_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: cover_bolt_1
            objectName: "cover_bolt_1"
            position: Qt.vector3d(0.469, 0.0898026, -0.0898026)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_019_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: cover_bolt_2
            objectName: "cover_bolt_2"
            position: Qt.vector3d(0.469, 0.127, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_020_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: cover_bolt_3
            objectName: "cover_bolt_3"
            position: Qt.vector3d(0.469, 0.0898026, 0.0898026)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_021_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: cover_bolt_4
            objectName: "cover_bolt_4"
            position: Qt.vector3d(0.469, 0, 0.127)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_022_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: cover_bolt_5
            objectName: "cover_bolt_5"
            position: Qt.vector3d(0.469, -0.0898026, 0.0898026)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_023_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: cover_bolt_6
            objectName: "cover_bolt_6"
            position: Qt.vector3d(0.469, -0.127, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_024_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: cover_bolt_7
            objectName: "cover_bolt_7"
            position: Qt.vector3d(0.469, -0.0898026, -0.0898026)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_025_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: inlet_bolt_0
            objectName: "inlet_bolt_0"
            position: Qt.vector3d(0.615, 0, -0.084)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_001_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: inlet_bolt_1
            objectName: "inlet_bolt_1"
            position: Qt.vector3d(0.615, 0.059397, -0.059397)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_002_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: inlet_bolt_2
            objectName: "inlet_bolt_2"
            position: Qt.vector3d(0.615, 0.084, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_003_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: inlet_bolt_3
            objectName: "inlet_bolt_3"
            position: Qt.vector3d(0.615, 0.059397, 0.059397)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_004_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: inlet_bolt_4
            objectName: "inlet_bolt_4"
            position: Qt.vector3d(0.615, 0, 0.084)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_005_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: inlet_bolt_5
            objectName: "inlet_bolt_5"
            position: Qt.vector3d(0.615, -0.059397, 0.059397)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_006_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: inlet_bolt_6
            objectName: "inlet_bolt_6"
            position: Qt.vector3d(0.615, -0.084, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_007_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: inlet_bolt_7
            objectName: "inlet_bolt_7"
            position: Qt.vector3d(0.615, -0.059397, -0.059397)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_008_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_0
            objectName: "outlet_bolt_0"
            position: Qt.vector3d(0.4, 0.36, -0.17)
            source: "meshes/cylinder_010_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_1
            objectName: "outlet_bolt_1"
            position: Qt.vector3d(0.376569, 0.36, -0.226569)
            source: "meshes/cylinder_011_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_2
            objectName: "outlet_bolt_2"
            position: Qt.vector3d(0.32, 0.36, -0.25)
            source: "meshes/cylinder_012_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_3
            objectName: "outlet_bolt_3"
            position: Qt.vector3d(0.263431, 0.36, -0.226569)
            source: "meshes/cylinder_013_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_4
            objectName: "outlet_bolt_4"
            position: Qt.vector3d(0.24, 0.36, -0.17)
            source: "meshes/cylinder_014_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_5
            objectName: "outlet_bolt_5"
            position: Qt.vector3d(0.263431, 0.36, -0.113431)
            source: "meshes/cylinder_015_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_6
            objectName: "outlet_bolt_6"
            position: Qt.vector3d(0.32, 0.36, -0.09)
            source: "meshes/cylinder_016_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: outlet_bolt_7
            objectName: "outlet_bolt_7"
            position: Qt.vector3d(0.376569, 0.36, -0.113431)
            source: "meshes/cylinder_017_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: pump_base
            objectName: "pump_base"
            position: Qt.vector3d(0.12, -0.24, 0.11)
            source: "meshes/pump_base_mesh.mesh"
            materials: [
                dark_material,
                steel_material
            ]
        }
        Model {
            id: pump_bearing
            objectName: "pump_bearing"
            position: Qt.vector3d(0.08, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_028_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: pump_body
            objectName: "pump_body"
            source: "meshes/pump_body_mesh.mesh"
            materials: [
                pump_blue_material
            ]
        }
        Model {
            id: pump_front_cover
            objectName: "pump_front_cover"
            position: Qt.vector3d(0.448, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_mesh.mesh"
            materials: [
                pump_blue_material
            ]
        }
        Model {
            id: pump_impeller
            objectName: "pump_impeller"
            position: Qt.vector3d(0.28, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_026_mesh.mesh"
            materials: [
                safety_yellow_material
            ]
        }
        Model {
            id: pump_inlet
            objectName: "pump_inlet"
            source: "meshes/pump_inlet_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pump_inlet_flange
            objectName: "pump_inlet_flange"
            source: "meshes/pump_inlet_flange_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pump_outlet_flange
            objectName: "pump_outlet_flange"
            position: Qt.vector3d(0.32, 0.34, -0.17)
            rotation: Qt.quaternion(0.707107, 0, 0, 0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/pump_outlet_flange_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: pump_outlet_neck
            objectName: "pump_outlet_neck"
            position: Qt.vector3d(0.32, 0.2, -0.17)
            source: "meshes/cylinder_009_mesh.mesh"
            materials: [
                pump_blue_material
            ]
        }
        Model {
            id: pump_shaft
            objectName: "pump_shaft"
            position: Qt.vector3d(0.18, 0, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_027_mesh.mesh"
            materials: [
                steel_material
            ]
        }
    }

}
