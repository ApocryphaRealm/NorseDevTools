// NorseDevTools - reload, inspect and test the Norse frameworks without restarting (UE4SS v4.0.0 C++ mod for NORSE:
// Oath of Blood), 2026-11-17. Survey: RESEARCH-more-frameworks.md (#10).
//   norse.reload   every framework re-reads its files (each one's own reload command, run as console commands)
//   norse.status   every Norse mod: loaded or not, version (its NorseManifest manifest), its log and the errors and
//                  warnings in it now - in NorseDevTools.log
//   norse.help     every framework's console commands in one list
//   auto reload    each framework's config folder (in the game's ue4ss\Mods) is watched once a second: a saved change
//                  runs that framework's reload by itself (bAutoReload in NorseDevTools.ini). Root Builder copies a file
//                  changed in the game folder back into its MO2 mod when the game closes.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"

#include <map>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod, g_mods; // ...\ue4ss\Mods\NorseDevTools, ...\ue4ss\Mods
    bool g_autoReload = true;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    // folder (under ue4ss\Mods) -> the reload command of the framework that reads it
    struct Watched
    {
        const wchar_t* folder;
        const wchar_t* reload;
        const char* what;
    };
    const Watched kWatched[] = {
        {L"NorsePatcher\\patches", L"np.reload", "NorsePatcher patches"},
        {L"NorseDistributor\\distributions", L"nd.reload", "NorseDistributor rules"},
        {L"NorseTagger\\tags", L"nt.reload", "NorseTagger rules"},
        {L"NorseConditions\\conditions", L"nc.reload", "NorseConditions presets"},
        {L"NorsePayload\\payloads", L"pl.reload", "NorsePayload tables"},
        {L"NorseImpact\\impacts", L"ni.reload", "NorseImpact rules"},
        {L"NorseLoot\\loot", L"nl.reload", "NorseLoot rules"},
        {L"NorseEncounters\\encounters", L"enc.reload", "NorseEncounters rules"},
        {L"NorseSound\\sounds", L"snd.reload", "NorseSound rules"},
        {L"NorseModelSwap\\models", L"mdl.reload", "NorseModelSwap rules"},
        {L"NorseMoveset\\movesets", L"nrn.movesets", "NorseMoveset movesets (NorseRealtimeNative)"},
        {L"NorseInput\\bindings", L"nrn.bindings", "NorseInput bindings (NorseRealtimeNative)"},
    };

    const char* kHelp[] = {
        "norse.reload | norse.status | norse.help                (NorseDevTools)",
        "np.reload | np.apply | np.dump <object path>            (NorsePatcher)",
        "nd.reload | nd.apply | nd.dump <object path>            (NorseDistributor)",
        "nt.reload | nt.apply | nt.list <tag> | nt.tags <path>   (NorseTagger)",
        "nc.eval <condition> | nc.list | nc.reload               (NorseConditions)",
        "ne.log on|off | ne.stats                                (NorseEvents)",
        "ncs.set <owner> <key> <value> | ncs.get | ncs.dump      (NorseCoSave)",
        "pl.list | pl.reload | pl.log on|off                     (NorsePayload)",
        "ni.list | ni.reload | ni.test <rule>                    (NorseImpact)",
        "ncl.test | ncl.where <rva>                              (NorseCrashLog)",
        "nl.reload | nl.apply | nl.dump <object path>            (NorseLoot)",
        "enc.reload | enc.list                                  (NorseEncounters)",
        "snd.reload | snd.apply | snd.music | snd.test <rule>   (NorseSound)",
        "mdl.list [class] | mdl.apply | mdl.reload              (NorseModelSwap)",
        "nm.list                                                 (NorseManifest)",
        "nrn.movesets | nrn.bindings | nrn.status | nrn.lock ... (NorseRealtimeNative)",
    };

    // ------------------------------------------------------------------ running console commands
    // KismetSystemLibrary::ExecuteConsoleCommand(WorldContextObject @0, FString Command @8, APlayerController* @24) - 32 bytes
    bool Exec(const std::wstring& cmd)
    {
        static UObject* ksl = nullptr;
        if (!ksl) ksl = RC::Unreal::UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, L"/Script/Engine.Default__KismetSystemLibrary", false);
        UObject* pc = nullptr;
        try
        {
            pc = RC::Unreal::UObjectGlobals::FindFirstOf(L"PlayerController");
        }
        catch (...)
        {
        }
        if (!ksl || !pc) return false;
        if (Reflect::Get().ParmsSizeOf(ksl, L"ExecuteConsoleCommand") != 32)
        {
            NRN_ERROR("ExecuteConsoleCommand's parameters changed - command '%s' not run", Narrow(cmd).c_str());
            return false;
        }
        Buf p;
        struct FStr
        {
            const wchar_t* data;
            int32_t num, max;
        } s{cmd.c_str(), static_cast<int32_t>(cmd.size() + 1), static_cast<int32_t>(cmd.size() + 1)};
        std::memcpy(p.b, &pc, 8);
        std::memcpy(p.b + 8, &s, 16);
        std::memcpy(p.b + 24, &pc, 8);
        return Reflect::Get().Call(ksl, L"ExecuteConsoleCommand", p.b, 32, sizeof(p.b));
    }

    void ReloadAll()
    {
        int n = 0;
        for (const Watched& w : kWatched)
            if (fs::exists(g_mods / fs::path(w.folder).parent_path()))
            {
                bool ok = Exec(w.reload);
                n += ok;
                NRN_INFO("norse.reload: %s -> %s", Narrow(w.reload).c_str(), ok ? "run" : "NOT run");
            }
        NRN_INFO("norse.reload: %d framework reload(s) run", n);
    }

    // ------------------------------------------------------------------ status
    // a framework's log: <its folder>\<Name>.log, or the folder its INI's sLogDir names
    fs::path LogOf(const std::wstring& name)
    {
        fs::path dir = g_mods / name;
        std::error_code ec;
        for (const auto& f : fs::directory_iterator(dir, ec))
            if (Lower(f.path().extension().wstring()) == L".ini")
                for (const auto& raw : ReadLines(f.path()))
                {
                    std::wstring l = Trim(raw);
                    if (l.rfind(L"sLogDir=", 0) == 0 && l.size() > 8) dir = fs::path(Trim(l.substr(8)));
                    if (l.rfind(L"sBusDir=", 0) == 0 && l.size() > 8) dir = fs::path(Trim(l.substr(8)));
                }
        return dir / (name + L".log");
    }

    void Status()
    {
        nm::Manifest man;
        man.Resolve(g_mods.parent_path());
        std::map<std::wstring, std::string> versions;
        for (const auto& p : man.Packs()) versions[Lower(p.name)] = Narrow(p.versionText) + (p.enabled ? "" : " (OFF: " + (p.problems.empty() ? std::string("?") : p.problems.front()) + ")");
        NRN_INFO("norse.status - Norse mods in %s:", Narrow(g_mods.wstring()).c_str());
        std::error_code ec;
        for (const auto& d : fs::directory_iterator(g_mods, ec))
        {
            std::wstring name = d.path().filename().wstring();
            if (name.rfind(L"Norse", 0) != 0) continue;
            fs::path dll = d.path() / "dlls" / "main.dll";
            bool hasDll = fs::exists(dll, ec);
            bool loaded = hasDll && GetModuleHandleW(dll.c_str()) != nullptr;
            std::string ver = versions.count(Lower(name)) ? versions[Lower(name)] : "no manifest";
            int errors = 0, warns = 0;
            fs::path log = LogOf(name);
            if (hasDll)
            {
                std::ifstream in(log);
                std::string line;
                while (std::getline(in, line))
                {
                    if (line.find("] [error]") != std::string::npos || line.find(" [error] ") != std::string::npos) ++errors;
                    if (line.find(" [warn] ") != std::string::npos) ++warns;
                }
            }
            NRN_INFO("  %-22s %-10s %-14s %s", Narrow(name).c_str(), ver.c_str(), hasDll ? (loaded ? "loaded" : "NOT LOADED") : "data only",
                     hasDll ? (Narrow(log.filename().wstring()) + ": " + std::to_string(errors) + " error line(s), " + std::to_string(warns) +
                               " warning(s)")
                                    .c_str()
                            : "");
        }
        NRN_INFO("  (the first error line of each log is its header: \"vX - log level\" - it is written at error level on purpose)");
    }

    // ------------------------------------------------------------------ watching the config folders
    std::map<std::wstring, std::wstring> g_sig; // folder -> signature (names, sizes, write times)

    std::wstring Signature(const fs::path& dir)
    {
        std::wstring s;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return L"-";
        for (const auto& f : fs::directory_iterator(dir, ec))
            if (f.is_regular_file(ec) && Lower(f.path().extension().wstring()) == L".ini")
                s += f.path().filename().wstring() + L"|" + std::to_wstring(f.file_size(ec)) + L"|" +
                     std::to_wstring(f.last_write_time(ec).time_since_epoch().count()) + L";";
        return s;
    }

    void Watch()
    {
        for (const Watched& w : kWatched)
        {
            std::wstring key = w.folder;
            std::wstring now = Signature(g_mods / w.folder);
            auto it = g_sig.find(key);
            if (it == g_sig.end())
            {
                g_sig[key] = now; // first look: remember, do not reload
                continue;
            }
            if (it->second == now) continue;
            it->second = now;
            bool ok = Exec(w.reload);
            NRN_INFO("auto reload: %s changed -> %s %s", w.what, Narrow(w.reload).c_str(), ok ? "run" : "NOT run (no player controller yet)");
        }
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Lower(Trim(c));
        NRN_INFO("command: %s", Narrow(cmd).c_str());
        if (cmd == L"norse.reload") ReloadAll();
        else if (cmd == L"norse.status") Status();
        else if (cmd == L"norse.help")
            for (const char* h : kHelp) NRN_INFO("  %s", h);
        else NRN_WARN("unknown command - norse.reload | norse.status | norse.help");
        return true;
    }
} // namespace

