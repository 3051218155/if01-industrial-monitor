import QtQuick
import QtQuick3D

Node {
    readonly property var assemblyBounds: ({minimum: Qt.vector3d(-0.09000003337860107, -0.2849999964237213, -0.22200000286102295), maximum: Qt.vector3d(0.690000057220459, 0.257999986410141, 0.2240000069141388)})
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
        id: gear_brass_material
        objectName: "gear_brass"
        baseColor: "#ffe0c376"
        metalness: 0.800000011920929
        roughness: 0.25
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: gearbox_green_material
        objectName: "gearbox_green"
        baseColor: "#ff59a686"
        metalness: 0.44999998807907104
        roughness: 0.3499999940395355
        cullMode: PrincipledMaterial.NoCulling
        alphaMode: PrincipledMaterial.Opaque
    }

    Node {
        id: root
        objectName: "ROOT"
        Model {
            id: gearbox_base
            objectName: "gearbox_base"
            position: Qt.vector3d(0.32, -0.25, 0)
            source: "meshes/cube_013_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_bearing
            objectName: "gearbox_bearing"
            position: Qt.vector3d(0.14, 0.02, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_027_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: gearbox_cover_bolt_0_5_0_18
            objectName: "gearbox_cover_bolt_0.5_0.18"
            position: Qt.vector3d(0.5, 0.231, -0.18)
            source: "meshes/cylinder_004_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_cover_bolt_0_5__0_18
            objectName: "gearbox_cover_bolt_0.5_-0.18"
            position: Qt.vector3d(0.5, 0.231, 0.18)
            source: "meshes/cylinder_003_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_cover_bolt_0_14_0_18
            objectName: "gearbox_cover_bolt_0.14_0.18"
            position: Qt.vector3d(0.14, 0.231, -0.18)
            source: "meshes/cylinder_002_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_cover_bolt_0_14__0_18
            objectName: "gearbox_cover_bolt_0.14_-0.18"
            position: Qt.vector3d(0.14, 0.231, 0.18)
            source: "meshes/cylinder_001_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_0
            objectName: "gearbox_end_bolt_0"
            position: Qt.vector3d(0.061, 0, -0.143)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_006_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_1
            objectName: "gearbox_end_bolt_1"
            position: Qt.vector3d(0.061, 0.101116, -0.101116)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_007_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_2
            objectName: "gearbox_end_bolt_2"
            position: Qt.vector3d(0.061, 0.143, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_008_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_3
            objectName: "gearbox_end_bolt_3"
            position: Qt.vector3d(0.061, 0.101116, 0.101116)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_009_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_4
            objectName: "gearbox_end_bolt_4"
            position: Qt.vector3d(0.061, 0, 0.143)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_010_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_5
            objectName: "gearbox_end_bolt_5"
            position: Qt.vector3d(0.061, -0.101116, 0.101116)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_011_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_6
            objectName: "gearbox_end_bolt_6"
            position: Qt.vector3d(0.061, -0.143, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_012_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_end_bolt_7
            objectName: "gearbox_end_bolt_7"
            position: Qt.vector3d(0.061, -0.101116, -0.101116)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_013_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_foot_0_5_0_12
            objectName: "gearbox_foot_0.5_0.12"
            position: Qt.vector3d(0.5, -0.21, -0.12)
            source: "meshes/cube_017_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: gearbox_foot_0_5__0_12
            objectName: "gearbox_foot_0.5_-0.12"
            position: Qt.vector3d(0.5, -0.21, 0.12)
            source: "meshes/cube_016_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: gearbox_foot_0_14_0_12
            objectName: "gearbox_foot_0.14_0.12"
            position: Qt.vector3d(0.14, -0.21, -0.12)
            source: "meshes/cube_015_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: gearbox_foot_0_14__0_12
            objectName: "gearbox_foot_0.14_-0.12"
            position: Qt.vector3d(0.14, -0.21, 0.12)
            source: "meshes/cube_014_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: gearbox_gear
            objectName: "gearbox_gear"
            position: Qt.vector3d(0.31, 0.03, 0.06)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_023_mesh.mesh"
            materials: [
                gear_brass_material
            ]
        }
        Model {
            id: gearbox_housing
            objectName: "gearbox_housing"
            position: Qt.vector3d(0.32, 0.02, 0)
            source: "meshes/cube_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_nameplate
            objectName: "gearbox_nameplate"
            position: Qt.vector3d(0.33, 0.14, 0.221)
            source: "meshes/cube_012_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_oil_cap
            objectName: "gearbox_oil_cap"
            position: Qt.vector3d(0.32, 0.242, 0)
            source: "meshes/cylinder_005_mesh.mesh"
            materials: [
                dark_material
            ]
        }
        Model {
            id: gearbox_output_bolt_0
            objectName: "gearbox_output_bolt_0"
            position: Qt.vector3d(0.584, 0, -0.078)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_015_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_bolt_1
            objectName: "gearbox_output_bolt_1"
            position: Qt.vector3d(0.584, 0.0551543, -0.0551543)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_016_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_bolt_2
            objectName: "gearbox_output_bolt_2"
            position: Qt.vector3d(0.584, 0.078, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_017_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_bolt_3
            objectName: "gearbox_output_bolt_3"
            position: Qt.vector3d(0.584, 0.0551543, 0.0551543)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_018_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_bolt_4
            objectName: "gearbox_output_bolt_4"
            position: Qt.vector3d(0.584, 0, 0.078)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_019_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_bolt_5
            objectName: "gearbox_output_bolt_5"
            position: Qt.vector3d(0.584, -0.0551543, 0.0551543)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_020_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_bolt_6
            objectName: "gearbox_output_bolt_6"
            position: Qt.vector3d(0.584, -0.078, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_021_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_bolt_7
            objectName: "gearbox_output_bolt_7"
            position: Qt.vector3d(0.584, -0.0551543, -0.0551543)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_022_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_output_flange
            objectName: "gearbox_output_flange"
            position: Qt.vector3d(0.556, 0.02, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_014_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_18_0_206
            objectName: "gearbox_rib_0.18_0.206"
            position: Qt.vector3d(0.18, -0.055, -0.206)
            source: "meshes/cube_003_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_18__0_206
            objectName: "gearbox_rib_0.18_-0.206"
            position: Qt.vector3d(0.18, -0.055, 0.206)
            source: "meshes/cube_002_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_25_0_206
            objectName: "gearbox_rib_0.25_0.206"
            position: Qt.vector3d(0.25, -0.055, -0.206)
            source: "meshes/cube_005_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_25__0_206
            objectName: "gearbox_rib_0.25_-0.206"
            position: Qt.vector3d(0.25, -0.055, 0.206)
            source: "meshes/cube_004_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_32_0_206
            objectName: "gearbox_rib_0.32_0.206"
            position: Qt.vector3d(0.32, -0.055, -0.206)
            source: "meshes/cube_007_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_32__0_206
            objectName: "gearbox_rib_0.32_-0.206"
            position: Qt.vector3d(0.32, -0.055, 0.206)
            source: "meshes/cube_006_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_39_0_206
            objectName: "gearbox_rib_0.39_0.206"
            position: Qt.vector3d(0.39, -0.055, -0.206)
            source: "meshes/cube_009_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_39__0_206
            objectName: "gearbox_rib_0.39_-0.206"
            position: Qt.vector3d(0.39, -0.055, 0.206)
            source: "meshes/cube_008_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_46_0_206
            objectName: "gearbox_rib_0.46_0.206"
            position: Qt.vector3d(0.46, -0.055, -0.206)
            source: "meshes/cube_011_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_rib_0_46__0_206
            objectName: "gearbox_rib_0.46_-0.206"
            position: Qt.vector3d(0.46, -0.055, 0.206)
            source: "meshes/cube_010_mesh.mesh"
            materials: [
                gearbox_green_material
            ]
        }
        Model {
            id: gearbox_shaft
            objectName: "gearbox_shaft"
            position: Qt.vector3d(0.54, 0.02, 0)
            rotation: Qt.quaternion(0.707107, 0, 0, -0.707107)
            scale: Qt.vector3d(1, 1, 1)
            source: "meshes/cylinder_025_mesh.mesh"
            materials: [
                steel_material
            ]
        }
        Model {
            id: gearbox_split_flange
            objectName: "gearbox_split_flange"
            position: Qt.vector3d(0.32, 0.08, 0)
            source: "meshes/cube_001_mesh.mesh"
            materials: [
                steel_material
            ]
        }
    }

}
