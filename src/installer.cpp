#include "install_token.hpp"
#include "installer.h"
#include "install-data.hpp"

#include <D2RLPlugin/context.h>
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace std::string_view_literals;

constexpr auto Excel = L"data/global/excel";
constexpr auto Strings = L"data/local/lng/strings";
constexpr auto PropertiesFile = L"properties.txt";
constexpr auto StatCostFile = L"itemstatcost.txt";
constexpr auto SharedSetup = L"plugins/basefiles-and-backups";
constexpr auto InstallKey = "UnrunedOathInstallData"sv;
constexpr auto Whitespace = " \t\r\n"sv;

class InstallLock {
public:
    InstallLock() {
        handle = CreateMutexW(nullptr, FALSE, L"Local\\D2RLoader.ModFirstRunInstall.v1");
        if (!handle) throw std::runtime_error("could not open the shared mod-install lock");
        const DWORD result = WaitForSingleObject(handle, 60000);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return;
        CloseHandle(handle);
        throw std::runtime_error("could not acquire the shared mod-install lock");
    }

    ~InstallLock() {
        ReleaseMutex(handle);
        CloseHandle(handle);
    }

    InstallLock(const InstallLock&) = delete;
    InstallLock& operator=(const InstallLock&) = delete;

private:
    HANDLE handle{};
};

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not read " + path.filename().string());
    return {std::istreambuf_iterator<char>(input), {}};
}

bool IsWithin(const fs::path& root, const fs::path& path) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.has_root_path() && *relative.begin() != L"..";
}

fs::path ModDataRoot(const D2RL::PluginContext& ctx) {
    if (!ctx.activeMod || !ctx.activeMod[0]) return {};
    const std::string_view activeMod(ctx.activeMod);
    const fs::path name(std::u8string(activeMod.begin(), activeMod.end()));
    if (name.has_parent_path() || name.has_root_path() || name == L"." || name == L"..") return {};
    const fs::path package = name.native() + L".mpq";
    const fs::path bases[]{
        ctx.modDirectory ? ctx.modDirectory : L"",
        ctx.scopeRootDirectory ? ctx.scopeRootDirectory : L"",
    };
    fs::path expected;
    for (const auto& base : bases) {
        if (base.empty()) continue;
        if (expected.empty()) {
            if (base.filename() == name) expected = base / package;
            else if (base.filename() == package) expected = base;
        }
        const fs::path candidates[]{base / package, base / name / package,
            base / L"mods" / name / package, base / name, base};
        for (const auto& candidate : candidates) {
            if ((candidate.filename() == package && fs::exists(candidate))
                    || (candidate.filename() == name && fs::is_directory(candidate / L"data")))
                return fs::canonical(candidate);
        }
    }
    return expected.empty() ? fs::path{} : fs::weakly_canonical(fs::absolute(expected));
}

std::string ReadModFile(const fs::path& root, const fs::path& relative) {
    const auto target = fs::canonical(root / relative);
    if (!IsWithin(root, target) || !fs::is_regular_file(target))
        throw std::runtime_error("invalid mod file " + relative.filename().string());
    return ReadFile(target);
}

std::optional<std::string> ReadOptionalModFile(const fs::path& root, const fs::path& relative) {
    const auto target = root / relative;
    if (!IsWithin(root, fs::weakly_canonical(target.parent_path())))
        throw std::runtime_error("invalid directory for " + relative.filename().string());
    if (fs::symlink_status(target).type() == fs::file_type::not_found) return std::nullopt;
    return ReadModFile(root, relative);
}

