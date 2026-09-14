#include "game_trainer.hpp"

#include "guarded_state.hpp"
#include "win32.hpp"

#include <stdexcept>
#include <string>

namespace offsets = game_offsets;

GameTrainer::GameTrainer(HANDLE process, uintptr_t image_base)
    : process_(process), image_base_(image_base),
      suspend_(ntdll_function<NtSuspendProcessFn>("NtSuspendProcess")),
      resume_(ntdll_function<NtResumeProcessFn>("NtResumeProcess")),
      ammo_bound_(process, image_base + offsets::ammo::bound_branch,
                  offsets::ammo::bound_original, offsets::ammo::bound_bypass),
      no_damage_(process, image_base + offsets::player::apply_damage,
                 offsets::player::damage_original, offsets::player::damage_bypass),
      movement_speed_(process, image_base + offsets::player::movement_speed,
                      offsets::player::speed_original, offsets::player::speed_boosted),
      original_text_size_(read_remote<uint64_t>(
          process, image_base + offsets::protection::text_section_size)) {}

void GameTrainer::toggle_no_damage() {
    ScopedProcessSuspend suspension(process_, suspend_, resume_);
    if (no_damage_.enabled()) {
        no_damage_.disable();
        restore_text_integrity_if_unused();
        log_stage(L"No-damage disabled.");
    } else {
        disable_text_integrity();
        no_damage_.enable();
        log_stage(L"No-damage enabled.");
    }
    suspension.resume();
}

void GameTrainer::set_ammo() {
    ScopedProcessSuspend suspension(process_, suspend_, resume_);
    disable_text_integrity();
    ammo_bound_.enable();
    write_coherent_ammo(offsets::ammo::boosted_value);
    suspension.resume();
    log_stage(L"Ammo set coherently to 200; bound bypass enabled.");
}

void GameTrainer::toggle_movement_speed() {
    ScopedProcessSuspend suspension(process_, suspend_, resume_);
    if (movement_speed_.enabled()) {
        movement_speed_.disable();
        log_stage(L"Movement speed restored to 220.");
    } else {
        movement_speed_.enable();
        log_stage(L"Movement speed increased to 440.");
    }
    suspension.resume();
}

void GameTrainer::activate_weapon(uint32_t weapon_id, const wchar_t* weapon_name) {
    if (weapon_id > 2) {
        throw std::runtime_error("Invalid weapon id");
    }
    ScopedProcessSuspend suspension(process_, suspend_, resume_);
    write_remote(process_, image_base_ + offsets::weapon::sentinel_first,
                 uint32_t{0xDEADC0DE});
    write_remote(process_, image_base_ + offsets::weapon::active, uint8_t{1});
    write_remote(process_, image_base_ + offsets::weapon::sentinel_second,
                 uint32_t{0xB16B00B5});
    write_remote(process_, image_base_ + offsets::weapon::active_mirror, uint8_t{1});
    increment_weapon_transition_counter();
    write_remote(process_, image_base_ + offsets::weapon::transition_timestamp,
                 GetTickCount64());
    write_remote(process_, image_base_ + offsets::weapon::id, weapon_id);
    write_remote(process_, image_base_ + offsets::weapon::timer, 10.0f);
    write_remote(process_, image_base_ + offsets::weapon::fire_cooldown_timer, 0.0f);
    weapon_active_ = true;
    next_weapon_refresh_ = GetTickCount64() + 1000;
    suspension.resume();
    log_stage(std::wstring(L"Activated persistent ") + weapon_name + L" weapon.");
}

void GameTrainer::update_weapon() {
    const uint64_t now = GetTickCount64();
    if (!weapon_active_ || now < next_weapon_refresh_) {
        return;
    }
    next_weapon_refresh_ = now + 1000;
    ScopedProcessSuspend suspension(process_, suspend_, resume_);
    write_remote(process_, image_base_ + offsets::weapon::timer, 10.0f);
    write_remote(process_, image_base_ + offsets::weapon::transition_timestamp, now);
    suspension.resume();
}

void GameTrainer::toggle_one_hit_enemies() {
    one_hit_enemies_enabled_ = !one_hit_enemies_enabled_;
    next_one_hit_update_ = 0;
    log_stage(one_hit_enemies_enabled_ ? L"One-hit enemies enabled."
                                       : L"One-hit enemies disabled.");
}