class NorseDevTools : public RC::CppUserModBase
{
  public:
    NorseDevTools()
    {
        ModName = L"NorseDevTools";
        ModVersion = L"" NDT_VERSION;
        ModDescription = L"Reload, inspect and test the Norse frameworks without restarting; config folders reload on save.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        g_mods = g_mod.parent_path();
        std::wstring dir;
        for (const auto& raw : ReadLines(g_mod / "NorseDevTools.ini"))
        {
            std::wstring l = Trim(StripComment(raw));
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
            if (l.rfind(L"bAutoReload=", 0) == 0) g_autoReload = _wtoi(l.c_str() + 12) != 0;
        }
        Log::Get().Open((dir.empty() ? g_mod : fs::path(dir)) / "NorseDevTools.log", 2);
        Log::Get().Write(Level::Error, "NorseDevTools v%s - auto reload %s", NDT_VERSION, g_autoReload ? "on" : "off");
    }

    auto on_unreal_init() -> void override
    {
        try
        {
            RC::Unreal::Hook::RegisterEngineTickPreCallback([](RC::Unreal::UEngine*, float) {
                static ULONGLONG next = 0;
                ULONGLONG now = GetTickCount64();
                if (!g_autoReload || now < next) return;
                next = now + 1000; // once a second: eight small folders
                try
                {
                    Watch();
                }
                catch (...)
                {
                }
            });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"norse.", 6) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseDevTools();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