std::string PathText(fs::path path) {
    const auto utf8 = path.make_preferred().u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::vector<std::string_view> Split(std::string_view text, char delimiter) {
    std::vector<std::string_view> result;
    for (;;) {
        const auto end = text.find(delimiter);
        auto field = text.substr(0, end);
        if (delimiter == '\n' && field.ends_with('\r')) field.remove_suffix(1);
        result.push_back(field);
        if (end == std::string_view::npos) return result;
        text.remove_prefix(end + 1);
    }
}

unsigned Number(std::string_view value) {
    if (value.empty()) throw std::runtime_error("missing numeric table field");
    unsigned number{};
    const auto end = value.data() + value.size();
    const auto result = std::from_chars(value.data(), end, number);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::runtime_error("invalid numeric table field");
    return number;
}

struct Tsv {
    std::map<std::string_view, std::size_t> columns;
    std::vector<std::vector<std::string_view>> rows;

    explicit Tsv(std::string_view text) {
        if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
        const auto lines = Split(text, '\n');
        const auto header = Split(lines.front(), '\t');
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i].empty() || !columns.emplace(header[i], i).second)
                throw std::runtime_error("empty or duplicate table column");
        for (auto line = lines.begin() + 1; line != lines.end(); ++line)
            if (!line->empty()) rows.push_back(Split(*line, '\t'));
    }

    std::size_t Column(std::string_view name) const {
        const auto found = columns.find(name);
        if (found == columns.end()) throw std::runtime_error("missing table column " + std::string(name));
        return found->second;
    }

    static std::string_view Field(const std::vector<std::string_view>& row, std::size_t column) {
        return column < row.size() ? row[column] : std::string_view{};
    }

    std::size_t Find(std::size_t column, std::string_view key) const {
        std::size_t found = rows.size();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (Field(rows[i], column) != key) continue;
            if (found != rows.size()) throw std::runtime_error("duplicate row " + std::string(key));
            found = i;
        }
        return found;
    }
};

unsigned StartingId(const Tsv& target, std::size_t keyColumn, std::string_view idColumn,
        std::string_view firstKey, bool allowUnnumberedRows = false) {
    const auto id = target.Column(idColumn);
    unsigned count{}, next{}, existing{};
    for (const auto& row : target.rows) {
        const auto key = Tsv::Field(row, keyColumn);
        if (key.empty() || key == "Expansion") continue;
        const auto field = Tsv::Field(row, id);
        if (!field.empty()) {
            const auto rowId = Number(field);
            if (rowId >= 0x7FFFFFFFu) throw std::runtime_error("table row ID is too large");
            if (allowUnnumberedRows && rowId != count)
                throw std::runtime_error("skill row ID and position do not match");
            next = (std::max)(next, rowId + 1);
        }
        if (key == firstKey) existing = count;
        ++count;
    }
    if (next != count && !allowUnnumberedRows)
        throw std::runtime_error("row IDs and table order do not match");
    const auto found = target.Find(keyColumn, firstKey);
    if (found == target.rows.size()) return count;
    if (Number(Tsv::Field(target.rows[found], id)) != existing)
        throw std::runtime_error("an existing Oath row has a different ID and position");
    return existing;
}

struct TableIds {
    unsigned ring{}, skill{}, state{};
};

std::string MergeTable(const InstallData::TableSeed& seed, std::string_view original, TableIds& ids) try {
    const Tsv source(seed.tsv);
    const Tsv target(original);
    const auto sourceKey = source.Column(seed.keyColumn);
    const auto targetKey = target.Column(seed.keyColumn);
    if (source.rows.empty()) throw std::runtime_error("no source rows");
    const auto firstKey = Tsv::Field(source.rows.front(), sourceKey);
    const std::wstring_view filename(seed.filename);
    if (filename == L"setitems.txt") ids.ring = StartingId(target, targetKey, seed.idColumn, firstKey);
    // Extracted Skills tables may leave the *Id comment blank for RotW rows.
    if (filename == L"skills.txt") ids.skill = StartingId(target, targetKey, seed.idColumn, firstKey, true);
    if (filename == L"states.txt") ids.state = StartingId(target, targetKey, seed.idColumn, firstKey);

    const auto newline = original.find("\r\n") == std::string_view::npos ? "\n"sv : "\r\n"sv;
    const auto creator = target.columns.find("*Creator");
    std::string result(original);
    std::set<std::string_view> keys;
    for (const auto& fields : source.rows) {
        const auto key = Tsv::Field(fields, sourceKey);
        if (key.empty()) throw std::runtime_error("missing source row key");
        if (!keys.insert(key).second) throw std::runtime_error("duplicate source row " + std::string(key));
        if (fields.size() > source.columns.size())
            throw std::runtime_error("source row has more fields than its header");
        const auto found = target.Find(targetKey, key);
        std::vector<std::string> row(target.columns.size());
        for (const auto& [name, index] : source.columns) {
            const auto field = Tsv::Field(fields, index);
            if (field.empty() || name == "*Creator") continue;
            const auto column = target.Column(name);
            auto& cell = row[column];
            if (field.starts_with("@ring")) cell = std::to_string(ids.ring + Number(field.substr(5)));
            else if (field == "@skill") cell = std::to_string(ids.skill);
            else if (field == "@state") cell = std::to_string(ids.state);
            else cell = field;
            if (found != target.rows.size() && Tsv::Field(target.rows[found], column) != cell)
                throw std::runtime_error("conflicting existing row " + std::string(key));
        }
        if (found != target.rows.size()) continue;
        if (creator != target.columns.end()) row[creator->second] = "MadMike";
        if (!result.ends_with('\n')) result += newline;
        for (std::size_t j = 0; j < row.size(); ++j) {
            if (j) result += '\t';
            result += row[j];
        }
        result += newline;
    }
    return result;
} catch (const std::exception& error) {
    throw std::runtime_error(std::string(error.what()) + " in " + fs::path(seed.filename).string());
}

