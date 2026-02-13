/// @file ToastGPUDriver.cpp
/// @brief OpenGL GPU Driver implementation for Ultralight in Toast Engine
/// @author dario
/// @date 12/02/2026.

#include "ToastGPUDriver.hpp"
#include "ToastGPUContext.hpp"
#include "UltralightShaders.hpp"

#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>

#include <Ultralight/platform/Platform.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <set>
#include <tuple>

namespace toast::hud {


// GPU_INFO is disabled to avoid verbose per-frame logging
#define GPU_INFO(x)

#if _WIN32
    #define GPU_FATAL(x) { \
        std::stringstream str; \
        str << "[GPU ERROR] " << x; \
        TOAST_ERROR("{}", str.str()); \
        __debugbreak(); \
        std::exit(-1); \
    }
#else
    #define GPU_FATAL(x) { \
        std::stringstream str; \
        str << "[GPU ERROR] " << x; \
        TOAST_ERROR("{}", str.str()); \
        std::exit(-1); \
    }
#endif

#ifdef _DEBUG
    #define CHECK_GL() { \
        if (GLenum err = glGetError()) { \
            GPU_FATAL("GL Error: " << glErrorString(err)); \
        } \
    }
#else
    #define CHECK_GL()
#endif


inline const char* glErrorString(GLenum err) noexcept {
    switch (err) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
        case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        default: return "UNKNOWN ERROR";
    }
}

inline std::string GetShaderLog(GLuint shader_id) {
    GLint length, result;
    glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &length);
    std::string str(length, ' ');
    glGetShaderInfoLog(shader_id, static_cast<GLsizei>(str.length()), &result, &str[0]);
    return str;
}

inline std::string GetProgramLog(GLuint program_id) {
    GLint length, result;
    glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &length);
    std::string str(length, ' ');
    glGetProgramInfoLog(program_id, static_cast<GLsizei>(str.length()), &result, &str[0]);
    return str;
}

static GLuint LoadShaderFromSource(GLenum shader_type, const char* source, const char* name) {
    // Check that we have a valid GL context
    if (!glfwGetCurrentContext()) {
        GPU_FATAL("No GL context current when loading shader: " << name);
    }
    
    // Clear any pending GL errors
    while (glGetError() != GL_NO_ERROR) {}
    
    GLuint shader_id = glCreateShader(shader_type);
    if (shader_id == 0) {
        GLenum err = glGetError();
        GPU_FATAL("glCreateShader failed for: " << name << "\n\tError: " << glErrorString(err));
    }
    
    glShaderSource(shader_id, 1, &source, nullptr);
    glCompileShader(shader_id);
    
    GLint compileStatus;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        std::string log = GetShaderLog(shader_id);
        glDeleteShader(shader_id);
        GPU_FATAL("Unable to compile shader: " << name << "\n\tLog: " << log);
    }
    return shader_id;
}


ToastGPUDriver::ToastGPUDriver(ToastGPUContext* context)
    : context_(context) {
    
    // Pre-load shader programs so they're ready when Ultralight needs them
    LoadPrograms();
    
    // Create a fallback 1x1 white texture for missing texture slots
    // This prevents rendering issues when textures are not yet loaded
    glGenTextures(1, &fallback_texture_id_);
    glBindTexture(GL_TEXTURE_2D, fallback_texture_id_);
    uint32_t white_pixel = 0xFFFFFFFF; // RGBA white
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    TOAST_TRACE("[GPU] Created fallback white texture (GL: {})", fallback_texture_id_);
    
    TOAST_TRACE("ToastGPUDriver initialized");
}

ToastGPUDriver::~ToastGPUDriver() {
    DestroyPrograms();
    if (fallback_texture_id_) {
        glDeleteTextures(1, &fallback_texture_id_);
        fallback_texture_id_ = 0;
    }
    TOAST_TRACE("ToastGPUDriver destroyed");
}


