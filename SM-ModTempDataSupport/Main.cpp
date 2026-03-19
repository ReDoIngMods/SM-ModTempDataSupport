#include <Windows.h>
#include <filesystem>

#include <MinHook/MinHook.h>
#include <LuaJIT/lua.hpp>

#include <SmSdk/TimestampCheck.hpp>
#include <SmSdk/DirectoryManager.hpp>

#include <boost/uuid/uuid_io.hpp>

struct LuaVM {
    struct lua_State* m_luaState;
private:
    char pad_0x0[0x68];
public:
    boost::uuids::uuid m_currentlyExecutingMod;
};

static bool GInitalized = false;

inline static std::string StyleJson(const std::string& compactJson) {
    std::string output;
    output.reserve(compactJson.size() * 2);

    int indentLevel = 0;
    bool insideString = false;
    bool isEscaped = false;
    const std::string indentUnit = "    ";

    auto writeIndentation = [&]() {
        for (int level = 0; level < indentLevel; ++level)
            output += indentUnit;
    };

    for (size_t characterIndex = 0; characterIndex < compactJson.size(); ++characterIndex) {
        const char character = compactJson[characterIndex];

        if (isEscaped) {
            isEscaped = false;
            output += character;
            continue;
        }

        if (character == '\\' && insideString) {
            isEscaped = true;
            output += character;
            continue;
        }

        if (character == '"') {
            insideString = !insideString;
            output += character;
            continue;
        }

        if (insideString) {
            output += character;
            continue;
        }

        switch (character) {
        case '{':
        case '[': {
            output += character;

            size_t peekIndex = characterIndex + 1;
            while (peekIndex < compactJson.size() && compactJson[peekIndex] == ' ')
                ++peekIndex;

            char closingBracket = (character == '{') ? '}' : ']';
            if (peekIndex >= compactJson.size() || compactJson[peekIndex] != closingBracket) {
                ++indentLevel;

                output += "\r\n";
                writeIndentation();
            }

            break;
        }
        case '}':
        case ']': {
            char previousCharacter = 0;
            for (int scanIndex = static_cast<int>(output.size()) - 1; scanIndex >= 0; scanIndex--) {
                if (output[scanIndex] == ' ' || output[scanIndex] == '\n' || output[scanIndex] == '\r')
                    continue;

                previousCharacter = output[scanIndex];
                break;
            }

            const char openingBracket = (character == '}') ? '{' : '[';
            if (previousCharacter == openingBracket) {
                output += character;
            } else {
                --indentLevel;
                output += "\r\n";
                writeIndentation();
                output += character;
            }

            break;
        }

        case ',':
            output += character;
            output += "\r\n";

            writeIndentation();
            break;
        case ':':
            output += ": ";
            break;
        default:
            output += character;
            break;
        }
    }

    return output;
}

inline static bool WriteToFile(const std::string& path, const std::string& data) {
	HANDLE hFile = CreateFileA(path.data(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER fileSize{};
	fileSize.QuadPart = data.size();

    if (!SetFilePointerEx(hFile, fileSize, nullptr, FILE_BEGIN) || !SetEndOfFile(hFile)) {
        CloseHandle(hFile);
        return false;
    }

	HANDLE hMapping = CreateFileMappingA(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
	if (!hMapping) {
        CloseHandle(hFile);
        return false;
    }

	LPVOID mappedView = MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, data.size());
    if (!mappedView) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
	}

	memcpy(mappedView, data.data(), data.size());

	FlushViewOfFile(mappedView, data.size());

	UnmapViewOfFile(mappedView);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return true;
}

inline static std::string TranslateToContentUUID(const std::string& path, const std::string& uuid) {
    std::string result = path;
    const std::string prefixes[] = { "$CONTENT_PATH", "$MOD_DATA" };

    for (const std::string& prefix : prefixes) {
        if (result.rfind(prefix, 0) != 0)
            continue;

        result = "$CONTENT_" + uuid + result.substr(prefix.size());
        break;
    }

    return result;
}

int hook_sm_json_save(lua_State* L) {
    if (lua_gettop(L) != 2) {
		luaL_error(L, "Expected %d arguments (got %d)", 2, lua_gettop(L));
        return 0;
    }

    luaL_argcheck(L, lua_istable(L, 1) || lua_isnil(L, 1) || lua_isnumber(L, 1) || lua_isboolean(L, 1) || lua_isstring(L, 1), 1, "table, nil, number, boolean, or string expected");

	size_t pathLength;
	const char* path = luaL_checklstring(L, 2, &pathLength);
	std::string pathStr(path, pathLength);

    if (pathStr.ends_with("/description.json")) {
		luaL_error(L, "'%s' not a valid call point.", pathStr.data());
        return 0;
    }

    lua_getfield(L, LUA_REGISTRYINDEX, "VMPtr");
    LuaVM* luaVM = reinterpret_cast<LuaVM*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

	const std::string currentlyExecutingMod = boost::uuids::to_string(luaVM->m_currentlyExecutingMod);
	const std::string translatedPath = TranslateToContentUUID(pathStr, currentlyExecutingMod);

    if (!pathStr.starts_with("$TEMP_DATA")) {
        if (!pathStr.starts_with("$CONTENT_" + currentlyExecutingMod)) {
            luaL_error(L, "'%s' is not located in the same content id as the caller", pathStr.data());
            return 0;
        }
    }

    std::string replacedPath = pathStr;
    
    SM::DirectoryManager* directoryManager = SM::DirectoryManager::GetInstance();
    directoryManager->replacePathR(replacedPath);
    
    if (pathStr == replacedPath) {
        luaL_error(L, "'%s' is not located in a valid directory", pathStr.data());
        return 0;
    }

    lua_getglobal(L, "sm");
    lua_getfield(L, -1, "json");
    lua_getfield(L, -1, "writeJsonString");
    lua_pushvalue(L, 1);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        luaL_error(L, lua_tostring(L, -1));
        return 0;
    }

    size_t jsonLength;
    const char* jsonStr = lua_tolstring(L, -1, &jsonLength);
    std::string data(jsonStr, jsonLength);

    lua_pop(L, 3);

    if (!WriteToFile(replacedPath, StyleJson(data))) {
		luaL_error(L, "Failed to write to '%s'", replacedPath.data());
        return 0;
    }

    return 0;
}