std::string MakeManifest(const std::string& original, const TableIds& ids) {
    const Tsv table(original);
    const auto name = table.Column("Stat");
    const auto id = table.Column("*ID");
    const auto shift = table.Column("ValShift");
    const auto send = table.Column("Send Bits");
    const auto param = table.Column("Send Param Bits");
    const auto sign = table.Column("Signed");

    struct Stat { unsigned id, shift, bits; std::size_t sourceIndex; };
    std::vector<Stat> stats;
    for (std::size_t i = 0; i < InstallData::StatNames.size(); ++i) {
        const auto key = InstallData::StatNames[i];
        const auto index = table.Find(name, key);
        if (index == table.rows.size()) throw std::runtime_error("missing aura stat " + std::string(key));
        const auto& row = table.rows[index];
        const auto read = [&](std::size_t col) {
            const auto field = Tsv::Field(row, col);
            return field.empty() ? 0u : Number(field);
        };
        const auto width = read(send);
        const auto signedBits = read(sign);
        const Stat stat{read(id), read(shift), width > signedBits ? (std::min)(width - signedBits, 31u) : 0u, i};
        if (stat.id >= 511 || stat.shift >= 31 || !stat.bits || read(param))
            throw std::runtime_error("unsupported aura stat " + std::string(key));
        stats.push_back(stat);
    }
    std::sort(stats.begin(), stats.end(), [](const Stat& a, const Stat& b) { return a.id < b.id; });
    std::string result = std::to_string(InstallData::Format);
    const auto append = [&](unsigned n) { result += ' '; result += std::to_string(n); };
    append(ids.ring);
    append(ids.skill);
    append(ids.state);
    append(InstallData::RingLevel);
    append(static_cast<unsigned>(stats.size()));
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const auto& stat = stats[i];
        if (i && stat.id == stats[i - 1].id) throw std::runtime_error("duplicate aura stat ID");
        append(stat.id);
        append(stat.shift);
        result += ' ';
        result += InstallData::StatNames[stat.sourceIndex];
    }
    for (const auto& pair : InstallData::PairValues) {
        for (const auto& stat : stats) {
            const auto raw = static_cast<std::uint64_t>(pair[stat.sourceIndex]) << stat.shift;
            if (raw >= (1ull << stat.bits))
                throw std::runtime_error("an Oath bonus exceeds its stat's network range");
            append(static_cast<unsigned>(raw));
        }
    }
    return result;
}

void ValidateProperties(const std::string& original) {
    const Tsv table(original);
    const auto code = table.Column("code");
    for (const auto name : InstallData::RequiredProperties)
        if (table.Find(code, name) == table.rows.size())
            throw std::runtime_error("missing item property " + std::string(name));
}

struct JsonIndex {
    std::set<std::string, std::less<>> keys;
    std::set<unsigned> ids;
};

std::size_t EndString(std::string_view text, std::size_t start) {
    for (std::size_t i = start + 1; i < text.size(); ++i) {
        if (text[i] == '\\') ++i;
        else if (text[i] == '"') return i;
    }
    throw std::runtime_error("unterminated localization string");
}

