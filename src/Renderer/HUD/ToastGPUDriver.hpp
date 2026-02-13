/// @file ToastGPUDriver.hpp
/// @brief OpenGL GPU Driver implementation for Ultralight in Toast Engine
/// @author dario
/// @date 12/02/2026.

#pragma once

#include <Ultralight/platform/GPUDriver.h>
#include <Ultralight/Geometry.h>
#include <Ultralight/Matrix.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <map>
#include <mutex>

#define ENABLE_OFFSCREEN_GL 0

namespace toast::hud {

// Forward declaration
class ToastGPUContext;

/// Program type alias for shader types
using ProgramType = ultralight::ShaderType;

///
/// @class ToastGPUDriver
/// @brief OpenGL implementation of Ultralight's GPUDriver interface
///
/// This class implements Ultralight's GPUDriver interface using OpenGL.
/// It handles texture management, render buffer (FBO) management,
/// geometry (VAO/VBO) management, and shader programs.
///
/// @note This implementation supports both single-sample and MSAA rendering.
///
class ToastGPUDriver : public ultralight::GPUDriver {
public:
    ///
    /// @brief Construct a new GPU driver
    /// @param context The GPU context that owns this driver
    ///
    explicit ToastGPUDriver(ToastGPUContext* context);
    
    ~ToastGPUDriver() override;
    
    /// @brief Get the driver name
    const char* name() const { return "Toast OpenGL"; }
	
    
    void BeginSynchronize() override { }
    void EndSynchronize() override { }

    uint32_t NextTextureId() override { 
        uint32_t id = next_texture_id_++;
        // Uncomment for debugging texture ID allocation:
        // TOAST_INFO("[GPU] NextTextureId allocated: {}", id);
        return id;
    }
    void CreateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
    void UpdateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
    void DestroyTexture(uint32_t texture_id) override;

    uint32_t NextRenderBufferId() override { return next_render_buffer_id_++; }
    void CreateRenderBuffer(uint32_t render_buffer_id, const ultralight::RenderBuffer& buffer) override;
    void DestroyRenderBuffer(uint32_t render_buffer_id) override;

    uint32_t NextGeometryId() override { return next_geometry_id_++; }
    void CreateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer& vertices,
                       const ultralight::IndexBuffer& indices) override;
    void UpdateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer& vertices,
                       const ultralight::IndexBuffer& indices) override;
    void DestroyGeometry(uint32_t geometry_id) override;

    void UpdateCommandList(const ultralight::CommandList& list) override;
	

    /// @brief Execute all pending draw commands
    void DrawCommandList();
    
    /// @brief Get the OpenGL texture ID for an Ultralight texture
    /// @param ultralight_texture_id The Ultralight texture ID
    /// @return The OpenGL texture ID, or 0 if not found
    GLuint GetTextureGL(uint32_t ultralight_texture_id) const;
    
    /// @brief Get the OpenGL texture ID for an Ultralight texture, resolving MSAA if needed
    /// @param ultralight_texture_id The Ultralight texture ID
    /// @return The OpenGL texture ID, or 0 if not found
    GLuint GetTextureGLResolved(uint32_t ultralight_texture_id);
    
    /// @brief Bind an Ultralight texture for use in rendering
    void BindUltralightTexture(uint32_t ultralight_texture_id);
    
    /// @brief Bind a texture to a specific texture unit
    void BindTexture(uint8_t texture_unit, uint32_t texture_id);
    
    /// @brief Bind a render buffer (FBO) for rendering
    void BindRenderBuffer(uint32_t render_buffer_id);
    
    /// @brief Clear a render buffer
    void ClearRenderBuffer(uint32_t render_buffer_id);
    
    /// @brief Load all shader programs
    void LoadPrograms();
    
    /// @brief Destroy all shader programs
    void DestroyPrograms();