void GameTrainer::update_one_hit_enemies() {
    const uint64_t now = GetTickCount64();
    if (!one_hit_enemies_enabled_ || now < next_one_hit_update_) {
        return;
    }
    next_one_hit_update_ = now + 200;

    ScopedProcessSuspend suspension(process_, suspend_, resume_);
    const uintptr_t begin = read_remote<uintptr_t>(
        process_, image_base_ + offsets::enemies::begin);
    const uintptr_t end = read_remote<uintptr_t>(
        process_, image_base_ + offsets::enemies::end);
    if (end < begin || (end - begin) % offsets::enemies::stride != 0 ||
        (end - begin) / offsets::enemies::stride > 10000) {
        throw std::runtime_error("Invalid enemy vector bounds");
    }
    for (uintptr_t enemy = begin; enemy < end; enemy += offsets::enemies::stride) {
        if (read_remote<uint8_t>(process_, enemy + offsets::enemies::active_offset) != 0) {
            write_remote(process_, enemy + offsets::enemies::health_offset, int32_t{1});
        }
    }
    suspension.resume();
}

void GameTrainer::restore_all() {
    ScopedProcessSuspend suspension(process_, suspend_, resume_);
    one_hit_enemies_enabled_ = false;
    if (ammo_bound_.enabled()) {
        write_coherent_ammo(30);
    }
    deactivate_weapon();
    no_damage_.disable();
    ammo_bound_.disable();
    movement_speed_.disable();
    restore_text_integrity_if_unused();
    suspension.resume();
    log_stage(L"All code patches restored.");
}

void GameTrainer::increment_weapon_transition_counter() {
    const uintptr_t address = image_base_ + offsets::weapon::transition_counter;
    write_remote(process_, address, read_remote<uint32_t>(process_, address) + 1);
}

void GameTrainer::deactivate_weapon() {
    const uint8_t active = read_remote<uint8_t>(
        process_, image_base_ + offsets::weapon::active);
    if (!weapon_active_ && active == 0) {
        return;
    }
    write_remote(process_, image_base_ + offsets::weapon::sentinel_first,
                 uint32_t{0xDEADC0DE});
    write_remote(process_, image_base_ + offsets::weapon::active, uint8_t{0});
    write_remote(process_, image_base_ + offsets::weapon::sentinel_second,
                 uint32_t{0xB16B00B5});
    write_remote(process_, image_base_ + offsets::weapon::active_mirror, uint8_t{0});
    increment_weapon_transition_counter();
    write_remote(process_, image_base_ + offsets::weapon::transition_timestamp,
                 uint64_t{0});
    write_remote(process_, image_base_ + offsets::weapon::timer, 0.0f);
    write_remote(process_, image_base_ + offsets::weapon::fire_cooldown_timer, 0.0f);
    weapon_active_ = false;
    next_weapon_refresh_ = 0;
}

void GameTrainer::write_coherent_ammo(uint32_t value) {
    const uintptr_t first_key_pointer = read_remote<uintptr_t>(
        process_, image_base_ + offsets::ammo::first_key_pointer);
    const uintptr_t second_key_pointer = read_remote<uintptr_t>(
        process_, image_base_ + offsets::ammo::second_key_pointer);
    const uint32_t first_key = read_remote<uint32_t>(process_, first_key_pointer);
    const uint32_t second_key = read_remote<uint32_t>(process_, second_key_pointer);
    const auto current_record = read_remote<GuardedRecord>(
        process_, image_base_ + offsets::ammo::record);
    const uint32_t current_value = current_record[0] ^ first_key;
    if (current_record != make_guarded_record(first_key, second_key, current_value)) {
        throw std::runtime_error("Current ammo record is not coherent");
    }
    write_remote(process_, image_base_ + offsets::ammo::record,
                 make_guarded_record(first_key, second_key, value));
}

void GameTrainer::disable_text_integrity() {
    if (!text_integrity_disabled_) {
        write_remote(process_, image_base_ + offsets::protection::text_section_size,
                     uint64_t{0});
        text_integrity_disabled_ = true;
    }
}

void GameTrainer::restore_text_integrity_if_unused() {
    if (text_integrity_disabled_ && !ammo_bound_.enabled() &&
        !no_damage_.enabled()) {
        write_remote(process_, image_base_ + offsets::protection::text_section_size,
                     original_text_size_);
        text_integrity_disabled_ = false;
    }
}
