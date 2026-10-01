/**
 * @file voxel_piece.hpp
 * @author Xein
 * @date 26 Sep 2026
 * @brief Base of everything a ProceduralVoxel builds itself from
 */

#pragma once
#include "voxel.hpp"

#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <toast/assets/types.hpp>
#include <toast/voxel/voxel_edit.hpp>
#include <vector>

namespace assets {
class VoxelModel;
}

namespace toast {

/** How a piece lands on the shape */
enum class PieceKind : uint8_t {
	fill,     ///< Adds its voxels with its write mode
	carve,    ///< Empties every voxel it has
	paint,    ///< Recolors voxels that are already solid
};

/**
 * Base of everything a ProceduralVoxel builds itself from
 *
 * Every piece draws into its own grid with its first script's editShape
 * The grid is cached so moving, rotating or reordering pieces never runs a script again
 *
 * Positions are in voxels from the piece corner and each voxel is 10 cm
 * The drawing functions only work while editShape runs
 */
class [[ToastNode, Hidden, Interface, Icon("Shape"), Color("Magenta")]] TOAST_API VoxelPiece : public Voxel {
public:
	// Drawing

	/** @returns the size of the grid in voxels */
	[[nodiscard, Reflect]]
	auto getSize() -> glm::vec3;

	/** @returns the palette ID at pos in the grid being drawn */
	[[nodiscard, Reflect]]
	auto getVoxel(glm::vec3 pos) -> int;

	[[Reflect]]
	void setVoxel(glm::vec3 pos, int id);

	[[Reflect]]
	void removeVoxel(glm::vec3 pos);

	[[Reflect]]
	void fillBox(glm::vec3 min, glm::vec3 max, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0);

	/** A box with its edges rounded by radius voxels */
	[[Reflect]]
	void fillRoundBox(
	    glm::vec3 min, glm::vec3 max, float radius, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0
	);

	[[Reflect]]
	void fillSphere(glm::vec3 center, float radius, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0);

	/** The ellipsoid that touches every face of the box from min to max */
	[[Reflect]]
	void fillEllipsoid(glm::vec3 min, glm::vec3 max, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0);

	[[Reflect]]
	void fillCylinder(
	    glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0
	);

	[[Reflect]]
	void fillCapsule(
	    glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0
	);

	[[Reflect]]
	void carveBox(glm::vec3 min, glm::vec3 max);

	[[Reflect]]
	void carveSphere(glm::vec3 center, float radius);

	[[Reflect]]
	void paintBox(glm::vec3 min, glm::vec3 max, int id);

	/**
	 * Drops a voxel model into the grid at pos
	 * @note Colors get matched to the ProceduralVoxel palette
	 */
	[[Reflect]]
	void stamp(
	    const assets::Handle<assets::VoxelModel>& asset, glm::vec3 pos, glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
	    voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0
	);

	/**
	 * Repeats the solid part of a voxel model across the whole grid
	 * @param fit 0 crop, 1 stretch, 2 center
	 * @param anchor Per axis 0 min, 1 center, 2 max
	 * @param offset Shifts the tiles by this many voxels
	 * @param mirror Per axis, non zero flips every other tile
	 */
	[[Reflect]]
	void tile(
	    const assets::Handle<assets::VoxelModel>& asset, int fit = 0, glm::vec3 anchor = glm::vec3(0.0f),
	    glm::vec3 offset = glm::vec3(0.0f), glm::vec3 mirror = glm::vec3(0.0f), voxel::WriteMode mode = voxel::WriteMode::replace,
	    int match_id = 0
	);

	// Piece

	/**
	 * Runs editShape again on the next rebuild
	 * @note Only needed when the script reads something the piece cannot see change
	 */
	[[Reflect]]
	void redraw();

	/** The script that draws the piece, the first of its scripts */
	[[nodiscard, Reflect]]
	auto getShapeScript() const -> const assets::Handle<assets::Script>&;

	/** Swaps the script that draws the piece and redraws it */
	[[Reflect]]
	void setShapeScript(assets::Handle<assets::Script> script);

	/** Clip planes in piece voxels, a voxel is kept when dot(xyz, centre) + w >= 0 */
	[[nodiscard, Reflect]]
	auto getClipPlanes() const -> std::vector<glm::vec4>;

	[[Reflect]]
	void setClipPlanes(std::vector<glm::vec4> planes);

	/** Keeps only the voxels on the side the normal in xyz points to */
	[[Reflect]]
	void addClipPlane(glm::vec4 plane);

	[[Reflect]]
	void clearClipPlanes();

	/**
	 * The box the piece covers on its ProceduralVoxel, in shape voxels
	 * @returns min then max or an empty list when it is not in a ProceduralVoxel
	 */
	[[nodiscard, Reflect]]
	auto getShapeBounds() -> std::vector<glm::vec3>;

	/** @returns the ProceduralVoxel the piece is built into, through any groups */
	[[nodiscard, Reflect]]
	auto getShape() -> Box<Node>;

	/** @returns true when the transform had to be snapped to the closest voxel and right angle */
	[[nodiscard, Reflect]]
	auto isMisaligned() const noexcept -> bool {
		return m_misaligned;
	}

	signals::Signal<> drawn;    ///< Sent after editShape drew the grid again

	                            // Backend

	[[nodiscard]]
	virtual auto kind() const noexcept -> PieceKind {
		return PieceKind::fill;
	}

