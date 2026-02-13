#pragma once

#include <Ultralight/platform/FontLoader.h>
#include <Ultralight/RefPtr.h>
#include <string>

class ToastFontLoader : public ultralight::FontLoader {
public:
    static ToastFontLoader& Get();

    ultralight::String fallback_font() const override;
    ultralight::String fallback_font_for_characters(const ultralight::String& characters,
                                                    int weight,
                                                    bool italic) const override;
    ultralight::RefPtr<ultralight::FontFile> Load(const ultralight::String& family,
                                                  int weight,
                                                  bool italic) override;

private:
    ToastFontLoader() = default;
    static void DestroyBuffer(void* user_data, void* data);
};