using LoadLuaLibFunc = std::add_pointer_t<void(lua_State*)>;
using LoadLuaEnvFunc = std::add_pointer_t<int(LuaVM*, LoadLuaLibFunc*, int enviromentFlag)>;
LoadLuaEnvFunc GLuaEnvInitOriginal = nullptr;

int hook_lua_env_init(LuaVM* luaVM, LoadLuaLibFunc* loadfuncs, int enviromentFlag) {
    int result = GLuaEnvInitOriginal(luaVM, loadfuncs, enviromentFlag);
    if (result != 0)
        return result;

	lua_State* L = luaVM->m_luaState;

	lua_getglobal(L, "sm");
	lua_pushboolean(L, 1);
	lua_setfield(L, -2, "tempDataMod_installed");
	lua_pop(L, 1);

	lua_getglobal(L, "unsafe_env");
	lua_getfield(L, -1, "sm");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "tempDataMod_installed");
    lua_pop(L, 2);

    return 0;
}

DWORD WINAPI OnProcessAttach(LPVOID lpParam) {
	HMODULE hModule = reinterpret_cast<HMODULE>(lpParam);

    if (!SmSdk::CheckTimestamp(_SM_TIMESTAMP_074_778)) {
		MessageBox(NULL, L"Unsupported version of Scrap Mechanic detected. Please use version 0.7.4.778.", L"SM-ModTempDataSupport - Version Mismatch", MB_ICONERROR | MB_OK);
        FreeLibrary(hModule);

        return FALSE;
	}

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        MessageBox(NULL, L"Failed to initialize MinHook.", L"SM-ModTempDataSupport - Initialization Error", MB_ICONERROR | MB_OK);
        FreeLibrary(hModule);
        return FALSE;
	}

    BYTE* baseAddress = reinterpret_cast<BYTE*>(GetModuleHandle(L"ScrapMechanic.exe"));
	
	// sm.json.save hook
    {
        LPVOID targetFunction = baseAddress + 0x92A370;

        status = MH_CreateHook(targetFunction, &hook_sm_json_save, nullptr);
        if (status != MH_OK) {
            MessageBox(NULL, L"Failed to create sm.json.save hook.", L"SM-ModTempDataSupport - Hook Creation Error", MB_ICONERROR | MB_OK);

            MH_Uninitialize();
            FreeLibrary(hModule);
            return FALSE;
        }

        status = MH_EnableHook(targetFunction);
        if (status != MH_OK) {
            MessageBox(NULL, L"Failed to enable sm.json.save hook.", L"SM-ModTempDataSupport - Hook Enabling Error", MB_ICONERROR | MB_OK);

            MH_Uninitialize();
            FreeLibrary(hModule);
            return FALSE;
        }
    }

    // Lua Env Init hook
    {
		LPVOID targetFunction = baseAddress + 0x54A7F0;
        
		status = MH_CreateHook(targetFunction, &hook_lua_env_init, reinterpret_cast<LPVOID*>(&GLuaEnvInitOriginal));
        if (status != MH_OK) {
            MessageBox(NULL, L"Failed to create Lua Env Init hook.", L"SM-ModTempDataSupport - Hook Creation Error", MB_ICONERROR | MB_OK);

            MH_Uninitialize();
            FreeLibrary(hModule);
            return FALSE;
		}

		status = MH_EnableHook(targetFunction);
        if (status != MH_OK) {
            MessageBox(NULL, L"Failed to enable Lua Env Init hook.", L"SM-ModTempDataSupport - Hook Enabling Error", MB_ICONERROR | MB_OK);

            MH_Uninitialize();
            FreeLibrary(hModule);
            return FALSE;
        }
    }

    GInitalized = true;

    return TRUE;
}

DWORD WINAPI OnProcessDetach(LPVOID lpParam) {
    if (!GInitalized)
        return TRUE;

    GInitalized = false;

    MH_STATUS status = MH_Uninitialize();
    if (status != MH_OK) {
        MessageBox(NULL, L"Failed to uninitialize MinHook.", L"SM-ModTempDataSupport - Uninitialization Error", MB_ICONERROR | MB_OK);
        return FALSE;
	}

    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		CreateThread(NULL, 0, OnProcessAttach, hModule, 0, NULL);
        break;
	case DLL_PROCESS_DETACH:
		CreateThread(NULL, 0, OnProcessDetach, NULL, 0, NULL);
        break;
    }

    return TRUE;
}

