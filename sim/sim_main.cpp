
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

SimState *state_manager = nullptr;

uint8_t dipswitch_a = 1;
uint8_t dipswitch_b = 0;

// Access simulation state via global SimCore instance

struct SimDebug
{
    uint32_t modified;
    uint32_t zoom;
    uint32_t dy;
    uint32_t y;
};

SimDebug *sim_debug_data = nullptr;

int main(int argc, char **argv)
{
    CommandQueue command_queue;
    std::string game_name_str;

    // Parse command line arguments
    if (!command_queue.parse_arguments(argc, argv, game_name_str))
    {
        return -1;
    }

    // Convert positional game argument to command if provided
    if (!game_name_str.empty())
    {
        // Check if it's an MRA file
        if (game_name_str.length() > 4 && game_name_str.substr(game_name_str.length() - 4) == ".mra")
        {
            command_queue.add(Command(CommandType::LOAD_MRA, game_name_str));
        }
        else
        {
            command_queue.add(Command(CommandType::LOAD_GAME, game_name_str));
            command_queue.add(Command(CommandType::RESET, 100));
        }
    }

    gSimCore.Init();

    // Only initialize UI if not in headless mode
    if (!command_queue.is_headless())
    {
        ui_init("PGM Simulator");
        ui_set_command_queue(&command_queue);
    }

    g_fs.addSearchPath(".");

    sim_debug_data = (SimDebug *)(gSimCore.mSDRAM->mData + 0x80000);

    gSimCore.mTop->ss_do_save = 0;
    gSimCore.mTop->ss_do_restore = 0;
    gSimCore.mTop->obj_debug_idx = -1;

    gSimCore.mTop->joystick_p1 = 0;
    gSimCore.mTop->joystick_p2 = 0;

    state_manager = new SimState(gSimCore.mTop, gSimCore.mDDRMemory.get(), 0x3E000000, 512 * 1024);

    if (!command_queue.is_headless())
    {
        gSimCore.mVideo->init(320, 224, imgui_get_renderer());

        gSimCore.mGfxCache->Init(imgui_get_renderer(), gSimCore.Memory(MemoryRegion::PALETTE_RAM));
    }
    else
    {
        // Minimal init for headless mode
        gSimCore.mVideo->init(320, 224, nullptr);
    }

    Verilated::traceEverOn(true);

    const Uint8 *keyboard_state = command_queue.is_headless() ? nullptr : SDL_GetKeyboardState(NULL);
    bool screenshot_key_pressed = false;

    // Track frame counting for interactive mode
    bool prev_vblank = false;

    // Main loop
    bool running = true;
    while (running)
    {
        // Check if we have commands to execute
        if (!command_queue.empty())
        {
            Command &cmd = command_queue.front();

            switch (cmd.type)
            {
            case CommandType::RUN_CYCLES:
                if (command_queue.is_verbose())
                    printf("Running %llu cycles...\n", cmd.count);
                gSimCore.Tick(cmd.count);
                if (command_queue.is_verbose())
                    printf("Completed running %llu cycles\n", cmd.count);
                command_queue.pop();
                break;

            case CommandType::RUN_FRAMES:
                if (command_queue.is_verbose())
                    printf("Running %llu frames...\n", cmd.count);
                for (uint64_t i = 0; i < cmd.count; i++)
                {
                    gSimCore.TickUntil([&] { return gSimCore.mTop->vblank == 0; });
                    gSimCore.TickUntil([&] { return gSimCore.mTop->vblank != 0; });
                }
                if (command_queue.is_verbose())
                    printf("Completed running %llu frames\n", cmd.count);
                command_queue.pop();
                break;

            case CommandType::SCREENSHOT:
                gSimCore.mVideo->update_texture();
                if (command_queue.is_verbose())
                    printf("Saving screenshot to: %s\n", cmd.filename.c_str());
                if (gSimCore.mVideo->save_screenshot(cmd.filename.c_str()))
                {
                    if (command_queue.is_verbose())
                        printf("Screenshot saved successfully\n");
                }
                else
                {
                    printf("Failed to save screenshot\n");
                }
                command_queue.pop();
                break;

            case CommandType::SAVE_STATE:
                if (command_queue.is_verbose())
                    printf("Saving state to: %s\n", cmd.filename.c_str());
                state_manager->save_state(cmd.filename.c_str());
                command_queue.pop();
                break;

            case CommandType::LOAD_STATE:
                if (command_queue.is_verbose())
                    printf("Loading state from: %s\n", cmd.filename.c_str());
                state_manager->restore_state(cmd.filename.c_str());
                command_queue.pop();
                break;

            case CommandType::TRACE_START:
                if (command_queue.is_verbose())
                    printf("Starting trace to: %s\n", cmd.filename.c_str());
                gSimCore.StartTrace(cmd.filename.c_str(), 99);
                if (command_queue.is_verbose())
                    printf("Trace started successfully\n");
                command_queue.pop();
                break;

            case CommandType::TRACE_STOP:
                if (command_queue.is_verbose())
                    printf("Stopping trace\n");
                if (gSimCore.IsTraceActive())
                {
                    gSimCore.StopTrace();
                    if (command_queue.is_verbose())
                        printf("Trace stopped successfully\n");
                }
                else
                {
                    if (command_queue.is_verbose())
                        printf("No trace was active\n");
                }
                command_queue.pop();
                break;

            case CommandType::LOAD_GAME:
                if (command_queue.is_verbose())
                    printf("Loading game: %s\n", cmd.filename.c_str());
                {
                    game_t game = game_find(cmd.filename.c_str());
                    if (game != GAME_INVALID)
                    {
                        if (game_init(game))
                        {
                            state_manager->set_game_name(cmd.filename.c_str());
                            if (!command_queue.is_headless())
                                ui_game_changed();
                            if (command_queue.is_verbose())
                                printf("Successfully loaded game: %s\n", cmd.filename.c_str());
                        }
                        else
                        {
                            printf("Failed to load game: %s\n", cmd.filename.c_str());
                        }
                    }
                    else
                    {
                        printf("Unknown game: %s\n", cmd.filename.c_str());
                    }
                }
                command_queue.pop();
                break;

            case CommandType::LOAD_MRA:
                if (command_queue.is_verbose())
                    printf("Loading MRA: %s\n", cmd.filename.c_str());
                if (game_init_mra(cmd.filename.c_str()))
                {
                    state_manager->set_game_name(gSimCore.GetGameName());
                    if (!command_queue.is_headless())
                        ui_game_changed();
                    if (command_queue.is_verbose())
                        printf("Successfully loaded MRA: %s (%s)\n", cmd.filename.c_str(), gSimCore.GetGameName());
                }
                else
                {
                    printf("Failed to load MRA: %s\n", cmd.filename.c_str());
                }
                command_queue.pop();
                break;

            case CommandType::RESET:
                if (command_queue.is_verbose())
                    printf("Reset for %llu cycles\n", cmd.count);
                // Set reset signal
                gSimCore.mTop->reset = 1;
                // Run for specified cycles
                gSimCore.Tick(cmd.count);
                // Clear reset signal
                gSimCore.mTop->reset = 0;
                command_queue.pop();
                break;

            case CommandType::SET_DIPSWITCH_A:
                if (command_queue.is_verbose())
                    printf("Set dipswitch A to 0x%llx\n", cmd.count);
                dipswitch_a = cmd.count & 0xff;
                gSimCore.mTop->dswa = dipswitch_a;
                command_queue.pop();
                break;

            case CommandType::SET_DIPSWITCH_B:
                if (command_queue.is_verbose())
                    printf("Set dipswitch B to 0x%llx\n", cmd.count);
                dipswitch_b = cmd.count & 0xff;
                gSimCore.mTop->dswb = dipswitch_b;
                command_queue.pop();
                break;

            case CommandType::EXIT:
                if (command_queue.is_verbose())
                    printf("Exiting...\n");
                running = false;
                command_queue.pop();
                break;

            default:
                printf("Unhandled command type\n");
                command_queue.pop();
                break;
            }
        }
        else if (!command_queue.is_headless())
        {
            gSimCore.mGfxCache->PruneCache();

            // Interactive mode
            if (!ui_begin_frame())
            {
                running = false;
                break;
            }

            // Handle F12 screenshot key
            if (keyboard_state && keyboard_state[SDL_SCANCODE_F12] && !screenshot_key_pressed)
            {
                std::string filename = gSimCore.mVideo->generate_screenshot_filename("sim");
                gSimCore.mVideo->save_screenshot(filename.c_str());
                screenshot_key_pressed = true;
            }
            else if (keyboard_state && !keyboard_state[SDL_SCANCODE_F12])
            {
                screenshot_key_pressed = false;
            }

            gSimCore.mTop->dswa = dipswitch_a & 0xff;
            gSimCore.mTop->dswb = dipswitch_b & 0xff;
            gSimCore.mTop->pause = gSimCore.mSystemPause;

            gSimCore.mTop->joystick_p1 = imgui_get_buttons() & 0xffff;
            gSimCore.mTop->start = (imgui_get_buttons() >> 16) & 0xffff;

            if (gSimCore.mSimulationRun || gSimCore.mSimulationStep)
            {
                if (gSimCore.mSimulationStepVblank)
                {
                    gSimCore.TickUntil([&] { return gSimCore.mTop->vblank == 0; });
                    gSimCore.TickUntil([&] { return gSimCore.mTop->vblank != 0; });
                }
                else
                {
                    gSimCore.Tick(gSimCore.mSimulationStepSize);
                }
                gSimCore.mVideo->update_texture();
            }
            gSimCore.mSimulationStep = false;

            ui_draw();

            ui_end_frame();
        }
        else
        {
            // Headless mode with no commands left
            running = false;
        }
    }

    gSimCore.Shutdown();

    delete state_manager;
    return 0;
}