void ToastGPUDriver::CreateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    // Ensure correct GL context is current
    glfwMakeContextCurrent(context_->active_window());
    
    if (bitmap->IsEmpty()) {
        CreateFBOTexture(texture_id, bitmap);
        return;
    }

    CHECK_GL();
    
    TextureEntry& entry = texture_map_[texture_id];
    glGenTextures(1, &entry.tex_id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, entry.tex_id);
    
    CHECK_GL();
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    CHECK_GL();
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bitmap->row_bytes() / bitmap->bpp());
    
    CHECK_GL();
    
    if (bitmap->format() == ultralight::BitmapFormat::A8_UNORM) {
        const void* pixels = bitmap->LockPixels();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, bitmap->width(), bitmap->height(), 0,
                    GL_RED, GL_UNSIGNED_BYTE, pixels);
        bitmap->UnlockPixels();
        
        // A8 textures are used for font glyphs - don't generate mipmaps
        // and use LINEAR filtering for smooth text
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        // The shader's fillGlyph() reads the glyph coverage from texture.r directly
        // so we DON'T need any swizzle - just keep R8 as-is
        // The shader handles the alpha multiplication with vertex color
        
        entry.width = bitmap->width();
        entry.height = bitmap->height();
        
        return; // Skip mipmap generation for A8 textures
    } else if (bitmap->format() == ultralight::BitmapFormat::BGRA8_UNORM_SRGB) {
        const void* pixels = bitmap->LockPixels();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bitmap->width(), bitmap->height(), 0,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        bitmap->UnlockPixels();
    } else {
        GPU_FATAL("Unhandled texture format: " << static_cast<int>(bitmap->format()));
    }

    CHECK_GL();
    glGenerateMipmap(GL_TEXTURE_2D);
    CHECK_GL();
    
    entry.width = bitmap->width();
    entry.height = bitmap->height();
}


void ToastGPUDriver::UpdateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    // Ensure correct GL context is current
    glfwMakeContextCurrent(context_->active_window());
    
    glActiveTexture(GL_TEXTURE0);
    auto it = texture_map_.find(texture_id);
    
    // If texture hasn't been created yet, create it using CreateTexture path
    if (it == texture_map_.end() || it->second.tex_id == 0) {
        // Forward to CreateTexture which will allocate and initialize the GL texture
        CreateTexture(texture_id, bitmap);
        return;
    }
    
    TextureEntry& entry = it->second;
    
    // Check if dimensions changed - if so, we need to reallocate
    bool needs_realloc = (entry.width != bitmap->width() || entry.height != bitmap->height());
    
    glBindTexture(GL_TEXTURE_2D, entry.tex_id);
    
    CHECK_GL();
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bitmap->row_bytes() / bitmap->bpp());

    if (!bitmap->IsEmpty()) {
        if (bitmap->format() == ultralight::BitmapFormat::A8_UNORM) {
            const void* pixels = bitmap->LockPixels();
            if (needs_realloc) {
                // Texture size changed - reallocate
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, bitmap->width(), bitmap->height(), 0,
                            GL_RED, GL_UNSIGNED_BYTE, pixels);
                entry.width = bitmap->width();
                entry.height = bitmap->height();
            } else {
                // Same size - use subimage for efficiency
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bitmap->width(), bitmap->height(),
                               GL_RED, GL_UNSIGNED_BYTE, pixels);
            }
            bitmap->UnlockPixels();
            // Don't generate mipmaps for A8 textures
        } else if (bitmap->format() == ultralight::BitmapFormat::BGRA8_UNORM_SRGB) {
            const void* pixels = bitmap->LockPixels();
            if (needs_realloc) {
                // Texture size changed - reallocate
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bitmap->width(), bitmap->height(), 0,
                            GL_BGRA, GL_UNSIGNED_BYTE, pixels);
                entry.width = bitmap->width();
                entry.height = bitmap->height();
            } else {
                // Same size - use subimage for efficiency
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bitmap->width(), bitmap->height(),
                               GL_BGRA, GL_UNSIGNED_BYTE, pixels);
            }
            bitmap->UnlockPixels();
            
            CHECK_GL();
            glGenerateMipmap(GL_TEXTURE_2D);
        } else {
            GPU_FATAL("Unhandled texture format: " << static_cast<int>(bitmap->format()));
        }
    }

    CHECK_GL();
}