JsonIndex IndexStrings(std::string_view text) {
    JsonIndex index;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.substr(i).starts_with("//")) {
            i = text.find('\n', i + 2);
            if (i == std::string_view::npos) break;
            continue;
        }
        if (text.substr(i).starts_with("/*")) {
            i = text.find("*/", i + 2);
            if (i == std::string_view::npos) throw std::runtime_error("unterminated localization comment");
            ++i;
            continue;
        }
        if (text[i] != '"') continue;
        const auto end = EndString(text, i);
        const auto property = text.substr(i + 1, end - i - 1);
        i = end;
        if (property != "Key" && property != "id") continue;
        const auto colon = text.find_first_not_of(Whitespace, i + 1);
        if (colon == std::string_view::npos || text[colon] != ':') continue;
        const auto start = text.find_first_not_of(Whitespace, colon + 1);
        if (start == std::string_view::npos) continue;
        if (property == "Key" && text[start] == '"') {
            i = EndString(text, start);
            index.keys.emplace(text.substr(start + 1, i - start - 1));
        } else if (property == "id" && text[start] >= '0' && text[start] <= '9') {
            auto last = text.find_first_not_of("0123456789", start);
            if (last == std::string_view::npos) last = text.size();
            index.ids.insert(Number(text.substr(start, last - start)));
            i = last - 1;
        }
    }
    return index;
}

unsigned NextStringId(std::set<unsigned>& ids) {
    for (unsigned id = 63000; id < 65000; ++id)
        if (ids.insert(id).second) return id;
    throw std::runtime_error("no free Oath localization IDs");
}

std::string MergeStrings(std::string_view original, const auto& seeds, std::set<unsigned>& ids,
        std::string_view manifest = {}) {
    const auto index = IndexStrings(original);
    const auto first = original.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    const auto close = original.find_last_not_of(Whitespace);
    if (first == std::string_view::npos || original[first] != '[' || original[close] != ']')
        throw std::runtime_error("expected a localization JSON array");

    const auto end = original.find_last_not_of(Whitespace, close - 1);
    if (end == std::string_view::npos || (original[end] != '[' && original[end] != '}'
            && original[end] != ',')) throw std::runtime_error("unsupported localization JSON array ending");
    const auto newline = original.find("\r\n") == std::string_view::npos ? "\n"sv : "\r\n"sv;
    std::string merged(original.substr(0, end + 1));
    const auto add = [&](std::string_view object) {
        if (merged.back() == '}') merged += ',';
        merged += newline;
        merged += "  {\"id\":";
        merged += std::to_string(NextStringId(ids));
        merged += ',';
        merged += object.substr(1);
    };
    for (const auto& seed : seeds)
        if (!index.keys.contains(seed.key)) add(seed.json);
    if (!manifest.empty()) {
        std::string object = "{\"Key\":\"" + std::string(InstallKey) + '"';
        for (const auto locale : InstallData::Locales) {
            object += ",\"";
            object += locale;
            object += "\":\"";
            object += manifest;
            object += '"';
        }
        object += '}';
        add(object);
    }
    if (merged.size() == end + 1) return std::string(original);
    merged += newline;
    merged += original.substr(close);
    return merged;
}

struct Edit {
    fs::path relative;
    std::optional<std::string> before;
    std::string after;
};

void QueueRecipeCleanup(const fs::path& root, std::vector<Edit>& edits) {
    const auto relative = fs::path(Excel) / L"cubemain.txt";
    auto original = ReadOptionalModFile(root, relative);
    if (!original) return;

    constexpr auto identify = "Unruned Oath testing: Scroll of Identify -> Dawn, Dusk, Eclipse\t"sv;
    constexpr auto portal = "Unruned Oath testing: Scroll of Town Portal -> Zenith, Midnight, Horizon\t"sv;
    std::string cleaned;
    cleaned.reserve(original->size());
    std::string_view remaining(*original);
    while (!remaining.empty()) {
        const auto end = remaining.find('\n');
        const auto line = remaining.substr(0, end == std::string_view::npos ? remaining.size() : end + 1);
        if (!line.starts_with(identify) && !line.starts_with(portal)) cleaned += line;
        remaining.remove_prefix(line.size());
    }
    if (cleaned.size() != original->size())
        edits.push_back({relative, std::move(original), std::move(cleaned)});
}

