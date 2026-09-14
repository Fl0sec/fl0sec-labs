#pragma once

#include <array>
#include <cstdint>

namespace game_offsets {

namespace launch {
inline constexpr wchar_t default_game_name[] = L"TBM.exe";
inline constexpr uintptr_t detected = 0x707B8;
inline constexpr uintptr_t storage_initialized = 0x70B08;
inline constexpr uintptr_t gameplay_ready = 0x70B0F;
}

namespace protection {
inline constexpr uintptr_t text_section_size = 0x70D78;
}

namespace ammo {
inline constexpr uintptr_t first_key_pointer = 0x70888;
inline constexpr uintptr_t second_key_pointer = 0x707C0;
inline constexpr uintptr_t record = 0x70910;
inline constexpr uintptr_t bound_branch = 0x47FB6;
inline constexpr uint32_t boosted_value = 200;
inline constexpr std::array<uint8_t, 6> bound_original = {
    0x0F, 0x8E, 0x81, 0x04, 0x00, 0x00};
inline constexpr std::array<uint8_t, 6> bound_bypass = {
    0xE9, 0x82, 0x04, 0x00, 0x00, 0x90};
}

namespace player {
inline constexpr uintptr_t apply_damage = 0x51F70;
inline constexpr uintptr_t movement_speed = 0x6AD54;
inline constexpr std::array<uint8_t, 1> damage_original = {0x48};
inline constexpr std::array<uint8_t, 1> damage_bypass = {0xC3};
inline constexpr std::array<uint8_t, 4> speed_original = {0x00, 0x00, 0x5C, 0x43};
inline constexpr std::array<uint8_t, 4> speed_boosted = {0x00, 0x00, 0xDC, 0x43};
}

namespace weapon {
inline constexpr uintptr_t sentinel_first = 0x700A8;
inline constexpr uintptr_t active = 0x700AC;
inline constexpr uintptr_t sentinel_second = 0x700B0;
inline constexpr uintptr_t active_mirror = 0x707BA;
inline constexpr uintptr_t transition_counter = 0x707EC;
inline constexpr uintptr_t transition_timestamp = 0x70AE0;
inline constexpr uintptr_t id = 0x70DBC;
inline constexpr uintptr_t timer = 0x70F18;
inline constexpr uintptr_t fire_cooldown_timer = 0x70FA0;
}

namespace enemies {
inline constexpr uintptr_t begin = 0x70F88;
inline constexpr uintptr_t end = 0x70F90;
inline constexpr uintptr_t health_offset = 0x0C;
inline constexpr uintptr_t active_offset = 0x1C;
inline constexpr uintptr_t stride = 0x24;
}

}