void ToastGPUDriver::BindTexture(uint8_t texture_unit, uint32_t texture_id) {
    glActiveTexture(GL_TEXTURE0 + texture_unit);
    
    if (texture_id == 0) {
        // Bind fallback texture instead of nothing
        glBindTexture(GL_TEXTURE_2D, fallback_texture_id_);
        return;
    }
    
    auto it = texture_map_.find(texture_id);
    if (it == texture_map_.end()) {
        // Log warning about missing texture
        static std::set<uint32_t> warned_missing;
        if (warned_missing.find(texture_id) == warned_missing.end()) {
            TOAST_WARN("[GPU] BindTexture: texture_id {} not found in texture_map_! (unit {}) - using fallback", texture_id, texture_unit);
            warned_missing.insert(texture_id);
        }
        // Bind fallback texture instead of nothing
        glBindTexture(GL_TEXTURE_2D, fallback_texture_id_);
        return;
    }
    
    TextureEntry& entry = it->second;
    
    // Verify the GL texture is valid
    if (entry.tex_id == 0 || !glIsTexture(entry.tex_id)) {
        static std::set<uint32_t> warned_invalid;
        if (warned_invalid.find(texture_id) == warned_invalid.end()) {
            TOAST_WARN("[GPU] BindTexture: texture_id {} has invalid GL texture {} (unit {}) - using fallback", texture_id, entry.tex_id, texture_unit);
            warned_invalid.insert(texture_id);
        }
        // Bind fallback texture instead of nothing
        glBindTexture(GL_TEXTURE_2D, fallback_texture_id_);
        return;
    }
    
    ResolveIfNeeded(entry.render_buffer_id);
    glBindTexture(GL_TEXTURE_2D, entry.tex_id);
    
    // Only set filter/wrap params, don't touch swizzle (it's set at creation time)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    CHECK_GL();
}

void ToastGPUDriver::DestroyTexture(uint32_t texture_id) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    // Ensure correct GL context is current
    glfwMakeContextCurrent(context_->active_window());
    
    TextureEntry& entry = texture_map_[texture_id];
    glDeleteTextures(1, &entry.tex_id);
    CHECK_GL();
    if (entry.msaa_tex_id) {
        glDeleteTextures(1, &entry.msaa_tex_id);
    }
    CHECK_GL();
    texture_map_.erase(texture_id);
}

void ToastGPUDriver::BindUltralightTexture(uint32_t ultralight_texture_id) {
    auto it = texture_map_.find(ultralight_texture_id);
    if (it == texture_map_.end()) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }
    
    TextureEntry& entry = it->second;
    ResolveIfNeeded(entry.render_buffer_id);
    glBindTexture(GL_TEXTURE_2D, entry.tex_id);
    CHECK_GL();
}


void ToastGPUDriver::CreateRenderBuffer(uint32_t render_buffer_id, const ultralight::RenderBuffer& buffer) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    // Ensure correct GL context is current
    glfwMakeContextCurrent(context_->active_window());
    
    if (render_buffer_id == 0) {
        return; // Render Buffer ID 0 is reserved for default framebuffer
    }

    RenderBufferEntry& entry = render_buffer_map_[render_buffer_id];
    entry.texture_id = buffer.texture_id;

    TextureEntry& textureEntry = texture_map_[buffer.texture_id];
    textureEntry.render_buffer_id = render_buffer_id;
}

void ToastGPUDriver::BindRenderBuffer(uint32_t render_buffer_id) {
    if (render_buffer_id == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    CreateFBOIfNeededForActiveContext(render_buffer_id);

    RenderBufferEntry& entry = render_buffer_map_[render_buffer_id];

    auto i = entry.fbo_map.find(glfwGetCurrentContext());
    if (i == entry.fbo_map.end()) {
        return;
    }

    auto& fbo_entry = i->second;

    if (context_->msaa_enabled()) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_entry.msaa_fbo_id);
        fbo_entry.needs_resolve = true;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_entry.fbo_id);
    }

    CHECK_GL();
}

void ToastGPUDriver::ClearRenderBuffer(uint32_t render_buffer_id) {
    glfwMakeContextCurrent(context_->active_window());

    BindRenderBuffer(render_buffer_id);
    glDisable(GL_SCISSOR_TEST);
    CHECK_GL();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    CHECK_GL();
    glClear(GL_COLOR_BUFFER_BIT);
    CHECK_GL();
}

