#include "DebugDialogs.hpp"

#include <iostream>
#include <stdexcept>

using namespace monopoly;

namespace
{
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
        std::cout << "[PASS] " << message << '\n';
    }
}

int main()
{
    try
    {
        const auto retry =
            debugui::questionPlan(debugui::Question::RetryCancel, true);
        require(retry.positiveText == "Retry" &&
                retry.negativeText == "Cancel" &&
                retry.positiveDefault && !retry.negativeDefault,
            "Retry/Cancel preserves the Win32 default button");

        const auto ok =
            debugui::questionPlan(debugui::Question::OkCancel, true);
        require(ok.positiveText == "OK" &&
                ok.negativeText == "Cancel",
            "OK/Cancel exposes the retail labels");

        require(debugui::ErrorExitStatus == 20,
            "ErrorExit preserves the retail process exit status 20");

        const auto noDefault =
            debugui::questionPlan(debugui::Question::YesNo, false);
        require(noDefault.positiveText == "Yes" &&
                noDefault.negativeText == "No" &&
                !noDefault.positiveDefault && noDefault.negativeDefault,
            "Yes/No preserves DefaultYes false semantics");

        std::cout << "Debug dialog contract tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