	[[nodiscard]]
	virtual auto writeMode() const noexcept -> voxel::WriteMode {
		return voxel::WriteMode::replace;
	}

	[[nodiscard]]
	virtual auto matchId() const noexcept -> uint8_t {
		return 0;
	}

	/** The palette the grid ids come from, 0 when they already are ProceduralVoxel ids */
	[[nodiscard]]
	virtual auto sourcePaletteUid() const -> uint64_t {
		return 0;
	}

	struct Grid {
		std::shared_ptr<const Grid> base;
		std::unique_ptr<voxel::Volume> volume;
		std::array<uint32_t, voxel::k_palette_size> histogram {};
	};

	using GridRef = std::shared_ptr<const Grid>;

	/**
	 * The grid after the clip planes, rebuilt first when an input changed
	 * @returns null when there is nothing to draw
	 * @note Waits for a grid still drawing on another thread
	 */
	[[nodiscard]]
	auto grid() -> const voxel::Volume*;

	/** Like grid() but it keeps the grid alive while someone else reads it */
	[[nodiscard]]
	auto gridRef() -> GridRef;

	// Drawing on other threads
	// for the editor ONLY (dario im looking at you)

	/** Starts drawing the grid on a worker */
	void requestGrid();

	/** Takes the grid a worker finished */
	auto collectGrid() -> bool;

	/** The newest grid there is without drawing or waiting, it can be out of date or null */
	[[nodiscard]]
	auto readyGrid() -> GridRef;

	/** @returns true while a worker is drawing the grid */
	[[nodiscard]]
	auto gridBuilding() const noexcept -> bool {
		return m_job.valid();
	}

	/** @returns true when the grid is out of date, drawing or not */
	[[nodiscard]]
	auto gridStale() const noexcept -> bool {
		return m_grid_dirty || m_job.valid();
	}

	/** Blocks until the worker drawing the grid is done and takes its grid */
	void waitForGrid();

	/** Bumped whenever something that changes the grid or how it lands changes */
	[[nodiscard]]
	auto inputRevision() const noexcept -> uint32_t {
		return m_input_revision;
	}

	/** How many times the script grid was built, tests use it to know a move did not redraw */
	[[nodiscard]]
	auto buildCount() const noexcept -> uint32_t {
		return m_build_count;
	}

	[[nodiscard]]
	auto clipPlanes() const noexcept -> const std::vector<glm::vec4>& {
		return m_clip_planes;
	}

	/** The size in voxels the piece covers before any rotation */
	[[nodiscard]]
	auto nominalSize() -> glm::ivec3 {
		return pieceSize();
	}

	/** Volumes resize, meshes keep the size of their model */
	[[nodiscard]]
	virtual auto resizable() const noexcept -> bool {
		return false;
	}

	/** Frees the grids, the next grid() call rebuilds them */
	void releaseGrid();

	/** Set by the ProceduralVoxel when the transform had to be snapped to the voxel lattice */
	void setMisaligned(bool misaligned);

protected:
	/** The size scripts draw into, the grid itself is rounded up to whole bricks */
	[[nodiscard]]
	virtual auto pieceSize() -> glm::ivec3;

	/** A fresh grid for editShape to draw into */
	[[nodiscard]]
	virtual auto prepareGrid() -> std::unique_ptr<voxel::Volume> = 0;

	/** Runs after editShape */
	virtual void finishGrid(voxel::Volume& /*grid*/) { }

	/** The grid needs its script to run again */
	void markGridDirty() noexcept;

	/** Only how the grid lands changed */
	void markLandingDirty() noexcept { ++m_input_revision; }

	void onReflectedFieldChanged(std::string_view field_name) override;
	void onScriptsReloading() override;
	void onScriptsReloaded() override;
	void onScriptVarChanged(std::string_view path) override;
	void updateInspectorMessages() override;

	template<typename Kernel>
	void draw(std::string_view operation, Kernel&& kernel);

	/** The grid being drawn when the calling thread is the one drawing it */
	[[nodiscard]]
	auto drawing() const noexcept -> voxel::Volume*;

	void init();
	void destroy();

	/** The script that draws the piece, it mirrors the first entry of the node scripts */
	[[Reflect, Name("Script")]]
	assets::Handle<assets::Script> m_shape_script;

	/** Clip planes in piece voxels, a voxel is kept when dot(xyz, centre) + w >= 0 */
	[[Reflect, Name("Clip Planes"), Group("Clip")]]
	std::vector<glm::vec4> m_clip_planes;

private:
	/** Runs editShape into a fresh grid, on whatever thread calls it */
	[[nodiscard]]
	auto drawGrid() -> GridRef;

	/** Takes a freshly drawn grid */
	void landGrid(GridRef drawn);

	/** Clips the drawn grid when the planes changed */
	void applyClip();

	/** The grid the drawing thread is filling, else the newest one without ever waiting on a worker */
	[[nodiscard]]
	auto latestGrid() -> const voxel::Volume*;

	GridRef m_drawn;
	GridRef m_grid;
	std::future<GridRef> m_job;
	bool m_grid_dirty = true;
	bool m_clip_dirty = true;
	bool m_misaligned = false;
	bool m_warned_outside_edit = false;
	uint32_t m_input_revision = 1;
	uint32_t m_build_count = 0;
};

}