void ToastGPUDriver::DestroyRenderBuffer(uint32_t render_buffer_id) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    if (render_buffer_id == 0) {
        return;
    }

    auto previous_context = glfwGetCurrentContext();

    RenderBufferEntry& entry = render_buffer_map_[render_buffer_id];
    for (auto& [context, fbo_entry] : entry.fbo_map) {
        glfwMakeContextCurrent(context);
        glDeleteFramebuffers(1, &fbo_entry.fbo_id);
        CHECK_GL();
        if (context_->msaa_enabled()) {
            glDeleteFramebuffers(1, &fbo_entry.msaa_fbo_id);
        }
        CHECK_GL();
    }

#if ENABLE_OFFSCREEN_GL
    if (entry.bitmap) {
        glDeleteBuffers(1, &entry.pbo_id);
    }
#endif
    CHECK_GL();
    render_buffer_map_.erase(render_buffer_id);

    glfwMakeContextCurrent(previous_context);
}


void ToastGPUDriver::CreateGeometry(uint32_t geometry_id,
                                    const ultralight::VertexBuffer& vertices,
                                    const ultralight::IndexBuffer& indices) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    // Ensure correct GL context is current
    glfwMakeContextCurrent(context_->active_window());
    
    GeometryEntry geometry;
    geometry.vertex_format = vertices.format;

    glGenBuffers(1, &geometry.vbo_vertices);
    glBindBuffer(GL_ARRAY_BUFFER, geometry.vbo_vertices);
    glBufferData(GL_ARRAY_BUFFER, vertices.size, vertices.data, GL_DYNAMIC_DRAW);
    CHECK_GL();

    glGenBuffers(1, &geometry.vbo_indices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.vbo_indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size, indices.data, GL_STATIC_DRAW);
    CHECK_GL();

    geometry_map_[geometry_id] = geometry;
}

void ToastGPUDriver::UpdateGeometry(uint32_t geometry_id,
                                    const ultralight::VertexBuffer& vertices,
                                    const ultralight::IndexBuffer& indices) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    // Ensure correct GL context is current
    glfwMakeContextCurrent(context_->active_window());
    
    GeometryEntry& geometry = geometry_map_[geometry_id];
    CHECK_GL();
    glBindBuffer(GL_ARRAY_BUFFER, geometry.vbo_vertices);
    glBufferData(GL_ARRAY_BUFFER, vertices.size, vertices.data, GL_DYNAMIC_DRAW);
    CHECK_GL();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.vbo_indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size, indices.data, GL_STATIC_DRAW);
    CHECK_GL();
}

void ToastGPUDriver::DestroyGeometry(uint32_t geometry_id) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    GeometryEntry& geometry = geometry_map_[geometry_id];
    CHECK_GL();
    glDeleteBuffers(1, &geometry.vbo_indices);
    glDeleteBuffers(1, &geometry.vbo_vertices);
    CHECK_GL();

    auto previous_context = glfwGetCurrentContext();

    for (auto& [context, vao_entry] : geometry.vao_map) {
        glfwMakeContextCurrent(context);
        glDeleteVertexArrays(1, &vao_entry);
        CHECK_GL();
    }

    CHECK_GL();
    geometry_map_.erase(geometry_id);

    glfwMakeContextCurrent(previous_context);
}


GLuint ToastGPUDriver::GetTextureGL(uint32_t ultralight_texture_id) const {
    auto it = texture_map_.find(ultralight_texture_id);
    if (it != texture_map_.end()) {
        return it->second.tex_id;
    }
    return 0;
}

GLuint ToastGPUDriver::GetTextureGLResolved(uint32_t ultralight_texture_id) {
    auto it = texture_map_.find(ultralight_texture_id);
    if (it != texture_map_.end()) {
        // Resolve MSAA if needed before returning the texture
        ResolveIfNeeded(it->second.render_buffer_id);
        return it->second.tex_id;
    }
    return 0;
}


void ToastGPUDriver::UpdateCommandList(const ultralight::CommandList& list) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    std::lock_guard<std::mutex> lock(command_list_mutex_);
    command_list_.clear();
    command_list_.reserve(list.size);
    for (uint32_t i = 0; i < list.size; ++i) {
        command_list_.push_back(list.commands[i]);
    }
}

