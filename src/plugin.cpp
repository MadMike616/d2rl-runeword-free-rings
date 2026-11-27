#include <D2RLPlugin/api.h>
#include <Windows.h>
#include "installer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "runeword-free-rings",
    .name = "Runeword-Free Rings",
    .version = "1.0.2",
    .author = "MadMike",
    .description = "Grants the Unruned Oath bonus only without equipped runewords.",
    .flags = D2RL::PluginFlags::ModScopedOnly | D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

// These native reads use byte witnesses from Drop Logger's independently
// validated local-player lookup. No native code or slots are replaced.
constexpr std::uint64_t ActiveGameStore = 0x4057AE;
constexpr std::uint64_t ActiveGameSlot = ActiveGameStore + 7 + 0x0269886B;
constexpr std::uint64_t LookupEntry = 0x48FE80;
constexpr std::uint64_t GameFromUnitEntry = 0x48FF00;
constexpr std::uint64_t ClientFromPlayerEntry = 0x48FDE0;
constexpr std::uint64_t LocalDataContextEntry = 0x08B2D0;
constexpr std::uint64_t LocalPlayerEntry = 0x09A480;
constexpr std::uint8_t ActiveGameBytes[]{0x48,0x89,0x05,0x6B,0x88,0x69,0x02,0x48,0x83,0xC4,0x28,0xC3};
constexpr std::uint8_t LookupBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xD8,0x8B,0xF2,0x48,0x8B,0xF9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x38,
    0x48,0x8D,0x4C,0x24,0x38,0xE8,0x46,0xD1,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x83,
    0xFE,0x05,0x75,0x09,0x48,0x8D,0x87,0x30,0x36,0x00,0x00,0xEB,0x0D,0x44,0x8B,0xC3,
    0x48,0x8B,0xD7,0x8B,0xCE,0xE8,0xE6,0x26,0x00,0x00,0x48,0x85,0xC0,0x74,0x19,0x48,
    0x8B,0x00,0x48,0x85,0xC0,0x74,0x11,0x39,0x58,0x08,0x74,0x0E,0x48,0x8B,0x80,0x58,
    0x01,0x00,0x00,0x48,0x85,0xC0,0x75,0xEF,0x33,0xC0,0x48,0x8B,0x5C,0x24,0x30,0x48,
    0x8B,0x74,0x24,0x40,0x48,0x83,0xC4,0x20,0x5F,0xC3};
constexpr std::uint8_t GameFromUnitBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x20,0x88,0x4C,
    0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x44,0xD9,0xFF,0xFF,0x84,0xC0,0x74,0x01,
    0xCC,0x48,0x8B,0x83,0xD8,0x00,0x00,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3,0x48,0x8B,
    0x81,0xD8,0x00,0x00,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3};
constexpr std::uint8_t ClientFromPlayerBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x1E,0xE8,0xDD,
    0xBB,0xEB,0xFF,0x85,0xC0,0x75,0x15,0x48};
constexpr std::uint8_t LocalDataContextBytes[]{0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3};
constexpr std::uint8_t LocalPlayerBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08,0x0F,0x83,0x85,
    0x00,0x00,0x00,0x8B,0xD9,0x48,0x89,0x5C,0x24,0x38,0x48,0x83,0xFB,0x08,0x72,0x19};

// Buff HUD's exact-state call-site witness. The rel32 resolves the helper;
// checking the whole instruction also rejects a replacement of the call.
constexpr std::uint64_t LookupStateCall = 0x433FFC;
constexpr std::uint8_t LookupStateBytes[]{0xE8,0x3F,0x19,0xEC,0xFF};
constexpr std::uint64_t LookupStateEntry = LookupStateCall + 5 - 0x13E6C1;
static_assert(LookupStateEntry == 0x2F5940);
// 1.3.1-beta D2RCore stat adapters, qualified by the export entry and bytes.
// The only write is to the transient list owned by this set's own aura.
constexpr std::uint8_t SetStatBytes[]{
    0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x48,0x44,0x89,0xC6,0x48,0x8B,0x05,0x9E,0x96,
    0xEC,0xFF,0x48,0x31,0xE0,0x48,0x89,0x44,0x24,0x40,0x48,0x85,0xD2,0x41,0x0F,0x94};
constexpr std::uint8_t ReadStatBytes[]{
    0x48,0x83,0xEC,0x28,0xE8,0xC7,0x69,0xBA,0xFF,0x89,0xC0,0x48,0x83,0xC4,0x28,0xC3};
constexpr std::uint8_t UnitVersionWitness[]{0x0F,0xB6,0x81,0xBD,0x01,0x00,0x00};
constexpr std::uint8_t SendChangedStatesBytes[]{
    0x48,0x83,0xEC,0x28,0x45,0x31,0xC0,0xE8,0x54,0x16,0xBB,0xFF,0x0F,0xB6,0xC0,0x48,
    0x83,0xC4,0x28,0xC3};
// The sender loads Unit+0x88, takes the active words at StatListEx+0xAF0,
// then takes the changed words immediately after ceil(stateCount / 32).
// The second witness confirms a changed bit whose active bit is clear sends
// state removal instead of serializing the current list.
constexpr std::uint8_t StateMasksBytes[]{
    0x48,0x8B,0xA8,0x90,0x02,0x00,0x00,0x4C,0x8B,0x80,0x98,0x02,0x00,0x00,0x48,0x8B,
    0x8E,0x88,0x00,0x00,0x00,0x48,0x85,0xC9,0x0F,0x94,0xC0,0x49,0x81,0xF8,0x01,0x80,
    0x00,0x00,0x0F,0x93,0xC2,0x08,0xC2,0x4C,0x89,0x44,0x24,0x50,0x75,0x1D,0x48,0x8D,
    0x81,0xF0,0x0A,0x00,0x00,0x49,0x8D,0x50,0x1F,0x48,0xC1,0xEA,0x05,0x48,0x8D,0x9C,
    0x91,0xF0,0x0A,0x00,0x00,0x48,0x0F,0x44,0xDA};
