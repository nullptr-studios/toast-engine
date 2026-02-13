#include <Toast/Renderer/HUD/ToastFontLoader.hpp>

#include <Toast/Log.hpp>
#include <Ultralight/Buffer.h>

#include <cstdio>
#include <cstdlib>
#include <string>

ToastFontLoader& ToastFontLoader::Get() {
    static ToastFontLoader instance;
    return instance;
}

void ToastFontLoader::DestroyBuffer(void* /*user_data*/, void* data) {
    delete[] static_cast<char*>(data);
}

ultralight::String ToastFontLoader::fallback_font() const {
#ifdef _WIN32
    TOAST_TRACE("[FontLoader] fallback_font() called, returning Arial");
    return "Arial";
#elif __APPLE__
    TOAST_TRACE("[FontLoader] fallback_font() called, returning Helvetica");
    return "Helvetica";
#else
    TOAST_TRACE("[FontLoader] fallback_font() called, returning sans-serif");
    return "sans-serif";
#endif
}

ultralight::String ToastFontLoader::fallback_font_for_characters(const ultralight::String& /*characters*/,
                                                                 int weight,
                                                                 bool italic) const {
    TOAST_TRACE("[FontLoader] fallback_font_for_characters() called, weight={} italic={}", weight, italic);
    return fallback_font();
}

ultralight::RefPtr<ultralight::FontFile> ToastFontLoader::Load(const ultralight::String& family,
                                                               int weight,
                                                               bool italic) {
    std::string font_path;
    std::string family_str = family.utf8().data();

    TOAST_TRACE("[FontLoader] Loading font: '{}' weight={} italic={}", family_str, weight, italic);

#ifdef _WIN32
    const char* windows_dir = getenv("WINDIR");
    std::string fonts_dir = windows_dir ? std::string(windows_dir) + "\\Fonts\\" : "C:\\Windows\\Fonts\\";

    if (family_str == "Arial" || family_str == "sans-serif") {
        if (weight >= 700 && italic) font_path = fonts_dir + "arialbi.ttf";
        else if (weight >= 700) font_path = fonts_dir + "arialbd.ttf";
        else if (italic) font_path = fonts_dir + "ariali.ttf";
        else font_path = fonts_dir + "arial.ttf";
    } else if (family_str == "Times New Roman" || family_str == "serif") {
        if (weight >= 700 && italic) font_path = fonts_dir + "timesbi.ttf";
        else if (weight >= 700) font_path = fonts_dir + "timesbd.ttf";
        else if (italic) font_path = fonts_dir + "timesi.ttf";
        else font_path = fonts_dir + "times.ttf";
    } else if (family_str == "Courier New" || family_str == "monospace") {
        if (weight >= 700 && italic) font_path = fonts_dir + "courbi.ttf";
        else if (weight >= 700) font_path = fonts_dir + "courbd.ttf";
        else if (italic) font_path = fonts_dir + "couri.ttf";
        else font_path = fonts_dir + "cour.ttf";
    } else if (family_str == "Segoe UI") {
        if (weight >= 700 && italic) font_path = fonts_dir + "segoeuiz.ttf";
        else if (weight >= 700) font_path = fonts_dir + "segoeuib.ttf";
        else if (italic) font_path = fonts_dir + "segoeuii.ttf";
        else font_path = fonts_dir + "segoeui.ttf";
    } else if (family_str == "Tahoma") {
        if (weight >= 700) font_path = fonts_dir + "tahomabd.ttf";
        else font_path = fonts_dir + "tahoma.ttf";
    } else if (family_str == "Verdana") {
        if (weight >= 700 && italic) font_path = fonts_dir + "verdanaz.ttf";
        else if (weight >= 700) font_path = fonts_dir + "verdanab.ttf";
        else if (italic) font_path = fonts_dir + "verdanai.ttf";
        else font_path = fonts_dir + "verdana.ttf";
    } else if (family_str == "Georgia") {
        if (weight >= 700 && italic) font_path = fonts_dir + "georgiaz.ttf";
        else if (weight >= 700) font_path = fonts_dir + "georgiab.ttf";
        else if (italic) font_path = fonts_dir + "georgiai.ttf";
        else font_path = fonts_dir + "georgia.ttf";
    } else {
        TOAST_WARN("[FontLoader] Unknown font family '{}', falling back to Arial", family_str);
        font_path = fonts_dir + "arial.ttf";
    }
#else
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif

    TOAST_TRACE("[FontLoader] Trying to load: {}", font_path);

    FILE* f = fopen(font_path.c_str(), "rb");
    if (!f) {
        TOAST_ERROR("[FontLoader] Could not open font file: {}", font_path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = new char[file_size];
    size_t bytes_read = fread(data, 1, file_size, f);
    fclose(f);

    if (bytes_read != file_size) {
        TOAST_ERROR("[FontLoader] Failed to read font file: {} (read {} of {} bytes)", font_path, bytes_read, file_size);
        delete[] data;
        return nullptr;
    }

    TOAST_TRACE("[FontLoader] Successfully loaded font: {} ({} bytes)", font_path, file_size);

    auto buffer = ultralight::Buffer::Create(data, file_size, &ToastFontLoader::DestroyBuffer, nullptr);
    return ultralight::FontFile::Create(buffer);
}

