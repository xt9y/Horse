#include <cstdio>

#define main horse_fbx_semantics_original_main
#include "fbx_semantics_contract.cpp"
#undef main

int main()
{
    std::fprintf(stderr, "[semantic] concave-ngon\n");
    testConcaveNgon();
    std::fprintf(stderr, "[semantic] mirrored-normal\n");
    testMirroredNonUniformNormal();
    std::fprintf(stderr, "[semantic] material-slots\n");
    testPerPolygonMaterials();
    std::fprintf(stderr, "[semantic] invalid-layer\n");
    testInvalidLayerFails();
    std::fprintf(stderr, "[semantic] external-texture\n");
    testExternalTextureGraph();
    std::fprintf(stderr, "[semantic] bad-texture\n");
    testExistingBadTextureFails();
    std::fprintf(stderr, "[semantic] inheritance\n");
    testInheritanceModes();
    std::fprintf(stderr, "[semantic] geometric-parent\n");
    testGeometricTransformDoesNotInherit();
    std::fprintf(stderr, "[semantic] skin\n");
    testSkinWeightsAndInvalidCluster();
    std::fprintf(stderr, "[semantic] animation\n");
    testAnimationThroughHierarchy();
    std::fprintf(stderr, "[semantic] complete\n");
    return 0;
}
