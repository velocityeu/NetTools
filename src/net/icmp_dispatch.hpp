#pragma once
#include <semaphore>

namespace veu::net::detail
{
inline std::counting_semaphore<4> &icmp_slots()
{
    static std::counting_semaphore<4> slots(4);
    return slots;
}
} // namespace veu::net::detail
