/**
 * @file procedural_voxel.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief Voxel shapes built from an ordered list of volumes
 *
 * A ProceduralVoxel rebuilds its shape from its children, top to bottom
 * Groups are walked depth first and disabled children are skipped
 *
 * Every piece draws into its own grid with its first script's editShape
 * The grid is cached so moving, rotating or reordering pieces never runs a script again
 */

#pragma once
#include "voxel_bucket.hpp"
#include "voxel_group.hpp"
#include "voxel_node.hpp"
#include "voxel_piece.hpp"
#include "voxel_volume.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <toast/voxel/stamp.hpp>
#include <toast/voxel/voxel_edit.hpp>
#include <vector>

namespace event {
struct ProceduralVoxelLayout;
}

namespace toast {

/**
 * A voxel shape built from its FillVolume, CarveVolume, PaintVolume, VoxelMesh and VoxelGroup children
 * It still renders, collides and breaks like any VoxelNode
 */
class [[ToastNode, Color("Magenta")]] TOAST_API ProceduralVoxel : public VoxelNode {
public:
	struct PieceLayout {
		Box<Node> node;
		PieceKind kind = PieceKind::fill;
		voxel::LatticePlacement placement;
		voxel::EditBounds bounds;    ///< Where the grid lands, in shape voxels
		glm::vec4 average_color {0.0f};
	};

	/** Every piece the last rebuild used in order */
	[[nodiscard]]
	auto layout() const noexcept -> const std::vector<PieceLayout>& {
		return m_layout;
	}

	/** Rebuilds the shape from its pieces now */
	[[Reflect, Button("Rebuild")]]
	void rebuild();

	/** Bakes the whole shape to a .tvox and replaces this node with a VoxelNode */
	[[Reflect, Button("Bake"), EditorAction("voxel_editor.bake")]]
	void bakeVoxel() { }

	/** Tells the VoxelEditor where every piece is, it is also sent after every editor rebuild */
	void sendLayout();

	/**
	 * The piece whose colour shows at a voxel of the shape
	 * @returns nothing when no piece draws there
	 * @note In game the pieces redraw their grid the first time this reads them
	 */
	[[nodiscard, Reflect]]
	auto getPieceAt(glm::vec3 pos) -> Box<Node>;

	/** @returns how many pieces the last rebuild used */
	[[nodiscard, Reflect]]
	auto getPieceCount() const noexcept -> int {
		return static_cast<int>(m_layout.size());
	}

	/** The piece that draws the voxel at shape voxel, null when none does */
	[[nodiscard]]
	auto pieceAt(glm::ivec3 voxel) -> VoxelPiece*;

	/**
	 * Keeps the piece grids after a build even in game
	 * @note Off by default since in game the shape is built once and destruction owns it after that
	 */
	void keepGrids(bool keep) noexcept { m_keep_grids = keep; }

	struct Composed {
		voxel::Volume volume;
		glm::ivec3 origin {0};    ///< Shape voxel of the volume's first voxel
	};

	/**
	 * Builds only these pieces, in shape order, into a fresh volume
	 * @note Collapse bakes it into a model, the shape itself is untouched
	 */
	[[nodiscard]]
	auto composePieces(std::span<VoxelPiece* const> only) -> std::optional<Composed>;

protected:
	void buildShape() override;
	auto shapeKey() -> uint64_t override;

	/** Piece outlines and the Split and Slice preview for the VoxelEditor */
	void drawDebug() override;

	void end();
	void destroy();

private:
	struct Collected {
		VoxelPiece* piece = nullptr;
		glm::mat4 to_root {1.0f};
	};

	struct Landing {
		VoxelPiece* piece = nullptr;
		const voxel::Volume* grid = nullptr;
		voxel::LatticePlacement placement;
		voxel::PaletteRemapTable remap {};
	};

	void collect(Node& node, const glm::mat4& to_root, std::vector<Collected>& out);

	struct BucketFill {
		VoxelBucket* bucket = nullptr;
		glm::ivec3 seed {0};
	};

	/** Every enabled bucket in hierarchy order, groups included */
	void collectBuckets(Node& node, const glm::mat4& to_root, std::vector<BucketFill>& out);

	/** Builds the grid of every piece and works out where it lands */
	[[nodiscard]]
	auto land(const std::vector<Collected>& pieces) -> std::vector<Landing>;

	/** Writes one piece into target whose first voxel is the shape voxel origin */
	static auto apply(voxel::Volume& target, const Landing& landing, glm::ivec3 origin) -> voxel::EditResult;

	[[nodiscard]]
	auto averageColor(const Landing& landing) -> glm::vec3;

	[[nodiscard]]
	auto remapFor(const VoxelPiece& piece) -> voxel::PaletteRemapTable;

	/** Builds the box a 3D tool is drawing into the shape so you see what it does before placing it */
	auto applyToolPreview(voxel::Volume* target) -> voxel::Volume*;

	/** The selected piece voxels that still show, the renderer tints them orange */
	void rebuildHighlight(const voxel::Volume* target);

	/** Front, side and top views of the built shape for the 2D views */
	void addProjections(event::ProceduralVoxelLayout& layout);

	std::unique_ptr<voxel::Volume> m_highlight;

	std::vector<PieceLayout> m_layout;
	bool m_keep_grids = false;
};

/** @returns true when node sits in a ProceduralVoxel, through any groups */
[[nodiscard]]
TOAST_API auto insideProceduralVoxel(Node& node) -> bool;

/** The ProceduralVoxel node is built into, through any groups, null when there is none */
[[nodiscard]]
TOAST_API auto owningProceduralVoxel(Node& node) -> ProceduralVoxel*;

// Placing pieces, everything in shape voxels

/** The transform from node's parent to its ProceduralVoxel, in metres */
[[nodiscard]]
TOAST_API auto parentToShape(Node& node) -> glm::mat4;

/** Where the piece grid lands on the shape, snapped to the lattice */
[[nodiscard]]
TOAST_API auto piecePlacement(VoxelPiece& piece) -> voxel::LatticePlacement;

/** The box the piece covers on the shape, its size and not the brick rounded grid */
[[nodiscard]]
TOAST_API auto pieceBounds(VoxelPiece& piece) -> voxel::EditBounds;

/**
 * Moves and resizes piece so it covers min..max with orientation
 * @note A VoxelMesh cannot resize so it keeps its size with its corner at min
 */
TOAST_API void placePiece(VoxelPiece& piece, const voxel::LatticeOrientation& orientation, glm::ivec3 min, glm::ivec3 max);

/** A clip plane from piece voxels to shape voxels */
[[nodiscard]]
TOAST_API auto planeToShape(glm::vec4 plane, const voxel::LatticePlacement& placement) noexcept -> glm::vec4;

/** A clip plane from shape voxels to piece voxels */
[[nodiscard]]
TOAST_API auto planeToPiece(glm::vec4 plane, const voxel::LatticePlacement& placement) noexcept -> glm::vec4;

}