void ToastGPUDriver::DrawCommandList() {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    // Copy command list under lock, then process outside the lock to avoid holding
    // the mutex during GL calls (which could be re-entrant via callbacks).
    std::vector<ultralight::Command> local_commands;
    {
        std::lock_guard<std::mutex> lock(command_list_mutex_);
        if (command_list_.empty()) {
            return;
        }
        local_commands = command_list_;
        command_list_.clear();
    }

    glfwMakeContextCurrent(context_->active_window());

    CHECK_GL();

    batch_count_ = 0;

    glEnable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_NEVER);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    CHECK_GL();

    for (const auto& cmd : local_commands) {
        switch (cmd.command_type) {
            case ultralight::CommandType::DrawGeometry:
                DrawGeometry(cmd.geometry_id, cmd.indices_count, 
                           cmd.indices_offset, cmd.gpu_state);
                break;
            case ultralight::CommandType::ClearRenderBuffer:
                ClearRenderBuffer(cmd.gpu_state.render_buffer_id);
                break;
        }
    }

    // local_commands cleared on scope exit
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    CHECK_GL();
}

void ToastGPUDriver::DrawGeometry(uint32_t geometry_id,
                                  uint32_t indices_count,
                                  uint32_t indices_offset,
                                  const ultralight::GPUState& state) {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    glfwMakeContextCurrent(context_->active_window());

    if (programs_.empty()) {
        LoadPrograms();
    }

    BindRenderBuffer(state.render_buffer_id);

    SetViewport(state.viewport_width, state.viewport_height);

    GeometryEntry& geometry = geometry_map_[geometry_id];
    SelectProgram(static_cast<ProgramType>(state.shader_type));
    UpdateUniforms(state);

    CHECK_GL();

    CreateVAOIfNeededForActiveContext(geometry_id);
    auto vao_entry = geometry.vao_map[glfwGetCurrentContext()];
    glBindVertexArray(vao_entry);
    CHECK_GL();


    BindTexture(0, state.texture_1_id);
    BindTexture(1, state.texture_2_id);
    BindTexture(2, state.texture_3_id);

    CHECK_GL();

    if (state.enable_scissor) {
        glEnable(GL_SCISSOR_TEST);
        const ultralight::IntRect& r = state.scissor_rect;
        glScissor(r.left, r.top, (r.right - r.left), (r.bottom - r.top));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    if (state.enable_blend) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    
    CHECK_GL();
    glDrawElements(GL_TRIANGLES, indices_count, GL_UNSIGNED_INT,
                  reinterpret_cast<GLvoid*>(indices_offset * sizeof(unsigned int)));
    CHECK_GL();
    glBindVertexArray(0);

    batch_count_++;

    CHECK_GL();
}


void ToastGPUDriver::LoadPrograms() {
    PROFILE_ZONE_C(tracy::Color::Orange);
    
    LoadProgram(ultralight::ShaderType::Fill);
    LoadProgram(ultralight::ShaderType::FillPath);
    
    TOAST_TRACE("Loaded Ultralight shader programs");
}

void ToastGPUDriver::DestroyPrograms() {
    glUseProgram(0);
    for (auto& [type, prog] : programs_) {
        glDetachShader(prog.program_id, prog.vert_shader_id);
        glDetachShader(prog.program_id, prog.frag_shader_id);
        glDeleteShader(prog.vert_shader_id);
        glDeleteShader(prog.frag_shader_id);
        glDeleteProgram(prog.program_id);
    }
    programs_.clear();
}

void ToastGPUDriver::LoadProgram(ProgramType type) {
    ProgramEntry prog;
    
    if (type == ultralight::ShaderType::Fill) {
        prog.vert_shader_id = LoadShaderFromSource(GL_VERTEX_SHADER,
            ultralight::shaders::shader_v2f_c4f_t2f_t2f_d28f_vert().c_str(), 
            "shader_v2f_c4f_t2f_t2f_d28f.vert");
        prog.frag_shader_id = LoadShaderFromSource(GL_FRAGMENT_SHADER,
            ultralight::shaders::shader_fill_frag().c_str(), 
            "shader_fill.frag");
    } else if (type == ultralight::ShaderType::FillPath) {
        prog.vert_shader_id = LoadShaderFromSource(GL_VERTEX_SHADER,
            ultralight::shaders::shader_v2f_c4f_t2f_vert().c_str(), 
            "shader_v2f_c4f_t2f.vert");
        prog.frag_shader_id = LoadShaderFromSource(GL_FRAGMENT_SHADER,
            ultralight::shaders::shader_fill_path_frag().c_str(), 
            "shader_fill_path.frag");
    }

    prog.program_id = glCreateProgram();
    glAttachShader(prog.program_id, prog.vert_shader_id);
    glAttachShader(prog.program_id, prog.frag_shader_id);

    glBindAttribLocation(prog.program_id, 0, "in_Position");
    glBindAttribLocation(prog.program_id, 1, "in_Color");
    glBindAttribLocation(prog.program_id, 2, "in_TexCoord");

    if (type == ultralight::ShaderType::Fill) {
        glBindAttribLocation(prog.program_id, 3, "in_ObjCoord");
        glBindAttribLocation(prog.program_id, 4, "in_Data0");
        glBindAttribLocation(prog.program_id, 5, "in_Data1");
        glBindAttribLocation(prog.program_id, 6, "in_Data2");
        glBindAttribLocation(prog.program_id, 7, "in_Data3");
        glBindAttribLocation(prog.program_id, 8, "in_Data4");
        glBindAttribLocation(prog.program_id, 9, "in_Data5");
        glBindAttribLocation(prog.program_id, 10, "in_Data6");
    }

    glLinkProgram(prog.program_id);
    
    GLint linkStatus;
    glGetProgramiv(prog.program_id, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        GPU_FATAL("Unable to link shader program.\n\tLog: " << GetProgramLog(prog.program_id));
    }
    
    glUseProgram(prog.program_id);

    if (type == ultralight::ShaderType::Fill) {
        glUniform1i(glGetUniformLocation(prog.program_id, "Texture1"), 0);
        glUniform1i(glGetUniformLocation(prog.program_id, "Texture2"), 1);
        glUniform1i(glGetUniformLocation(prog.program_id, "Texture3"), 2);
    }

    programs_[type] = prog;
}

void ToastGPUDriver::SelectProgram(ProgramType type) {
    auto i = programs_.find(type);
    if (i != programs_.end()) {
        cur_program_id_ = i->second.program_id;
        glUseProgram(i->second.program_id);
    } else {
        GPU_FATAL("Missing shader type: " << static_cast<int>(type));
    }
}

void ToastGPUDriver::UpdateUniforms(const ultralight::GPUState& state) {
    bool flip_y = state.render_buffer_id != 0;
    ultralight::Matrix model_view_projection = ApplyProjection(
        state.transform, 
        static_cast<float>(state.viewport_width), 
        static_cast<float>(state.viewport_height), 
        flip_y);

    float params[4] = { 
        static_cast<float>(glfwGetTime() / 1000.0), 
        static_cast<float>(state.viewport_width), 
        static_cast<float>(state.viewport_height), 
        1.0f 
    };
    SetUniform4f("State", params);
    CHECK_GL();
    
    ultralight::Matrix4x4 mat = model_view_projection.GetMatrix4x4();
    SetUniformMatrix4fv("Transform", 1, mat.data);
    CHECK_GL();
    
    SetUniform4fv("Scalar4", 2, &state.uniform_scalar[0]);
    CHECK_GL();
    
    SetUniform4fv("Vector", 8, &state.uniform_vector[0].x);
    CHECK_GL();
    
    SetUniform1ui("ClipSize", state.clip_size);
    CHECK_GL();
    
    SetUniformMatrix4fv("Clip", 8, &state.clip[0].data[0]);
    CHECK_GL();
}


void ToastGPUDriver::SetUniform1ui(const char* name, GLuint val) {
    glUniform1ui(glGetUniformLocation(cur_program_id_, name), val);
}

void ToastGPUDriver::SetUniform1f(const char* name, float val) {
    glUniform1f(glGetUniformLocation(cur_program_id_, name), static_cast<GLfloat>(val));
}

void ToastGPUDriver::SetUniform1fv(const char* name, size_t count, const float* val) {
    glUniform1fv(glGetUniformLocation(cur_program_id_, name), static_cast<GLsizei>(count), val);
}

void ToastGPUDriver::SetUniform4f(const char* name, const float val[4]) {
    glUniform4f(glGetUniformLocation(cur_program_id_, name),
                static_cast<GLfloat>(val[0]), static_cast<GLfloat>(val[1]),
                static_cast<GLfloat>(val[2]), static_cast<GLfloat>(val[3]));
}

void ToastGPUDriver::SetUniform4fv(const char* name, size_t count, const float* val) {
    glUniform4fv(glGetUniformLocation(cur_program_id_, name), static_cast<GLsizei>(count), val);
}

void ToastGPUDriver::SetUniformMatrix4fv(const char* name, size_t count, const float* val) {
    glUniformMatrix4fv(glGetUniformLocation(cur_program_id_, name), 
                       static_cast<GLsizei>(count), GL_FALSE, val);
}

void ToastGPUDriver::SetViewport(uint32_t width, uint32_t height) {
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

ultralight::Matrix ToastGPUDriver::ApplyProjection(const ultralight::Matrix4x4& transform,
                                                    float screen_width,
                                                    float screen_height,
                                                    bool flip_y) {
    ultralight::Matrix transform_mat;
    transform_mat.Set(transform);

    ultralight::Matrix result;
    result.SetOrthographicProjection(screen_width, screen_height, flip_y);
    result.Transform(transform_mat);

    return result;
}


void ToastGPUDriver::CreateFBOTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) {
    CHECK_GL();
    
    TextureEntry& entry = texture_map_[texture_id];
    entry.width = bitmap->width();
    entry.height = bitmap->height();

    // Allocate a single-sampled texture
    glGenTextures(1, &entry.tex_id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, entry.tex_id);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Allocate texture in linear space
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, entry.width, entry.height, 0,
                GL_BGRA, GL_UNSIGNED_BYTE, nullptr);

    if (context_->msaa_enabled()) {
        // Allocate the multisampled texture
        glGenTextures(1, &entry.msaa_tex_id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, entry.msaa_tex_id);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, 
                               entry.width, entry.height, GL_FALSE);
    }

    CHECK_GL();
    glGenerateMipmap(GL_TEXTURE_2D);
    CHECK_GL();
}

