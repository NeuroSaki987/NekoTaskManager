#pragma once
#include "Common.h"

class FontManager {
public:
    ~FontManager();
    bool LoadEmbeddedFonts(HINSTANCE instance);
    const std::wstring& UiFontFamily() const { return m_uiFont; }
    const std::wstring& PixelFontFamily() const { return m_pixelFont; }
private:
    std::wstring ExtractResourceToTempFile(HINSTANCE instance, int resourceId, const std::wstring& extension) const;

    std::wstring m_uiFont = L"Segoe UI";
    std::wstring m_pixelFont = L"HYPixel 11px";
    std::vector<std::wstring> m_loadedFiles;
};
