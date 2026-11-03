// Fragment stage of the near cell mesh shader backend in its own module since Intel drivers crash compiling it from the mesh module
import voxel;
import lighting;

[shader("fragment")]
[earlydepthstencil]
FSOutput meshQuadFragmentMain(QuadVSOutput input)
{
    return shadeQuad(input);
}
