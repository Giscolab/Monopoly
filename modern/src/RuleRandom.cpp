#include "RuleRandom.hpp"

#include <chrono>

namespace monopoly::rules::random
{
    namespace
    {
        std::mt19937 globalGenerator;
    }

    void initialize()
    {
        const auto seed =
            static_cast<std::mt19937::result_type>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            );

        globalGenerator.seed(seed);
    }

    void seed(std::uint32_t value)
    {
        globalGenerator.seed(value);
    }


    std::mt19937& generator()
    {
        return globalGenerator;
    }
}