void ToastGPUDriver::CreateFBOIfNeededForActiveContext(uint32_t render_buffer_id) {
    if (render_buffer_id == 0) {
        return;
    }

    auto i = render_buffer_map_.find(render_buffer_id);
    if (i == render_buffer_map_.end()) {
        GPU_FATAL("Error, render buffer entry should exist here.");
        return;
    }

    RenderBufferEntry& entry = i->second;
    auto j = entry.fbo_map.find(glfwGetCurrentContext());
    if (j != entry.fbo_map.end()) {
        return; // Already exists
    }

    FBOEntry& fbo_entry = entry.fbo_map[glfwGetCurrentContext()];

    glGenFramebuffers(1, &fbo_entry.fbo_id);
    CHECK_GL();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_entry.fbo_id);
    CHECK_GL();

    TextureEntry& textureEntry = texture_map_[entry.texture_id];

    glBindTexture(GL_TEXTURE_2D, textureEntry.tex_id);
    CHECK_GL();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 
                          textureEntry.tex_id, 0);
    CHECK_GL();

    GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBuffers);
    CHECK_GL();

    GLenum result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (result != GL_FRAMEBUFFER_COMPLETE) {
        GPU_FATAL("Error creating FBO: " << result);
    }
    CHECK_GL();

    if (!context_->msaa_enabled()) {
        return;
    }

    // Create MSAA FBO
    glGenFramebuffers(1, &fbo_entry.msaa_fbo_id);
    CHECK_GL();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_entry.msaa_fbo_id);
    CHECK_GL();

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textureEntry.msaa_tex_id);
    CHECK_GL();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                          GL_TEXTURE_2D_MULTISAMPLE, textureEntry.msaa_tex_id, 0);
    CHECK_GL();

    glDrawBuffers(1, drawBuffers);
    CHECK_GL();

    result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (result != GL_FRAMEBUFFER_COMPLETE) {
        GPU_FATAL("Error creating MSAA FBO: " << result);
    }
    CHECK_GL();
}