constexpr std::uint8_t StateOffBytes[]{0x42,0x8B,0x84,0xA4,0xA0,0x02,0x00,0x00,0x0F,0xA3,0xC8,0x73,0xA9};
constexpr std::size_t UnitStatListOffset = 0x88;
constexpr std::size_t ActiveStateMaskOffset = 0xAF0;
// D2RCore's Chronicle preview predicate checks unit type, ItemData at
// Unit+0x10, and the runeword bit in the flags at ItemData+0x18.
constexpr std::uint8_t RunewordLayoutBytes[]{
    0x48,0x85,0xC9,0x74,0x20,0x83,0x39,0x04,0x75,0x1B,0x48,0x8B,0x41,0x10,0x48,0x85,
    0xC0,0x74,0x12,0xB9,0x00,0x00,0x00,0xC4,0x23,0x48,0x18,0x81,0xF9,0x00,0x00,0x00,
    0x84,0x0F,0x94,0xC0,0xC3,0x31,0xC0,0xC3};
constexpr std::uint32_t RunewordFlag = 0x04000000;

constexpr int NoPair = -1;
constexpr std::array RingNames{"Dawn", "Dusk", "Eclipse", "Zenith", "Midnight", "Horizon"};
constexpr std::size_t MaxStats = 32;
constexpr std::size_t PairCount = RingNames.size() * (RingNames.size() - 1) / 2;
constexpr auto RotwBank = static_cast<std::uint8_t>(D2RL::DataTables::Bank::Rotw);
using OathValues = std::array<int, MaxStats>;
constexpr OathValues NoBonus{};

struct OathStat {
    unsigned id, shift;
};
struct OathPair {
    unsigned first, second;
    char tooltip[64];
    OathValues values;
};
struct OathDefinition {
    std::array<unsigned, RingNames.size()> rings;
    unsigned skill, state, statCount;
    std::array<OathStat, MaxStats> stats;
    std::array<OathPair, PairCount> pairs;
};
// Assigned once on the game thread before TableReady publishes the definition.
OathDefinition Oath{};
bool DefinitionLoaded{};
unsigned StateWordCount{}; // game thread

using FindUnit = void* (__fastcall*)(void*, std::uint32_t, std::uint32_t);
using GetGame = void* (__fastcall*)(void*);
using FindState = void* (__fastcall*)(void*, std::int32_t);
using SetStat = bool (__fastcall*)(std::uint8_t, void*, std::int32_t, std::int32_t, std::uint32_t);
using ReadStat = std::int32_t (__fastcall*)(std::uint8_t, void*, std::int32_t, std::uint32_t);
using IsChroniclePreview = bool (__fastcall*)(void*);
using SendStates = bool (__fastcall*)(void*, void*);
using GetLocalDataContext = std::int32_t (__fastcall*)();
using GetLocalPlayer = void* (__fastcall*)(std::int32_t);
FindUnit FindServerUnit{};
GetGame UnitGame{};
GetGame PlayerClient{};
GetLocalDataContext LocalDataContext{};
GetLocalPlayer LocalPlayer{};
FindState FindAura{};
SetStat SetBonus{};
ReadStat ReadBonus{};
SendStates SendChangedStates{};
bool HasNativeRunewordLayout{};
bool HasItemDetails{};

const D2RL::PluginContext* Context{};
const D2RL::InventoryService* Inventory{};
const D2RL::ItemService* Items{};
const D2RL::ThreadService* Threads{};
const D2RL::LifecycleService* Lifecycle{};
const D2RL::DataTableService* Tables{};
const D2RL::LocalizationService* Strings{};
const D2RL::SharedEventService* Events{};
HANDLE Stop{}, Worker{};
std::atomic_bool TableReady{}, Queued{};
std::atomic_int TooltipPair{NoPair};
std::atomic_uint32_t PlayerId{D2RL::Lifecycle::InvalidUnitId};
// Only the game callback reads or writes this. Reset by session generation.
std::atomic_uint64_t Session{};
std::uint64_t ObservedSession{};
int PublishedPair{NoPair};
bool PublishPending{}; // game thread
constexpr auto RingCode = D2RL::Items::MakeItemCode("rin");

template<typename T>
bool Read(const void* source, T& out) noexcept {
    SIZE_T size{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, &out, sizeof out, &size)
        && size == sizeof out;
}

template<typename Fn, std::size_t N>
Fn CoreFunction(HMODULE core, const char* name, const std::uint8_t (&expected)[N]) noexcept {
    const auto proc = GetProcAddress(core, name);
    std::uint8_t bytes[N]{};
    if (!Read(reinterpret_cast<const void*>(proc), bytes)
            || std::memcmp(bytes, expected, N) != 0) return nullptr;
    return reinterpret_cast<Fn>(reinterpret_cast<std::uintptr_t>(proc));
}

template<std::size_t N>
bool Matches(const void* address, const std::uint8_t (&expected)[N]) noexcept {
    std::uint8_t bytes[N]{};
    return Read(address, bytes) && std::memcmp(bytes, expected, N) == 0;
}

bool ResolveNative(const D2RL::PluginContext* context) noexcept {
    const auto core = GetModuleHandleW(L"D2RCore.dll");
    if (!core || !context->exeBase
            || !context->CheckExpectedBytes(ActiveGameStore, ActiveGameBytes, sizeof ActiveGameBytes)
            || !context->CheckExpectedBytes(LookupEntry, LookupBytes, sizeof LookupBytes)
            || !context->CheckExpectedBytes(GameFromUnitEntry, GameFromUnitBytes, sizeof GameFromUnitBytes)
            || !context->CheckExpectedBytes(ClientFromPlayerEntry, ClientFromPlayerBytes, sizeof ClientFromPlayerBytes)
            || !context->CheckExpectedBytes(LookupStateCall, LookupStateBytes, sizeof LookupStateBytes)) return false;
    const auto unitWriter = GetProcAddress(core, "SetWideUnitStat");
    std::uint8_t versionBytes[sizeof UnitVersionWitness]{};
    if (!unitWriter || !Read(reinterpret_cast<const std::uint8_t*>(unitWriter) + 0x37, versionBytes)
            || std::memcmp(versionBytes, UnitVersionWitness, sizeof versionBytes)) return false;
    SetBonus = CoreFunction<SetStat>(core, "SetWideListStat", SetStatBytes);
    ReadBonus = CoreFunction<ReadStat>(core, "ReadWideListStat", ReadStatBytes);
    SendChangedStates = CoreFunction<SendStates>(core, "SendChangedWideStateStats", SendChangedStatesBytes);
    const auto* coreBase = reinterpret_cast<const std::uint8_t*>(core);
    if (!Matches(coreBase + 0x3E5A64, StateMasksBytes)
            || !Matches(coreBase + 0x3E5BCC, StateOffBytes)) return false;
    HasNativeRunewordLayout = CoreFunction<IsChroniclePreview>(
        core, "IsChronicleRunewordPreview", RunewordLayoutBytes) != nullptr;
    FindAura = reinterpret_cast<FindState>(context->exeBase + LookupStateEntry);
    FindServerUnit = reinterpret_cast<FindUnit>(context->exeBase + LookupEntry);
    UnitGame = reinterpret_cast<GetGame>(context->exeBase + GameFromUnitEntry);
    PlayerClient = reinterpret_cast<GetGame>(context->exeBase + ClientFromPlayerEntry);
    if (context->CheckExpectedBytes(LocalDataContextEntry, LocalDataContextBytes, sizeof LocalDataContextBytes)
            && context->CheckExpectedBytes(LocalPlayerEntry, LocalPlayerBytes, sizeof LocalPlayerBytes)) {
        LocalDataContext = reinterpret_cast<GetLocalDataContext>(context->exeBase + LocalDataContextEntry);
        LocalPlayer = reinterpret_cast<GetLocalPlayer>(context->exeBase + LocalPlayerEntry);
    }
    return SetBonus && ReadBonus && SendChangedStates;
}

