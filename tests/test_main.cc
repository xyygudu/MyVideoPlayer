#include <SDL3/SDL_hints.h>
#include <gtest/gtest.h>

#include "mvp/logging.h"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // Silent, and independent of the machine's audio hardware; the
    // SDL_AUDIO_DRIVER environment variable still overrides this default.
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_DEFAULT);
    mvp::logging::Init();
    mvp::logging::SetConsoleLoggingEnabled(false);
    mvp::logging::EnableFileLogging(MVP_TEST_LOG_FILE);
    mvp::logging::SetLevel(mvp::logging::Level::kDebug);
    return RUN_ALL_TESTS();
}
