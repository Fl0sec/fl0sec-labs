#pragma once

#include <array>
#include <cstdint>

using GuardedRecord = std::array<uint32_t, 30>;

inline GuardedRecord make_guarded_record(uint32_t first_key, uint32_t second_key,
                                         uint32_t value) {
    GuardedRecord record{};
    record[0] = first_key ^ value;
    record[8] = 0xDEADC0DE;
    record[14] = 0xB16B00B5;
    record[26] = second_key ^ value;

    uint32_t state = first_key ^ second_key ^ value;
    for (size_t index = 1; index < record.size(); ++index) {
        if (index == 8 || index == 14 || index == 26) {
            continue;
        }
        state = 0x01000193u * state ^ 0x811C9DC5u;
        record[index] = state;
    }
    return record;
}