void* ResolveUnit(void* game, std::uint32_t type, std::uint32_t id) noexcept {
    if (!game || id == D2RL::Lifecycle::InvalidUnitId) return nullptr;
    __try {
        auto* unit = FindServerUnit(game, type, id);
        std::array<std::uint32_t, 3> identity{};
        if (!Read(unit, identity) || identity[0] != type || identity[2] != id
                || UnitGame(unit) != game) return nullptr;
        return unit;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void* ActiveGame(const D2RL::PluginContext* context) noexcept {
    void* game{};
    if (!Read(reinterpret_cast<const void*>(context->exeBase + ActiveGameSlot), game)) return nullptr;
    return game;
}

unsigned OathRingId(const D2RL::Items::ItemInfo& item) noexcept {
    if (item.quality != D2RL::Items::Quality::Set || item.code != RingCode) return 0;
    for (const auto row : Oath.rings)
        if (item.qualityRecordId == static_cast<std::int32_t>(row)) return row;
    return 0;
}

int FindPair(unsigned first, unsigned second) noexcept {
    for (std::size_t i = 0; i < Oath.pairs.size(); ++i) {
        const auto& pair = Oath.pairs[i];
        if ((pair.first == first && pair.second == second)
                || (pair.first == second && pair.second == first)) return static_cast<int>(i);
    }
    return NoPair;
}


bool GetRunewordFlag(const D2RL::PluginContext* ctx, void* game,
        D2RL::ItemHandle handle, bool& runeword) noexcept {
    if (HasItemDetails) {
        D2RL::Items::ItemDetails details{.structSize = D2RL::Items::ItemDetailsSize};
        if (Items->getItemDetails(ctx, handle, &details, nullptr, 0) != D2RL::Items::Result::Success)
            return false;
        runeword = (details.flags & D2RL::Items::ItemDetailRuneword) != 0;
        return true;
    }
    if (!HasNativeRunewordLayout) return false;
    D2RL::Items::ItemInfo info{.structSize = D2RL::Items::ItemInfoSize};
    if (Items->getItemInfo(ctx, handle, &info) != D2RL::Items::Result::Success
            || info.container != D2RL::Items::ItemContainer::Equipment) return false;
    const auto* unit = static_cast<const std::uint8_t*>(ResolveUnit(game, 4, info.runtimeId));
    const std::uint8_t* itemData{};
    std::uint32_t flags{};
    if (!unit || !Read(unit + 0x10, itemData) || !itemData || !Read(itemData + 0x18, flags)) return false;
    runeword = (flags & RunewordFlag) != 0;
    return true;
}

// The equipment service includes both the active and the alternate weapon slots.
int EquippedPair(const D2RL::PluginContext* ctx, D2RL::PlayerHandle player, void* game) noexcept {
    std::array<unsigned, 2> rings{};
    for (const int slot : {6, 7}) {
        D2RL::ItemHandle item{};
        const auto result = Inventory->getEquippedItem(ctx, player, slot, &item);
        D2RL::Items::ItemInfo info{.structSize = D2RL::Items::ItemInfoSize};
        if (result != D2RL::Inventory::Result::Success || item == D2RL::InvalidItemHandle
                || Items->getItemInfo(ctx, item, &info) != D2RL::Items::Result::Success
                || info.bodyLocation != slot) return NoPair;
        rings[slot - 6] = OathRingId(info);
    }
    const auto candidate = FindPair(rings[0], rings[1]);
    if (candidate == NoPair) return NoPair;
    for (int slot = 1; slot <= 12; ++slot) {
        if (slot == 6 || slot == 7) continue;
        D2RL::ItemHandle item{};
        const auto result = Inventory->getEquippedItem(ctx, player, slot, &item);
        if (result == D2RL::Inventory::Result::NotFound
                || (result == D2RL::Inventory::Result::Success && item == D2RL::InvalidItemHandle)) continue;
        if (result != D2RL::Inventory::Result::Success) return NoPair;
        bool runeword{};
        if (!GetRunewordFlag(ctx, game, item, runeword) || runeword) return NoPair;
    }
    return candidate;
}

bool WriteAuraStat(std::uint8_t bank, void* list, int id, int value) noexcept {
    __try {
        return SetBonus(bank, list, id, value, 0) && ReadBonus(bank, list, id, 0) == value;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

int UpdateBonus(void* unit, int pair) noexcept {
    __try {
        const bool enabled = pair != NoPair;
        auto* list = static_cast<std::uint8_t*>(FindAura(unit, static_cast<int>(Oath.state)));
        if (!list) return enabled ? -1 : 0;
        std::uint32_t state{}, skill{};
        std::uint8_t bank{};
        if (!Read(list + 0x20, state) || state != Oath.state || !Read(list + 0x28, skill)
                || skill != Oath.skill || !Read(static_cast<const std::uint8_t*>(unit) + 0x1BD, bank)
                || bank != RotwBank) return -1;
        const auto& desired = enabled ? Oath.pairs[pair].values : NoBonus;
        OathValues previous{};
        for (std::size_t i = 0; i < Oath.statCount; ++i)
            previous[i] = ReadBonus(bank, list, static_cast<int>(Oath.stats[i].id), 0);
        if (previous == desired) return 0;
        bool success = true;
        for (std::size_t i = 0; i < Oath.statCount; ++i) {
            if (previous[i] == desired[i]) continue;
            if (!WriteAuraStat(bank, list, static_cast<int>(Oath.stats[i].id), desired[i])) {
                success = false;
                if (enabled) break;
            }
        }
        if (!success) {
            // Never publish a partial bonus. Clear every owned stat on a failed enable.
            if (enabled)
                for (std::size_t i = 0; i < Oath.statCount; ++i)
                    WriteAuraStat(bank, list, static_cast<int>(Oath.stats[i].id), 0);
            return -1;
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

bool SendAuraState(void* unit, void* client, volatile LONG* activeWord,
        volatile LONG* changedWord, LONG stateBit, bool enabled) {
    // The receiver updates only stats present in a state packet. For removal,
    // serialize the stock state-off message so the client drops its old list
    // even when setting the server stat to zero removed the stat entry.
    const LONG previousChanged = InterlockedOr(changedWord, stateBit);
    bool restoreActive = false;
    __try {
        if (!enabled) restoreActive = (InterlockedAnd(activeWord, ~stateBit) & stateBit) != 0;
        return SendChangedStates(unit, client);
    } __finally {
        if (restoreActive) InterlockedOr(activeWord, stateBit);
        if (!(previousChanged & stateBit)) InterlockedAnd(changedWord, ~stateBit);
    }
}

bool PublishAura(void* unit, bool enabled) noexcept {
    __try {
        auto* client = PlayerClient(unit);
        auto* player = static_cast<std::uint8_t*>(unit);
        std::uint8_t* statList{};
        std::uint8_t bank{};
        std::uint32_t flags{};
        if (!client || !Read(player + 0x1BD, bank) || bank != RotwBank
                || !Read(player + UnitStatListOffset, statList) || !statList
                || !Read(statList + 0x1C, flags) || !(flags & 0x80000000u)) return false;
        auto* activeWords = reinterpret_cast<LONG*>(statList + ActiveStateMaskOffset);
        auto* activeWord = activeWords + Oath.state / 32;
        auto* changedWord = activeWord + StateWordCount;
        const auto stateBit = static_cast<LONG>(1u << (Oath.state % 32));
        LONG activeMask{}, changedMask{};
        if (!Read(activeWord, activeMask) || !Read(changedWord, changedMask)
                || (enabled && !(activeMask & stateBit))) return false;
        return SendAuraState(unit, client, activeWord, changedWord, stateBit, enabled);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void SyncClientAura(void* unit, int pair) noexcept {
    if (PublishedPair != NoPair && PublishedPair != pair) {
        // An on-packet merges its fields into an existing list. Send state-off
        // first so stats missing from the new pair disappear on the client.
        if (!PublishAura(unit, false)) return;
        PublishedPair = NoPair;
        if (pair == NoPair) {
            PublishPending = false;
            return;
        }
    }
    if (!PublishAura(unit, pair != NoPair)) return;
    PublishedPair = pair;
    PublishPending = false;
}

void __cdecl Tick(const D2RL::PluginContext* ctx, void*) noexcept {
    Queued.store(false);
    const auto id = PlayerId.load();
    const auto session = Session.load();
    if (!TableReady.load() || id == D2RL::Lifecycle::InvalidUnitId) return;
    if (ObservedSession != session) {
        ObservedSession = session;
        PublishedPair = NoPair;
        PublishPending = false;
    }
    void* game = ActiveGame(ctx);
    void* unit = ResolveUnit(game, 0, id);
    if (!unit) { TooltipPair.store(NoPair); return; }
    D2RL::PlayerHandle handle{};
    const auto pair = Inventory->getLocalPlayer(ctx, &handle) == D2RL::Inventory::Result::Success
        ? EquippedPair(ctx, handle, game) : NoPair;
    if (PlayerId.load() != id || Session.load() != session || !TableReady.load()) return;
    const auto update = UpdateBonus(unit, pair);
    const int activePair = update >= 0 ? pair : NoPair;
    if (update == 1 || activePair != PublishedPair) PublishPending = true;
    if (PublishPending) {
        TooltipPair.store(NoPair);
        SyncClientAura(unit, activePair);
    }
    TooltipPair.store(PublishPending ? NoPair : PublishedPair);
}

DWORD WINAPI Poll(void*) noexcept {
    while (WaitForSingleObject(Stop, 100) == WAIT_TIMEOUT) {
        if (!TableReady.load() || PlayerId.load() == D2RL::Lifecycle::InvalidUnitId
                || Queued.exchange(true)) continue;
        if (Threads->runOnGameThread(Context, Tick, nullptr) != D2RL::Threads::Result::Success)
            Queued.store(false);
    }
    return 0;
}

void __cdecl OnGameplay(const D2RL::PluginContext*,
        const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
    if (!event || event->structSize < D2RL::Lifecycle::GameplayEventRequiredSize) return;
    switch (event->kind) {
    case D2RL::Lifecycle::GameplayEventKind::GameJoined:
    case D2RL::Lifecycle::GameplayEventKind::GameLeft:
        TooltipPair.store(NoPair);
        PlayerId.store(D2RL::Lifecycle::InvalidUnitId);
        Session.store(event->sessionGeneration);
        Queued.store(false);
        break;
    case D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady:
        Session.store(event->sessionGeneration);
        PlayerId.store(event->playerId);
        break;
    default: break;
    }
}

std::string_view NextField(std::string_view& text) noexcept {
    const auto end = text.find(' ');
    const auto field = text.substr(0, end);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    return field;
}

bool ReadNumber(std::string_view& text, unsigned& value) noexcept {
    const auto field = NextField(text);
    if (field.empty()) return false;
    const auto end = field.data() + field.size();
    const auto result = std::from_chars(field.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

bool ParseDefinition(std::string_view text, OathDefinition& definition) noexcept {
    unsigned format{}, firstRing{}, itemLevel{};
    if (!ReadNumber(text, format) || format != 1
            || !ReadNumber(text, firstRing) || firstRing == 0
            || firstRing > static_cast<unsigned>(std::numeric_limits<int>::max()) - (RingNames.size() - 1)
            || !ReadNumber(text, definition.skill) || definition.skill == 0
            || !ReadNumber(text, definition.state) || definition.state == 0
            || !ReadNumber(text, itemLevel) || itemLevel < 1 || itemLevel > 99
            || !ReadNumber(text, definition.statCount) || definition.statCount == 0
            || definition.statCount > definition.stats.size()) return false;

    for (std::size_t i = 0; i < definition.rings.size(); ++i)
        definition.rings[i] = firstRing + static_cast<unsigned>(i);
    for (std::size_t i = 0; i < definition.statCount; ++i) {
        auto& stat = definition.stats[i];
        if (!ReadNumber(text, stat.id) || stat.id >= 511 || (i && stat.id <= definition.stats[i - 1].id)
                || !ReadNumber(text, stat.shift) || stat.shift >= 31) return false;
        const auto name = NextField(text);
        if (name.empty() || name.size() > 39
                || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string_view::npos)
            return false;
    }

    std::size_t pairIndex{};
    for (std::size_t first = 0; first < RingNames.size(); ++first) {
        for (std::size_t second = first + 1; second < RingNames.size(); ++second) {
            auto& pair = definition.pairs[pairIndex++];
            pair.first = definition.rings[first];
            pair.second = definition.rings[second];
            std::snprintf(pair.tooltip, sizeof pair.tooltip, "d2r:UnrunedOathPair%s%s",
                RingNames[first], RingNames[second]);
            bool hasBonus = false;
            for (std::size_t i = 0; i < definition.statCount; ++i) {
                unsigned raw{};
                if (!ReadNumber(text, raw) || raw > static_cast<unsigned>(std::numeric_limits<int>::max())
                        || (raw & ((1u << definition.stats[i].shift) - 1u))) return false;
                pair.values[i] = static_cast<int>(raw);
                hasBonus |= raw != 0;
            }
            if (!hasBonus) return false;
        }
    }
    return text.empty();
}

bool LoadDefinition(const D2RL::PluginContext* ctx, OathDefinition& definition) noexcept {
    std::array<char, 4096> buffer{};
    std::uint32_t size{};
    const auto result = Strings->getStringByKey(ctx, "d2r:UnrunedOathInstallData", buffer.data(),
        static_cast<std::uint32_t>(buffer.size()), &size);
    return result == D2RL::Localization::Result::Success && size > 1
        && size <= buffer.size() && buffer[size - 1] == '\0'
        && ParseDefinition({buffer.data(), size - 1}, definition);
}

bool GetRotwTable(const D2RL::PluginContext* ctx, D2RL::DataTables::TableId id,
        D2RL::DataTables::TableView& view) noexcept {
    view.structSize = D2RL::DataTables::TableViewSize;
    return Tables->getTable(ctx, D2RL::DataTables::Bank::Rotw, id, &view)
            == D2RL::DataTables::Result::Success && view.rows && view.rowSize && view.rowCount;
}

bool MatchRingRows(const D2RL::DataTables::TableView& items, const OathDefinition& definition) noexcept {
    // D2RCore reads the compiled index at row + 2. The base item FourCC
    // follows at row + 0x28, immediately before the flags at row + 0x2C.
    constexpr std::size_t SetItemRowSize = 0x1CC;
    constexpr std::size_t NameOffset = 0x02;
    constexpr std::size_t CodeOffset = 0x28;
    if (items.rowSize != SetItemRowSize || definition.rings.back() >= items.rowCount) return false;
    for (std::size_t i = 0; i < RingNames.size(); ++i) {
        std::array<char, CodeOffset + 4> row{};
        const auto offset = static_cast<std::size_t>(definition.rings[i]) * items.rowSize;
        if (!Read(static_cast<const std::uint8_t*>(items.rows) + offset, row)) return false;
        char expected[CodeOffset - NameOffset]{};
        const auto length = std::snprintf(expected, sizeof expected, "Oath of %s", RingNames[i]);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof expected
                || std::memcmp(row.data() + NameOffset, expected, static_cast<std::size_t>(length) + 1) != 0
                || std::memcmp(row.data() + CodeOffset, "rin ", 4) != 0) return false;
    }
    return true;
}

void __cdecl OnTables(const D2RL::PluginContext* ctx, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    TableReady.store(false);
    TooltipPair.store(NoPair);
    OathDefinition candidate{};
    if (!DefinitionLoaded && !LoadDefinition(ctx, candidate)) {
        ctx->LogWarn("Unruned Oath: installation data missing or invalid. Stage the Oath overlay for this mod.");
        return;
    }
    const auto& definition = DefinitionLoaded ? Oath : candidate;
    using Id = D2RL::DataTables::TableId;
    D2RL::DataTables::TableView items{}, skills{}, states{}, stats{};
    if (!GetRotwTable(ctx, Id::SetItems, items) || !MatchRingRows(items, definition)) {
        ctx->LogError("Unruned Oath: the installed ring records do not match the loaded RotW SetItems table.");
        return;
    }
    if (!GetRotwTable(ctx, Id::Skills, skills) || definition.skill >= skills.rowCount
            || !GetRotwTable(ctx, Id::States, states) || definition.state >= states.rowCount
            || states.rowSize != 0x44 || states.rowCount > 0x8000
            || !GetRotwTable(ctx, Id::ItemStatCost, stats)
            || definition.stats[definition.statCount - 1].id >= stats.rowCount) {
        ctx->LogError("Unruned Oath: the installed aura or stats do not match the loaded RotW tables.");
        return;
    }
    if (!DefinitionLoaded) {
        Oath = candidate;
        DefinitionLoaded = true;
    }
    StateWordCount = (states.rowCount + 31) / 32;
    TableReady.store(true);
}

bool ReadClientAura(OathValues& bonus) noexcept {
    __try {
        const auto index = LocalDataContext();
        if (index < 0 || index >= 8) return false;
        auto* unit = LocalPlayer(index);
        std::array<std::uint32_t, 3> identity{};
        std::uint8_t bank{};
        if (!Read(unit, identity) || identity[0] != 0 || identity[2] != PlayerId.load()
                || !Read(static_cast<const std::uint8_t*>(unit) + 0x1BD, bank) || bank != RotwBank)
            return false;
        auto* list = FindAura(unit, static_cast<int>(Oath.state));
        for (std::size_t i = 0; i < Oath.statCount; ++i)
            bonus[i] = list ? ReadBonus(bank, list, static_cast<int>(Oath.stats[i].id), 0) : 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ClientBonusMatches(int pair) noexcept {
    if (pair == NoPair || !LocalDataContext || !LocalPlayer) return true;
    OathValues bonus{};
    return !ReadClientAura(bonus) || bonus == Oath.pairs[pair].values;
}

int ActiveTooltipPair(const void* item, int bodyLocation) noexcept {
    const auto active = TooltipPair.load();
    if (active == NoPair) return NoPair;
    std::array<std::uint32_t, 3> identity{};
    if (item && (!Read(item, identity) || identity[0] != 4)) return NoPair;
    if (!item && bodyLocation != 6 && bodyLocation != 7) return NoPair;
    D2RL::PlayerHandle player{};
    if (Inventory->getLocalPlayer(Context, &player) != D2RL::Inventory::Result::Success) return NoPair;
    const auto& pair = Oath.pairs[active];
    for (const int slot : {6, 7}) {
        if (!item && slot != bodyLocation) continue;
        D2RL::ItemHandle handle{};
        D2RL::Items::ItemInfo info{.structSize = D2RL::Items::ItemInfoSize};
        if (Inventory->getEquippedItem(Context, player, slot, &handle) != D2RL::Inventory::Result::Success
                || Items->getItemInfo(Context, handle, &info) != D2RL::Items::Result::Success
                || info.container != D2RL::Items::ItemContainer::Equipment || info.bodyLocation != slot
                || (item && info.runtimeId != identity[2])) continue;
        const auto ring = OathRingId(info);
        if ((ring == pair.first || ring == pair.second)
                && ClientBonusMatches(active) && active == TooltipPair.load()) return active;
    }
    return NoPair;
}

struct GameTooltipText {
    char* data;
    std::uint64_t size;
    std::uint64_t capacity;
};
static_assert(sizeof(GameTooltipText) == 24 && offsetof(GameTooltipText, size) == 8);

using AssignGameText = GameTooltipText* (__fastcall*)(GameTooltipText*, const char*, std::size_t);
AssignGameText AssignTooltipText{};
constexpr std::uintptr_t AssignTextRva = 0x80CF0;
// The game reserves space through its own allocator, copies the bytes,
// and writes both the terminator and the string's length.
constexpr std::uint8_t AssignTextBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
    0x48,0x83,0xEC,0x20,0x48,0x8B,0xF2,0x48,0x8B,0xEA,0x48,0x2B,0x31,0x49,0x8B,0xD0,
    0x49,0x8B,0xD8,0x48,0x8B,0xF9,0xE8,0xF5,0x54,0xFF,0xFF,0x48,0x8B,0x0F,0x48,0x85,
    0xDB,0x74,0x1C,0x4C,0x8B,0xC3,0x48,0x3B,0x77,0x08,0x73,0x0B,0x48,0x8D,0x14,0x31,
    0xE8,0x2B,0x29,0x25,0x01,0xEB,0x08,0x48,0x8B,0xD5,0xE8,0x21,0x29,0x25,0x01,0x48,
    0x8B,0x07,0x48,0x8B,0x6C,0x24,0x38,0x48,0x8B,0x74,0x24,0x40,0xC6,0x04,0x03,0x00,
    0x48,0x8B,0xC7,0x48,0x89,0x5F,0x08,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,
    0x5F,0xC3};
constexpr std::string_view ColorMarker = "\xEE\x81\xBE";
constexpr std::string_view SetGold = "\xEE\x81\xBE" "4";

std::string VisibleText(std::string_view line) {
    std::string plain;
    while (!line.empty()) {
        std::size_t markerSize{};
        if (line.starts_with(ColorMarker))
            markerSize = line.substr(ColorMarker.size()).starts_with("c") ? 4 : 3;
        else if (line.starts_with("\xC3\xBF" "c")) markerSize = 3;
        else if (line.starts_with("\xFF" "c")) markerSize = 2;
        if (markerSize && line.size() > markerSize) line.remove_prefix(markerSize + 1);
        else {
            if (line.front() != '\r') plain += line.front();
            line.remove_prefix(1);
        }
    }
    const auto first = plain.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return plain.substr(first, plain.find_last_not_of(" \t") - first + 1);
}

std::vector<std::string_view> TooltipLines(std::string_view text) {
    std::vector<std::string_view> lines;
    for (;;) {
        const auto end = text.find('\n');
        lines.push_back(text.substr(0, end));
        if (end == std::string_view::npos) return lines;
        text.remove_prefix(end + 1);
    }
}

std::string NativeSetText(std::string_view text, std::string_view setName, std::string_view contribution) {
    if (setName.empty()) return {};
    const auto lines = TooltipLines(text);
    std::vector<std::string> plain;
    plain.reserve(lines.size());
    for (const auto line : lines) plain.push_back(VisibleText(line));
    // The full localized name occurs as the set heading and inside its aura
    // line. Require both and leave ambiguous tooltips alone.
    const auto heading = std::find(plain.begin(), plain.end(), setName);
    if (heading == plain.end() || std::find(heading + 1, plain.end(), setName) != plain.end()) return {};
    const auto isAura = [setName](std::string_view line) {
        return line != setName && line.find(setName) != std::string_view::npos;
    };
    const auto marker = std::find_if(plain.begin(), plain.end(), isAura);
    if (marker == plain.end() || std::find_if(marker + 1, plain.end(), isAura) != plain.end()) return {};
    const auto markerIndex = static_cast<std::size_t>(marker - plain.begin());

    std::vector<std::string_view> stats;
    if (!contribution.empty()) {
        stats = TooltipLines(contribution);
        if (stats.size() < 2) return {};
        stats.erase(stats.begin()); // The pair title is for the SDK fallback.
        if (marker > heading) std::reverse(stats.begin(), stats.end());
    }

    std::string rewritten;
    rewritten.reserve(text.size());
    bool first = true;
    const auto append = [&](std::string_view line, bool gold) {
        if (!first) rewritten += '\n';
        first = false;
        if (gold) rewritten += SetGold;
        rewritten += line;
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i == markerIndex) {
            for (const auto stat : stats) append(VisibleText(stat), true);
            if (stats.empty() && i > 0 && i + 1 < lines.size()
                    && plain[i - 1].empty() && plain[i + 1].empty()) ++i;
        } else {
            append(lines[i], false);
        }
    }
    return rewritten;
}

bool WriteTooltipText(GameTooltipText* output, const GameTooltipText& snapshot,
        const char* text, std::size_t size) noexcept {
    __try {
        if (!AssignTooltipText || output->data != snapshot.data || output->size != snapshot.size
                || output->capacity != snapshot.capacity || size > 16'384) return false;
        return AssignTooltipText(output, text, size) == output && output->size == size;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template<std::size_t N>
std::string_view TooltipString(const char* key, std::array<char, N>& buffer) noexcept {
    std::uint32_t size{};
    if (Strings->getStringByKey(Context, key, buffer.data(), static_cast<std::uint32_t>(N), &size)
            != D2RL::Localization::Result::Success || size < 2 || size > N
            || buffer[size - 1] != '\0') return {};
    return {buffer.data(), size - 1};
}

void FormatOathTooltip(GameTooltipText* output, const void* item, int bodyLocation) noexcept {
    if (!Strings || !Strings->getStringByKey || !TableReady.load()) return;
    try {
        GameTooltipText snapshot{};
        if (!Read(output, snapshot) || !snapshot.data || !snapshot.size
                || snapshot.size > 16'384 || snapshot.capacity < snapshot.size
                || (snapshot.capacity >> 63)) return;
        std::string text(static_cast<std::size_t>(snapshot.size) + 1, '\0');
        SIZE_T read{};
        if (!ReadProcessMemory(GetCurrentProcess(), snapshot.data, text.data(), text.size(), &read)
                || read != text.size() || text.back() != '\0') return;
        text.pop_back();
        std::array<char, 256> nameBuffer{};
        const auto name = VisibleText(TooltipString("d2r:UnrunedOath", nameBuffer));
        if (name.empty() || text.find(name) == std::string::npos) return;

        // The game builds its set section before the shared tooltip event.
        // Read the confirmed client bonus and identify the actual equipped ring.
        const auto active = ActiveTooltipPair(item, bodyLocation);
        std::array<char, 1024> pairBuffer{};
        const auto contribution = active == NoPair ? std::string_view{}
            : TooltipString(Oath.pairs[active].tooltip, pairBuffer);
        const auto revised = NativeSetText(text, name, contribution);
        if (!revised.empty()) (void)WriteTooltipText(output, snapshot, revised.data(), revised.size());
    } catch (...) {
        // Leave the builder's text available if formatting fails.
    }
}

using BuildTooltip = GameTooltipText* (__fastcall*)(GameTooltipText*, void*, void*, void*,
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, void*);
using BuildComparison = GameTooltipText* (__fastcall*)(GameTooltipText*, int);
BuildTooltip PreviousTooltip{};
BuildComparison PreviousComparison{};
bool TooltipInstalled{};
bool ComparisonInstalled{};

GameTooltipText* __fastcall BuildOathTooltip(GameTooltipText* output, void* viewer, void* other,
        void* item, std::uint64_t mode, std::uint64_t showStats, std::uint64_t flags,
        std::uint64_t context, void* scratch) noexcept {
    auto* built = PreviousTooltip(output, viewer, other, item, mode, showStats, flags, context, scratch);
    FormatOathTooltip(built, item, 0);
    return built;
}

GameTooltipText* __fastcall BuildOathComparison(GameTooltipText* output, int bodyLocation) noexcept {
    auto* built = PreviousComparison(output, bodyLocation);
    FormatOathTooltip(built, nullptr, bodyLocation);
    return built;
}

constexpr std::uintptr_t TooltipSlotRva = 0x3E2AF58;
constexpr std::uintptr_t ComparisonSlotRva = 0x3E2AF60;
constexpr std::uintptr_t TooltipThunkRva = 0x3E2B826;
constexpr std::uintptr_t ComparisonThunkRva = 0x3E2B82C;
constexpr std::uint8_t TooltipThunkBytes[]{0xFF,0x25,0x2C,0xF7,0xFF,0xFF};
constexpr std::uint8_t ComparisonThunkBytes[]{0xFF,0x25,0x2E,0xF7,0xFF,0xFF};
// The call sites validate the argument layout and the returned string header.
constexpr std::uint8_t TooltipCallBytes[]{
    0xC6,0x44,0x24,0x28,0x01,0x4D,0x8B,0xC4,0x89,0x74,0x24,0x20,0x49,0x8B,0xD4,
    0xE8,0x38,0xE9,0xB6,0x03,0x4C,0x8B,0xF0,0x48,0x39,0x70,0x10};
constexpr std::uint8_t ComparisonCallBytes[]{
    0x0F,0xB6,0x54,0x24,0x21,0x48,0x8D,0x4C,0x24,0x78,0xE8,0x36,0x7A,0xB6,0x03};

bool ExecutablePointer(std::uintptr_t address) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof info)
        && info.State == MEM_COMMIT && !(info.Protect & PAGE_GUARD)
        && (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

bool ExchangeTooltipSlot(std::uintptr_t rva, std::uintptr_t expected, std::uintptr_t replacement) noexcept {
    auto* slot = reinterpret_cast<LONG64*>(Context->exeBase + rva);
    DWORD protection{};
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &protection)) return false;
    const auto before = InterlockedCompareExchange64(slot, static_cast<LONG64>(replacement),
        static_cast<LONG64>(expected));
    DWORD unused{};
    VirtualProtect(slot, sizeof *slot, protection, &unused);
    return static_cast<std::uintptr_t>(before) == expected;
}

template<typename Fn>
bool ChainTooltipSlot(std::uintptr_t rva, Fn hook, Fn& previous) noexcept {
    const auto slot = Context->exeBase + rva;
    std::uintptr_t target{};
    if (slot % alignof(std::uintptr_t) != 0 || !Read(reinterpret_cast<const void*>(slot), target)
            || target == reinterpret_cast<std::uintptr_t>(hook) || !ExecutablePointer(target)) return false;
    previous = reinterpret_cast<Fn>(target);
    return ExchangeTooltipSlot(rva, target, reinterpret_cast<std::uintptr_t>(hook));
}

void InstallNativeTooltip() noexcept {
    if (!Strings || !Strings->getStringByKey) return;
    const auto* base = reinterpret_cast<const std::uint8_t*>(Context->exeBase);
    if (!Context->CheckExpectedBytes(AssignTextRva, AssignTextBytes, sizeof AssignTextBytes)) {
        Context->LogWarn("Unruned Oath: native tooltip string layout unavailable.");
        return;
    }
    AssignTooltipText = reinterpret_cast<AssignGameText>(Context->exeBase + AssignTextRva);
    TooltipInstalled = Matches(base + TooltipThunkRva, TooltipThunkBytes)
        && Context->CheckExpectedBytes(0x2BCEDA, TooltipCallBytes, sizeof TooltipCallBytes)
        && ChainTooltipSlot(TooltipSlotRva, static_cast<BuildTooltip>(BuildOathTooltip), PreviousTooltip);
    ComparisonInstalled = TooltipInstalled && Matches(base + ComparisonThunkRva, ComparisonThunkBytes)
        && Context->CheckExpectedBytes(0x2C3DE7, ComparisonCallBytes, sizeof ComparisonCallBytes)
        && ChainTooltipSlot(ComparisonSlotRva, static_cast<BuildComparison>(BuildOathComparison), PreviousComparison);
    if (!TooltipInstalled) Context->LogWarn("Unruned Oath: final item tooltip formatter unavailable.");
    else if (!ComparisonInstalled) Context->LogWarn("Unruned Oath: comparison tooltip formatter unavailable.");
}

void RemoveNativeTooltip() noexcept {
    if (ComparisonInstalled)
        (void)ExchangeTooltipSlot(ComparisonSlotRva, reinterpret_cast<std::uintptr_t>(&BuildOathComparison),
            reinterpret_cast<std::uintptr_t>(PreviousComparison));
    if (TooltipInstalled)
        (void)ExchangeTooltipSlot(TooltipSlotRva, reinterpret_cast<std::uintptr_t>(&BuildOathTooltip),
            reinterpret_cast<std::uintptr_t>(PreviousTooltip));
}

void __cdecl AddTooltip(const D2RL::PluginContext* ctx, D2RL::SharedEvents::ItemTooltipEvent* e, void*) noexcept {
    if (!TableReady.load() || !e || e->structSize < sizeof *e || !e->text || !e->capacity) return;
    D2RL::Items::ItemInfo item{.structSize = D2RL::Items::ItemInfoSize};
    if (Items->getItemInfo(ctx, e->item, &item) != D2RL::Items::Result::Success) return;
    const auto ring = OathRingId(item);
    if (!ring) return;
    if (item.container != D2RL::Items::ItemContainer::Equipment) return;
    const auto active = TooltipPair.load();
    if (!ClientBonusMatches(active)) return;
    if (active == NoPair || active != TooltipPair.load()) return;
    const auto& pair = Oath.pairs[active];
    if (ring != pair.first && ring != pair.second) return;
    std::uint32_t size{};
    if (Strings->getStringByKey(ctx, pair.tooltip, e->text, e->capacity, &size)
            != D2RL::Localization::Result::Success || !size || size > e->capacity) {
        e->text[0] = '\0';
        e->length = 0;
        return;
    }
    e->length = size - 1;
}

template<typename T>
bool Service(const D2RL::PluginContext* ctx, const T*& value,
        std::size_t required = sizeof(T)) noexcept {
    return ctx->QueryService(&value) == D2RL::ServiceQueryResult::Success
        && value && value->serviceSize >= required && value->serviceVersion == T::AbiVersion;
}

template<typename T>
bool RequireService(const D2RL::PluginContext* ctx, const T*& value,
        const char* name, std::size_t required) noexcept {
    if (Service(ctx, value, required)) return true;
    char message[160]{};
    std::snprintf(message, sizeof message, "Unruned Oath: required %s service is unavailable.", name);
    ctx->LogError(message);
    return false;
}

void StopPolling() noexcept {
    if (Stop) SetEvent(Stop);
    if (Worker) { WaitForSingleObject(Worker, INFINITE); CloseHandle(Worker); Worker = nullptr; }
    if (Stop) { CloseHandle(Stop); Stop = nullptr; }
}
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Info; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* ctx) noexcept -> bool {
    if (!D2RL::HasContext(ctx) || ctx->loadScope != D2RL::LoadScope::Mod) return false;
    if (!ResolveNative(ctx)) {
        ctx->LogError("Unruned Oath: native helper signatures did not match; bonus disabled.");
        return false;
    }
    if (!RequireService(ctx, Inventory, "Inventory", offsetof(D2RL::InventoryService, forEachInventoryItem))
            || !RequireService(ctx, Items, "Item", offsetof(D2RL::ItemService, getItemInfo)
                + sizeof(D2RL::Items::GetItemInfoFn))
            || !RequireService(ctx, Threads, "Thread", D2RL::ThreadServiceRequiredSize)
            || !RequireService(ctx, Tables, "DataTable", offsetof(D2RL::DataTableService, getRow))
            || !RequireService(ctx, Strings, "Localization", D2RL::LocalizationServiceRequiredSize)
            || !RequireService(ctx, Lifecycle, "Lifecycle", offsetof(D2RL::LifecycleService, unregisterGameplayEventListener)))
        return false;
    if (!Inventory->getLocalPlayer || !Inventory->getEquippedItem
            || !Items->getItemInfo || !Threads->runOnGameThread
            || !Tables->getTable || !Strings->getStringByKey || !Lifecycle->registerGameplayEventListener
            || !Lifecycle->registerDataTablesLoadedListener) {
        ctx->LogError("Unruned Oath: a required service callback is missing.");
        return false;
    }
    HasItemDetails = D2RL::HasItemServiceField(Items, D2RL::ItemServiceDetailsFieldEnd)
        && Items->getItemDetails;
    if (!HasItemDetails && !HasNativeRunewordLayout) {
        ctx->LogError("Unruned Oath: no verified way to read the completed-runeword flag.");
        return false;
    }
    const auto install = RunFirstRunInstall(*ctx);
    if (install == InstallResult::Installed) return true;
    if (install == InstallResult::Failed) return false;
    Context = ctx;
    D2RL::Lifecycle::ListenerHandle token{};
    const D2RL::Lifecycle::DataTablesLoadedListener tables{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .callback = OnTables};
    if (Lifecycle->registerDataTablesLoadedListener(ctx, &tables, &token) != D2RL::Lifecycle::Result::Success) return false;
    for (const auto kind : {D2RL::Lifecycle::GameplayEventKind::GameJoined,
            D2RL::Lifecycle::GameplayEventKind::GameLeft, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady}) {
        const D2RL::Lifecycle::GameplayEventListener event{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = kind, .callback = OnGameplay};
        if (Lifecycle->registerGameplayEventListener(ctx, &event, &token) != D2RL::Lifecycle::Result::Success) return false;
    }
    Stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (Stop) Worker = CreateThread(nullptr, 0, Poll, nullptr, 0, nullptr);
    if (!Worker) { StopPolling(); return false; }
    InstallNativeTooltip();
    if (!TooltipInstalled && Service(ctx, Events) && Events->registerItemTooltipListener) {
        const D2RL::SharedEvents::ItemTooltipListener tooltip{
            .structSize = D2RL::SharedEvents::ItemTooltipListenerSize,
            .region = D2RL::SharedEvents::ItemTooltipRegion::Attributes,
            .position = D2RL::SharedEvents::ItemTooltipPosition::Bottom,
            .callback = AddTooltip};
        D2RL::SharedEvents::ListenerHandle ignored{};
        (void)Events->registerItemTooltipListener(ctx, &tooltip, &ignored);
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    RemoveNativeTooltip();
    StopPolling();
}
