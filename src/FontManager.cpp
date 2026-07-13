#include "FontManager.h"
#include "../resources/resource.h"

FontManager::~FontManager()
{
    for (const auto& file : m_loadedFiles) {
        RemoveFontResourceExW(file.c_str(), FR_PRIVATE, nullptr);
        DeleteFileW(file.c_str());
    }
}

std::wstring FontManager::ExtractResourceToTempFile(HINSTANCE instance, int resourceId, const std::wstring& extension) const
{
    HRSRC res = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res) return L"";

    HGLOBAL loaded = LoadResource(instance, res);
    if (!loaded) return L"";

    const DWORD size = SizeofResource(instance, res);
    const void* data = LockResource(loaded);
    if (!data || size == 0) return L"";

    wchar_t tempPath[MAX_PATH]{};
    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempPath)) return L"";
    if (!GetTempFileNameW(tempPath, L"NTM", 0, tempFile)) return L"";

    std::wstring finalPath = std::wstring(tempFile) + extension;
    DeleteFileW(finalPath.c_str());
    if (!MoveFileW(tempFile, finalPath.c_str())) {
        DeleteFileW(tempFile);
        return L"";
    }

    HANDLE file = CreateFileW(finalPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(finalPath.c_str());
        return L"";
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    if (!ok || written != size) {
        DeleteFileW(finalPath.c_str());
        return L"";
    }

    return finalPath;
}

bool FontManager::LoadEmbeddedFonts(HINSTANCE instance)
{
    const std::wstring fontPath = ExtractResourceToTempFile(instance, IDR_USER_CUSTOM_FONT, L".ttf");
    if (fontPath.empty()) {
        return false;
    }

    if (AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr) > 0) {
        m_loadedFiles.push_back(fontPath);
        return true;
    }

    DeleteFileW(fontPath.c_str());
    return false;
}
