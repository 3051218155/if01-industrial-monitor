import QtQuick
import QtQuick3D

Node {
    id: node

    // Resources
    PrincipledMaterial {
        id: principledMaterial
        metalness: 1
        roughness: 1
        alphaMode: PrincipledMaterial.Opaque
    }

    // Nodes:
    Model {
        id: bearing
        objectName: "bearing"
        source: "meshes/fillet16_001_mesh.mesh"
        materials: [
            principledMaterial
        ]
    }

    // Animations:
}