void ToastGPUDriver::CreateVAOIfNeededForActiveContext(uint32_t geometry_id) {
    auto i = geometry_map_.find(geometry_id);
    if (i == geometry_map_.end()) {
        GPU_FATAL("Geometry ID doesn't exist: " << geometry_id);
        return;
    }

    auto& geometry_entry = i->second;

    auto j = geometry_entry.vao_map.find(glfwGetCurrentContext());
    if (j != geometry_entry.vao_map.end()) {
        return; // Already exists
    }

    GLuint vao_entry;

    glGenVertexArrays(1, &vao_entry);
    glBindVertexArray(vao_entry);

    glBindBuffer(GL_ARRAY_BUFFER, geometry_entry.vbo_vertices);
    CHECK_GL();

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry_entry.vbo_indices);
    CHECK_GL();

    if (geometry_entry.vertex_format == ultralight::VertexBufferFormat::_2f_4ub_2f_2f_28f) {
        GLsizei stride = 140;

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(0));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<GLvoid*>(8));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(12));
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(20));
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(28));
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(44));
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(60));
        glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(76));
        glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(92));
        glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(108));
        glVertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(124));

        for (int i = 0; i <= 10; ++i) {
            glEnableVertexAttribArray(i);
        }

        CHECK_GL();
    } else if (geometry_entry.vertex_format == ultralight::VertexBufferFormat::_2f_4ub_2f) {
        GLsizei stride = 20;

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(0));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<GLvoid*>(8));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<GLvoid*>(12));

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);

        CHECK_GL();
    } else {
        GPU_FATAL("Unhandled vertex format: " << static_cast<int>(geometry_entry.vertex_format));
    }

    glBindVertexArray(0);

    geometry_entry.vao_map[glfwGetCurrentContext()] = vao_entry;
}

