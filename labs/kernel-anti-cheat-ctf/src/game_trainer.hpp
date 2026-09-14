#pragma once

#include "game_offsets.hpp"
#include "remote_process.hpp"

#include <cstdint>

class GameTrainer {
public:
    GameTrainer(HANDLE process, uintptr_t image_base);

    void toggle_no_damage();
    void set_ammo();
    void toggle_movement_speed();
    void activate_weapon(uint32_t weapon_id, const wchar_t* weapon_name);
    void update_weapon();
    void toggle_one_hit_enemies();
    void update_one_hit_enemies();
    void restore_all();

private:
    void increment_weapon_transition_counter();
    void deactivate_weapon();
    void write_coherent_ammo(uint32_t value);
    void disable_text_integrity();
    void restore_text_integrity_if_unused();

    HANDLE process_;
    uintptr_t image_base_;
    NtSuspendProcessFn suspend_;
    NtResumeProcessFn resume_;
    RemotePatch<6> ammo_bound_;
    RemotePatch<1> no_damage_;
    RemotePatch<4> movement_speed_;
    uint64_t original_text_size_;
    bool text_integrity_disabled_ = false;
    bool weapon_active_ = false;
    bool one_hit_enemies_enabled_ = false;
    uint64_t next_weapon_refresh_ = 0;
    uint64_t next_one_hit_update_ = 0;
};
