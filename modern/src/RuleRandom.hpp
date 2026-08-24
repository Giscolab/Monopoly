#pragma once

#include <random>
#include <cstdint>

namespace monopoly::rules::random
{
    void initialize();

    void seed(std::uint32_t value);

    std::mt19937& generator();
}