void ToastGPUDriver::ResolveIfNeeded(uint32_t render_buffer_id) {
    if (!context_->msaa_enabled()) {
        return;
    }

    if (render_buffer_id == 0) {
        return;
    }

    auto it = render_buffer_map_.find(render_buffer_id);
    if (it == render_buffer_map_.end()) {
        return;
    }
    
    RenderBufferEntry& renderBufferEntry = it->second;
    if (!renderBufferEntry.texture_id) {
        return;
    }

    auto i = renderBufferEntry.fbo_map.find(glfwGetCurrentContext());
    if (i == renderBufferEntry.fbo_map.end()) {
        return;
    }

    FBOEntry& fbo_entry = i->second;

    TextureEntry& textureEntry = texture_map_[renderBufferEntry.texture_id];
    if (fbo_entry.needs_resolve) {
        GLint drawFboId = 0, readFboId = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFboId);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFboId);
        CHECK_GL();
        
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_entry.fbo_id);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_entry.msaa_fbo_id);
        CHECK_GL();
        
        glBlitFramebuffer(0, 0, textureEntry.width, textureEntry.height, 
                         0, 0, textureEntry.width, textureEntry.height, 
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
        CHECK_GL();
        
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFboId);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFboId);
        CHECK_GL();
        
        fbo_entry.needs_resolve = false;
    }
}

void ToastGPUDriver::MakeTextureSRGBIfNeeded(uint32_t texture_id) {
    TextureEntry& textureEntry = texture_map_[texture_id];
    if (!textureEntry.is_sRGB) {
        // Destroy existing texture
        glDeleteTextures(1, &textureEntry.tex_id);
        CHECK_GL();
        
        // Create new sRGB texture
        glGenTextures(1, &textureEntry.tex_id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureEntry.tex_id);
        CHECK_GL();
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, textureEntry.width, 
                    textureEntry.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        CHECK_GL();
        
        textureEntry.is_sRGB = true;
    }
}

#if ENABLE_OFFSCREEN_GL
void ToastGPUDriver::UpdateBitmap(RenderBufferEntry& entry, GLuint pbo_id) {
    CHECK_GL();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_id);
    CHECK_GL();
    GLubyte* src = static_cast<GLubyte*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
    CHECK_GL();
    void* dest = entry.bitmap->LockPixels();
    std::memcpy(dest, src, entry.bitmap->size());
    entry.bitmap->UnlockPixels();
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    CHECK_GL();
    entry.is_bitmap_dirty = true;
}
#endif

} // namespace toast::hud