void WriteAtomic(const fs::path& target, std::string_view bytes, bool replaceExisting) {
    auto temporary = target;
    temporary += L".oath-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!file || !MoveFileExW(temporary.c_str(), target.c_str(), flags))
            throw std::runtime_error("could not write " + target.filename().string());
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

void ApplyEdits(const fs::path& root, const fs::path& backupRoot, const std::vector<Edit>& edits) {
    std::string createdFiles;
    for (const auto& edit : edits) {
        if (!edit.before) {
            createdFiles += edit.relative.generic_string() + '\n';
            continue;
        }
        if (edit.before == edit.after) continue;
        const auto backup = backupRoot / edit.relative;
        fs::create_directories(backup.parent_path());
        if (!fs::copy_file(root / edit.relative, backup) || ReadFile(backup) != *edit.before)
            throw std::runtime_error("could not back up " + edit.relative.filename().string());
    }
    if (!createdFiles.empty()) {
        fs::create_directories(backupRoot);
        WriteAtomic(backupRoot / L"created-files.txt", createdFiles, false);
    }
    std::vector<const Edit*> written;
    written.reserve(edits.size());
    try {
        for (const auto& edit : edits) {
            if (edit.before == edit.after) continue;
            if (ReadOptionalModFile(root, edit.relative) != edit.before)
                throw std::runtime_error("a mod file changed during setup");
            const auto target = root / edit.relative;
            fs::create_directories(target.parent_path());
            WriteAtomic(target, edit.after, edit.before.has_value());
            written.push_back(&edit);
        }
    } catch (...) {
        bool restored = true;
        for (auto i = written.rbegin(); i != written.rend(); ++i) {
            try {
                const auto* edit = *i;
                if (ReadModFile(root, edit->relative) != edit->after) restored = false;
                else if (edit->before) WriteAtomic(root / edit->relative, *edit->before, true);
                else fs::remove(root / edit->relative);
            } catch (...) {
                restored = false;
            }
        }
        if (!restored) throw std::runtime_error("could not restore every edited file; original files are in the install backup");
        throw;
    }
}

InstallResult Install(const D2RL::PluginContext& ctx) {
    const auto root = ModDataRoot(ctx);
    if (root.empty() || fs::is_regular_file(root)) return InstallResult::NoLooseData;
    InstallLock lock;
    if (InstallToken::Complete(ctx)) return InstallResult::AlreadyInstalled;
    const auto skillFile = fs::path(Strings) / L"skills.json";
    const auto itemFile = fs::path(Strings) / L"item-names.json";
    const auto propertiesFile = fs::path(Excel) / PropertiesFile;
    const auto statCostFile = fs::path(Excel) / StatCostFile;
    auto looseSkills = ReadOptionalModFile(root, skillFile);
    const bool installed = looseSkills && IndexStrings(*looseSkills).keys.contains(InstallKey);
    std::vector<Edit> edits;
    if (installed) {
        QueueRecipeCleanup(root, edits);
        if (edits.empty()) {
            InstallToken::Commit(ctx);
            return InstallResult::AlreadyInstalled;
        }
    }

    if (!ctx.pluginConfigPath || !ctx.pluginConfigPath[0])
        throw std::runtime_error("D2RLoader did not provide the mod's configuration path");
    const auto loader = fs::weakly_canonical(fs::path(ctx.pluginConfigPath).parent_path().parent_path());
    const auto mod = root.extension() == L".mpq" ? root.parent_path() : root;
    if (loader.filename() != L"d2rloader" || !IsWithin(mod, loader))
        throw std::runtime_error("D2RLoader's backup path is outside this mod");

    if (!installed) {
        std::optional<fs::path> baseRoot;
        const auto load = [&](const fs::path& relative, std::optional<std::string> loose) -> std::optional<Edit> {
            if (loose) return Edit{relative, loose, std::move(*loose)};
            if (!baseRoot) {
                baseRoot = fs::weakly_canonical(loader / SharedSetup / L"basefiles");
                if (!IsWithin(loader, *baseRoot))
                    throw std::runtime_error("the shared basefiles directory is outside the mod's loader folder");
            }
            const auto baseRelative = relative.lexically_relative(L"data");
            if (auto base = ReadOptionalModFile(*baseRoot, baseRelative))
                return Edit{relative, std::nullopt, std::move(*base)};
            const auto message = "Unruned Oath: missing required file " + PathText(root / relative)
                + ". Put an extracted copy here: " + PathText(*baseRoot / baseRelative);
            ctx.LogError(message.c_str());
            return std::nullopt;
        };
        auto skills = load(skillFile, std::move(looseSkills));
        auto items = load(itemFile, ReadOptionalModFile(root, itemFile));
        auto properties = load(propertiesFile, ReadOptionalModFile(root, propertiesFile));
        auto statCosts = load(statCostFile, ReadOptionalModFile(root, statCostFile));
        std::vector<Edit> tables;
        for (const auto& seed : InstallData::Tables) {
            const auto relative = fs::path(Excel) / seed.filename;
            if (auto table = load(relative, ReadOptionalModFile(root, relative)))
                tables.push_back(std::move(*table));
        }
        if (!skills || !items || !properties || !statCosts || tables.size() != InstallData::Tables.size())
            return InstallResult::Failed;
        if (!skills->before && IndexStrings(skills->after).keys.contains(InstallKey))
            throw std::runtime_error("basefiles skills.json contains an Oath installation marker. Extract an original copy.");

        QueueRecipeCleanup(root, edits);
        ValidateProperties(properties->after);
        edits.push_back(std::move(*properties));
        TableIds ids{};
        for (std::size_t i = 0; i < tables.size(); ++i) {
            auto& table = tables[i];
            table.after = MergeTable(InstallData::Tables[i], table.after, ids);
            edits.push_back(std::move(table));
        }
        if (ids.state >= 0x8000) throw std::runtime_error("the Oath state ID is outside the supported range");
        const auto manifest = MakeManifest(statCosts->after, ids);
        if (manifest.size() >= 4096) throw std::runtime_error("the generated installation data is too large");
        edits.push_back(std::move(*statCosts));

        std::set<unsigned> used;
        const auto strings = root / Strings;
        if (fs::is_directory(strings)) {
            for (const auto& file : fs::directory_iterator(strings)) {
                if (file.path().extension() != L".json" || !file.is_regular_file()) continue;
                auto index = IndexStrings(ReadModFile(root, fs::path(Strings) / file.path().filename()));
                used.merge(index.ids);
            }
        }
        if (!items->before) used.merge(IndexStrings(items->after).ids);
        if (!skills->before) used.merge(IndexStrings(skills->after).ids);
        items->after = MergeStrings(items->after, InstallData::ItemNames, used);
        edits.push_back(std::move(*items));
        skills->after = MergeStrings(skills->after, InstallData::Skills, used, manifest);
        // Commit the manifest last. If the process stops early, the next launch can
        // reuse matching rows and finish the remaining edits.
        edits.push_back(std::move(*skills));
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t timestamp[48]{};
    swprintf_s(timestamp, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu",
        static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth), static_cast<unsigned>(now.wDay),
        static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
        static_cast<unsigned>(now.wMilliseconds), GetCurrentProcessId());
    const auto backupRoot = fs::weakly_canonical(
        loader / SharedSetup / L"backups/runeword-free-rings" / timestamp);
    if (!IsWithin(loader, backupRoot)) throw std::runtime_error("the backup directory is outside the mod's loader folder");
    ApplyEdits(root, backupRoot, edits);
    InstallToken::Commit(ctx);
    return installed ? InstallResult::RecipesRemoved : InstallResult::Installed;
}
}

InstallResult RunFirstRunInstall(const D2RL::PluginContext& context) noexcept {
    try {
        const auto result = Install(context);
        if (result == InstallResult::Installed)
            context.LogInfo("Unruned Oath: first-run install complete. Restart D2RLoader to load the rings.");
        else if (result == InstallResult::RecipesRemoved)
            context.LogInfo("Unruned Oath: removed the old Cube recipes. Restart D2RLoader to refresh the game tables.");
        else if (result == InstallResult::NoLooseData)
            context.LogInfo("Unruned Oath: checking for a staged overlay. Automatic setup requires an unpacked mod.");
        return result;
    } catch (const std::exception& error) {
        char message[512]{};
        std::snprintf(message, sizeof message, "Unruned Oath: first-run setup failed. %s", error.what());
        context.LogError(message);
        return InstallResult::Failed;
    }
}
