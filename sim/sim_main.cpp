
#include "sim_core.h"
#include "sim_ui.h"
#include "sim_video.h"
#include "sim_state.h"
#include "sim_command.h"
#include "sim_hierarchy.h"
#include "games.h"
#include "PGM.h"
#include "PGM___024root.h"
#include "imgui_wrap.h"
#include "file_search.h"
#include "sim_sdram.h"
#include "sim_ddr.h"
#include "verilated_fst_c.h"

#include "gfx_cache.h"

#include <stdio.h>
#include <memory>
#include <SDL.h>

// Access via global SimCore instance

SimState *gStateManager = nullptr;

uint8_t gDipswitchA = 1;
uint8_t gDipswitchB = 0;

// Access simulation state via global SimCore instance

struct SimDebug
{
    uint32_t mModified;
    uint32_t mZoom;
    uint32_t mDy;
    uint32_t mY;
};

SimDebug *gSimDebugData = nullptr;

int main(int argc, char **argv)
{
    CommandQueue commandQueue;
    std::string gameNameStr;

    // Parse command line arguments
    if (!commandQueue.ParseArguments(argc, argv, gameNameStr))
    {
        return -1;
    }

    // Convert positional game argument to command if provided
    if (!gameNameStr.empty())
    {
        // Check if it's an MRA file
        if (gameNameStr.length() > 4 && gameNameStr.substr(gameNameStr.length() - 4) == ".mra")
        {
            commandQueue.Add(Command(CommandType::LOAD_MRA, gameNameStr));
        }
        else
        {
            commandQueue.Add(Command(CommandType::LOAD_GAME, gameNameStr));
            commandQueue.Add(Command(CommandType::RESET, 100));
        }
    }

    gSimCore.Init();

    // Only initialize UI if not in headless mode
    if (!commandQueue.IsHeadless())
    {
        UiInit("PGM Simulator");
        UiSetCommandQueue(&commandQueue);
    }

    gFileSearch.AddSearchPath(".");

    gSimDebugData = (SimDebug *)(gSimCore.mSDRAM->mData + 0x80000);

    gSimCore.mTop->ss_do_save = 0;
    gSimCore.mTop->ss_do_restore = 0;
    gSimCore.mTop->obj_debug_idx = -1;

    gSimCore.mTop->joystick_p1 = 0;
    gSimCore.mTop->joystick_p2 = 0;

    gStateManager = new SimState(gSimCore.mTop, gSimCore.mDDRMemory.get(), 0x3E000000, 512 * 1024);

    if (!commandQueue.IsHeadless())
    {
        gSimCore.mVideo->Init(448, 224, ImguiGetRenderer());

        gSimCore.mGfxCache->Init(ImguiGetRenderer(), gSimCore.Memory(MemoryRegion::PALETTE_RAM));
    }
    else
    {
        // Minimal init for headless mode
        gSimCore.mVideo->Init(448, 224, nullptr);
    }

    Verilated::traceEverOn(true);

    const Uint8 *keyboardState = commandQueue.IsHeadless() ? nullptr : SDL_GetKeyboardState(NULL);
    bool screenshotKeyPressed = false;

    // Track frame counting for interactive mode
    bool prevVblank = false;

    // Main loop
    bool running = true;
    while (running)
    {
        // Check if we have commands to execute
        if (!commandQueue.Empty())
        {
            Command &cmd = commandQueue.Front();

            switch (cmd.mType)
            {
            case CommandType::RUN_CYCLES:
                if (commandQueue.IsVerbose())
                    printf("Running %llu cycles...\n", cmd.mCount);
                gSimCore.Tick(cmd.mCount);
                if (commandQueue.IsVerbose())
                    printf("Completed running %llu cycles\n", cmd.mCount);
                commandQueue.Pop();
                break;

            case CommandType::RUN_FRAMES:
                if (commandQueue.IsVerbose())
                    printf("Running %llu frames...\n", cmd.mCount);
                for (uint64_t i = 0; i < cmd.mCount; i++)
                {
                    if (!gSimCore.TickUntil([&] { return gSimCore.mTop->vblank == 0; }, 10000000).Succeeded())
                        break;
                    if (!gSimCore.TickUntil([&] { return gSimCore.mTop->vblank != 0; }, 10000000).Succeeded())
                        break;
                }
                if (commandQueue.IsVerbose())
                    printf("Completed running %llu frames\n", cmd.mCount);
                commandQueue.Pop();
                break;

            case CommandType::SCREENSHOT:
                gSimCore.mVideo->UpdateTexture();
                if (commandQueue.IsVerbose())
                    printf("Saving screenshot to: %s\n", cmd.mFilename.c_str());
                if (gSimCore.mVideo->SaveScreenshot(cmd.mFilename.c_str()))
                {
                    if (commandQueue.IsVerbose())
                        printf("Screenshot saved successfully\n");
                }
                else
                {
                    printf("Failed to save screenshot\n");
                }
                commandQueue.Pop();
                break;

            case CommandType::SAVE_STATE:
                if (commandQueue.IsVerbose())
                    printf("Saving state to: %s\n", cmd.mFilename.c_str());
                gStateManager->SaveState(cmd.mFilename.c_str());
                commandQueue.Pop();
                break;

            case CommandType::LOAD_STATE:
                if (commandQueue.IsVerbose())
                    printf("Loading state from: %s\n", cmd.mFilename.c_str());
                gStateManager->RestoreState(cmd.mFilename.c_str());
                commandQueue.Pop();
                break;

            case CommandType::TRACE_START:
                if (commandQueue.IsVerbose())
                    printf("Starting trace to: %s\n", cmd.mFilename.c_str());
                gSimCore.StartTrace(cmd.mFilename.c_str(), 99);
                if (commandQueue.IsVerbose())
                    printf("Trace started successfully\n");
                commandQueue.Pop();
                break;

            case CommandType::TRACE_STOP:
                if (commandQueue.IsVerbose())
                    printf("Stopping trace\n");
                if (gSimCore.IsTraceActive())
                {
                    gSimCore.StopTrace();
                    if (commandQueue.IsVerbose())
                        printf("Trace stopped successfully\n");
                }
                else
                {
                    if (commandQueue.IsVerbose())
                        printf("No trace was active\n");
                }
                commandQueue.Pop();
                break;

            case CommandType::LOAD_GAME:
                if (commandQueue.IsVerbose())
                    printf("Loading game: %s\n", cmd.mFilename.c_str());
                {
                    Game game = GameFind(cmd.mFilename.c_str());
                    if (game != GAME_INVALID)
                    {
                        if (GameInit(game))
                        {
                            gStateManager->SetGameName(cmd.mFilename.c_str());
                            if (!commandQueue.IsHeadless())
                                UiGameChanged();
                            if (commandQueue.IsVerbose())
                                printf("Successfully loaded game: %s\n", cmd.mFilename.c_str());
                        }
                        else
                        {
                            printf("Failed to load game: %s\n", cmd.mFilename.c_str());
                        }
                    }
                    else
                    {
                        printf("Unknown game: %s\n", cmd.mFilename.c_str());
                    }
                }
                commandQueue.Pop();
                break;

            case CommandType::LOAD_MRA:
                if (commandQueue.IsVerbose())
                    printf("Loading MRA: %s\n", cmd.mFilename.c_str());
                if (GameInitMra(cmd.mFilename.c_str()))
                {
                    gStateManager->SetGameName(gSimCore.GetGameName());
                    if (!commandQueue.IsHeadless())
                        UiGameChanged();
                    if (commandQueue.IsVerbose())
                        printf("Successfully loaded MRA: %s (%s)\n", cmd.mFilename.c_str(), gSimCore.GetGameName());
                }
                else
                {
                    printf("Failed to load MRA: %s\n", cmd.mFilename.c_str());
                }
                commandQueue.Pop();
                break;

            case CommandType::RESET:
                if (commandQueue.IsVerbose())
                    printf("Reset for %llu cycles\n", cmd.mCount);
                // Set reset signal
                gSimCore.mTop->reset = 1;
                // Run for specified cycles
                gSimCore.Tick(cmd.mCount);
                // Clear reset signal
                gSimCore.mTop->reset = 0;
                commandQueue.Pop();
                break;

            case CommandType::SET_DIPSWITCH_A:
                if (commandQueue.IsVerbose())
                    printf("Set dipswitch A to 0x%llx\n", cmd.mCount);
                gDipswitchA = cmd.mCount & 0xff;
                gSimCore.mTop->dswa = gDipswitchA;
                commandQueue.Pop();
                break;

            case CommandType::SET_DIPSWITCH_B:
                if (commandQueue.IsVerbose())
                    printf("Set dipswitch B to 0x%llx\n", cmd.mCount);
                gDipswitchB = cmd.mCount & 0xff;
                gSimCore.mTop->dswb = gDipswitchB;
                commandQueue.Pop();
                break;

            case CommandType::EXIT:
                if (commandQueue.IsVerbose())
                    printf("Exiting...\n");
                running = false;
                commandQueue.Pop();
                break;

            default:
                printf("Unhandled command type\n");
                commandQueue.Pop();
                break;
            }
        }
        else if (!commandQueue.IsHeadless())
        {
            gSimCore.mGfxCache->PruneCache();

            // Interactive mode
            if (!UiBeginFrame())
            {
                running = false;
                break;
            }

            // Handle F12 screenshot key
            if (keyboardState && keyboardState[SDL_SCANCODE_F12] && !screenshotKeyPressed)
            {
                std::string filename = gSimCore.mVideo->GenerateScreenshotFilename("sim");
                gSimCore.mVideo->SaveScreenshot(filename.c_str());
                screenshotKeyPressed = true;
            }
            else if (keyboardState && !keyboardState[SDL_SCANCODE_F12])
            {
                screenshotKeyPressed = false;
            }

            gSimCore.mTop->dswa = gDipswitchA & 0xff;
            gSimCore.mTop->dswb = gDipswitchB & 0xff;
            gSimCore.mTop->pause = gSimCore.mSystemPause;

            gSimCore.mTop->joystick_p1 = ImguiGetButtons() & 0xffff;
            gSimCore.mTop->start = (ImguiGetButtons() >> 16) & 0xffff;

            if (gSimCore.mSimulationRun || gSimCore.mSimulationStep)
            {
                if (gSimCore.mSimulationStepVblank)
                {
                    gSimCore.TickUntil([&] { return gSimCore.mTop->vblank == 0; }, 0);
                    gSimCore.TickUntil([&] { return gSimCore.mTop->vblank != 0; }, 0);
                }
                else
                {
                    gSimCore.Tick(gSimCore.mSimulationStepSize);
                }
                gSimCore.mVideo->UpdateTexture();
            }
            gSimCore.mSimulationStep = false;

            UiDraw();

            UiEndFrame();
        }
        else
        {
            // Headless mode with no commands left
            running = false;
        }
    }

    gSimCore.Shutdown();

    delete gStateManager;
    return 0;
}
