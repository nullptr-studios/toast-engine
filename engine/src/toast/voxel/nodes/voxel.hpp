/**
 * @file voxel.hpp
 * @author Xein
 * @date 27 Sep 2026
 * @brief Common base of every voxel node
 */

#pragma once
#include <toast/world/node_3d.hpp>

namespace toast {

/** Groups VoxelNode, VoxelPiece, VoxelBucket and VoxelGroup, it does nothing on its own */
class [[ToastNode, Hidden, Interface]] TOAST_API Voxel : public Node3D { };

}