private:

    /// Texture entry for tracking OpenGL textures
    struct TextureEntry {
        GLuint tex_id = 0;          ///< GL Texture ID
        GLuint msaa_tex_id = 0;     ///< MSAA Texture ID (if MSAA enabled)
        uint32_t render_buffer_id = 0; ///< Associated render buffer (if RTT)
        GLuint width = 0;           ///< Texture width
        GLuint height = 0;          ///< Texture height
        bool is_sRGB = false;       ///< Whether texture is sRGB
    };

    /// FBO entry for a specific GL context
    struct FBOEntry {
        GLuint fbo_id = 0;          ///< FBO ID for resolve
        GLuint msaa_fbo_id = 0;     ///< MSAA FBO ID
        bool needs_resolve = false;  ///< Whether MSAA resolve is needed
    };

    /// Render buffer entry
    struct RenderBufferEntry {
        std::map<GLFWwindow*, FBOEntry> fbo_map; ///< FBOs per GL context
        uint32_t texture_id = 0;    ///< Backing texture ID
#if ENABLE_OFFSCREEN_GL
        ultralight::RefPtr<ultralight::Bitmap> bitmap;
        GLuint pbo_id = 0;
        bool is_bitmap_dirty = false;
        bool is_first_draw = true;
        bool needs_update = false;
#endif
    };

    /// Geometry entry for VAO/VBO management
    struct GeometryEntry {
        std::map<GLFWwindow*, GLuint> vao_map; ///< VAOs per GL context
        ultralight::VertexBufferFormat vertex_format;
        GLuint vbo_vertices = 0;    ///< VBO for vertices
        GLuint vbo_indices = 0;     ///< VBO for indices
    };

    /// Shader program entry
    struct ProgramEntry {
        GLuint program_id = 0;
        GLuint vert_shader_id = 0;
        GLuint frag_shader_id = 0;
    };
	

    void LoadProgram(ProgramType type);
    void SelectProgram(ProgramType type);
    void UpdateUniforms(const ultralight::GPUState& state);
    
    void SetUniform1ui(const char* name, GLuint val);
    void SetUniform1f(const char* name, float val);
    void SetUniform1fv(const char* name, size_t count, const float* val);
    void SetUniform4f(const char* name, const float val[4]);
    void SetUniform4fv(const char* name, size_t count, const float* val);
    void SetUniformMatrix4fv(const char* name, size_t count, const float* val);
    
    void SetViewport(uint32_t width, uint32_t height);
    ultralight::Matrix ApplyProjection(const ultralight::Matrix4x4& transform, 
                                        float screen_width, float screen_height, bool flip_y);
    
    void CreateFBOTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap);
    void CreateFBOIfNeededForActiveContext(uint32_t render_buffer_id);
    void CreateVAOIfNeededForActiveContext(uint32_t geometry_id);
    void ResolveIfNeeded(uint32_t render_buffer_id);
    void MakeTextureSRGBIfNeeded(uint32_t texture_id);
    
    void DrawGeometry(uint32_t geometry_id, uint32_t indices_count,
                     uint32_t indices_offset, const ultralight::GPUState& state);

#if ENABLE_OFFSCREEN_GL
    void UpdateBitmap(RenderBufferEntry& entry, GLuint pbo_id);
#endif
	

    ToastGPUContext* context_ = nullptr;
    
    uint32_t next_texture_id_ = 1;
    uint32_t next_render_buffer_id_ = 1;
    uint32_t next_geometry_id_ = 1;
    
    std::map<uint32_t, TextureEntry> texture_map_;
    std::map<uint32_t, RenderBufferEntry> render_buffer_map_;
    std::map<uint32_t, GeometryEntry> geometry_map_;
    std::map<ProgramType, ProgramEntry> programs_;
    
    std::vector<ultralight::Command> command_list_;
    std::mutex command_list_mutex_;  ///< Mutex for thread-safe command list access
    
    GLuint cur_program_id_ = 0;
    uint32_t batch_count_ = 0;
    
    /// Fallback 1x1 white texture for missing texture slots
    GLuint fallback_texture_id_ = 0;
};

} // namespace toast::hud
