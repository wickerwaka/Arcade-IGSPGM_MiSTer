#include "imgui_wrap.h"
#include "sim_ui.h"
#include "imgui_wrap.h"
#include "third_party/imgui_memory_editor.h"
#include "sim_controller.h"
#include "sim_core.h"
#include "sim_hierarchy.h"
#include "PGM.h"
#include "PGM___024root.h"
#include "verilated_fst_c.h"
#include "sim_sdram.h"
#include "sim_ddr.h"

#include <algorithm>
#include <cstring>

class MemoryInterfaceEditor : public MemoryEditor
{
  public:
    MemoryInterfaceEditor(MemoryInterface &mem) : MemoryEditor(), mMemory(mem)
    {
        ReadFn = ReadMem;
        WriteFn = WriteMem;
        UserData = this;
    }

    void DrawContents()
    {
        MemoryEditor::DrawContents(nullptr, mMemory.GetSize());
    }

    static ImU8 ReadMem(const ImU8 *, size_t off, void *user)
    {
        MemoryInterfaceEditor *selfPtr = (MemoryInterfaceEditor *)user;
        ImU8 data;
        selfPtr->mMemory.Read(off, 1, &data);
        return data;
    }

    static void WriteMem(ImU8 *, size_t off, ImU8 data, void *user)
    {
        MemoryInterfaceEditor *selfPtr = (MemoryInterfaceEditor *)user;
        selfPtr->mMemory.Write(off, 1, &data);
    }

    MemoryInterface &mMemory;
};

void UiInit(const char *title)
{
    ImguiInit(title);
}

bool UiBeginFrame()
{
    return ImguiBeginFrame();
}

void UiEndFrame()
{
    ImguiEndFrame();
}

static bool gRefreshStateFiles = true;

void UiGameChanged()
{
    char title[64];
    const char *name = gSimCore.GetGameName();

    snprintf(title, sizeof(title), "IGS PGM - %s", name);
    ImguiSetTitle(title);
    gRefreshStateFiles = true;
}

void UiDraw()
{
    if (ImGui::Begin("Simulation Control"))
    {
        ImGui::LabelText("Ticks", "%llu", gSimCore.mTotalTicks);
        ImGui::Checkbox("Run", &gSimCore.mSimulationRun);
        if (ImGui::Button("Step"))
        {
            gSimCore.mSimulationStep = true;
            gSimCore.mSimulationRun = false;
        }
        ImGui::InputInt("Step Size", &gSimCore.mSimulationStepSize);
        ImGui::Checkbox("Step Frame", &gSimCore.mSimulationStepVblank);

        ImGui::Checkbox("WP Set", &gSimCore.mSimulationWpSet);
        ImGui::SameLine();
        ImGui::InputInt("##wpaddr", &gSimCore.mSimulationWpAddr, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);

        if (ImGui::Button("Reset"))
        {
            gSimController.Reset(100);
        }

        ImGui::SameLine();
        ImGui::Checkbox("Pause", &gSimCore.mSystemPause);

        ImGui::Separator();

        // Save/Restore State Section
        ImGui::Text("Save/Restore State");

        static char sStateFilename[256] = "state.pgmstate";
        ImGui::InputText("State Filename", sStateFilename, sizeof(sStateFilename));

        static std::vector<std::string> sStateFiles;
        static int sSelectedStateFile = -1;

        // Auto-generate filename when file list is loaded/updated
        if (gRefreshStateFiles)
        {
            auto listResult = gSimController.ListStates();
            sStateFiles = listResult.ok ? listResult.value.mStates : std::vector<std::string>{};
            std::string autoName = sStateFiles.size() < 1000 ? "000.pgmstate" : "999.pgmstate";
            if (!sStateFiles.empty())
            {
                for (int i = 0; i < 1000; i++)
                {
                    char candidate[32];
                    snprintf(candidate, sizeof(candidate), "%03d.pgmstate", i);
                    if (std::find(sStateFiles.begin(), sStateFiles.end(), candidate) == sStateFiles.end())
                    {
                        autoName = candidate;
                        break;
                    }
                }
            }
            strncpy(sStateFilename, autoName.c_str(), sizeof(sStateFilename) - 1);
            sStateFilename[sizeof(sStateFilename) - 1] = '\0';
            gRefreshStateFiles = false;
        }

        if (ImGui::Button("Save State"))
        {
            // Ensure filename has .pgmstate extension
            std::string filename = sStateFilename;
            if (filename.size() < 9 || filename.substr(filename.size() - 9) != ".pgmstate")
            {
                filename += ".pgmstate";
                strncpy(sStateFilename, filename.c_str(), sizeof(sStateFilename) - 1);
                sStateFilename[sizeof(sStateFilename) - 1] = '\0';
            }

            if (gSimController.SaveState(sStateFilename).ok)
            {
                // Update file list after successfully saving
                auto listResult = gSimController.ListStates();
                sStateFiles = listResult.ok ? listResult.value.mStates : std::vector<std::string>{};
                // Try to select the newly saved file
                for (size_t i = 0; i < sStateFiles.size(); i++)
                {
                    if (sStateFiles[i] == sStateFilename)
                    {
                        sSelectedStateFile = i;
                        break;
                    }
                }
                // Auto-generate next filename after successful save
                std::string autoName = sStateFilename;
                for (int i = 0; i < 1000; i++)
                {
                    char candidate[32];
                    snprintf(candidate, sizeof(candidate), "%03d.pgmstate", i);
                    if (std::find(sStateFiles.begin(), sStateFiles.end(), candidate) == sStateFiles.end())
                    {
                        autoName = candidate;
                        break;
                    }
                }
                strncpy(sStateFilename, autoName.c_str(), sizeof(sStateFilename) - 1);
                sStateFilename[sizeof(sStateFilename) - 1] = '\0';
            }
        }

        // Show list of state files
        if (sStateFiles.size() > 0)
        {
            ImGui::Text("Available State Files:");
            ImGui::BeginChild("StateFiles", ImVec2(0, 100), true);
            for (size_t i = 0; i < sStateFiles.size(); i++)
            {
                if (ImGui::Selectable(sStateFiles[i].c_str(), sSelectedStateFile == (int)i, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    sSelectedStateFile = (int)i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        gSimController.LoadState(sStateFiles[i].c_str());
                    }
                }
            }
            ImGui::EndChild();
        }
        else
        {
            ImGui::Text("No state files found (*.pgmstate)");
        }

        ImGui::Separator();

        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("Trace Depth", &gSimCore.mTraceDepth, 1, 10,
                            gSimCore.mTraceActive ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None))
        {
            gSimCore.mTraceDepth = std::min(std::max(gSimCore.mTraceDepth, 1), 99);
        }
        ImGui::PopItemWidth();
        ImGui::InputText("Filename", gSimCore.mTraceFilename, sizeof(gSimCore.mTraceFilename),
                         gSimCore.IsTraceActive() ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None);
        if (ImGui::Button(gSimCore.IsTraceActive() ? "Stop Tracing###TraceBtn" : "Start Tracing###TraceBtn"))
        {
            if (gSimCore.IsTraceActive())
            {
                gSimCore.StopTrace();
            }
            else
            {
                if (strlen(gSimCore.mTraceFilename) > 0)
                {
                    gSimCore.StartTrace(gSimCore.mTraceFilename, gSimCore.mTraceDepth);
                }
            }
        }
    }

    ImGui::End();
}

class DipswitchWindow : public Window
{
  public:
    DipswitchWindow() : Window("Dipswitches")
    {
    }

    void Init() {};
    void Draw()
    {
        uint32_t dipSwitchA = gSimController.GetDipSwitchA();
        uint32_t dipSwitchB = gSimController.GetDipSwitchB();

        if (ImGui::BeginTable("dipswitches", 9))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            for (int i = 0; i < 8; i++)
            {
                char n[2];
                n[0] = '0' + i;
                n[1] = 0;
                ImGui::TableSetupColumn(n, ImGuiTableColumnFlags_WidthFixed);
            }
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("DWSA");
            for (int i = 0; i < 8; i++)
            {
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                ImGui::CheckboxFlags("##dwsa", &dipSwitchA, ((uint32_t)1 << i));
                ImGui::PopID();
            }
            ImGui::TableNextColumn();
            ImGui::Text("DWSB");
            for (int i = 0; i < 8; i++)
            {
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                ImGui::CheckboxFlags("##dwsb", &dipSwitchB, ((uint32_t)1 << i));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        gSimController.SetDipSwitchA(static_cast<uint8_t>(dipSwitchA));
        gSimController.SetDipSwitchB(static_cast<uint8_t>(dipSwitchB));
    }
};

DipswitchWindow gDipswitchWindow;

class ROMWindow : public Window
{
  public:
    struct Tab
    {
        const char *mName;
        std::unique_ptr<MemoryInterfaceEditor> mEditor;

        Tab(const char *name, MemoryInterface &memory) : mName(name), mEditor(std::make_unique<MemoryInterfaceEditor>(memory))
        {
        }
    };

    std::vector<Tab> mTabs;

    ROMWindow() : Window("ROM View")
    {
    }

    void Init()
    {
        mTabs.clear();
        mTabs.emplace_back("BIOS", gSimCore.Memory(MemoryRegion::BIOS_ROM));
        mTabs.emplace_back("T", gSimCore.Memory(MemoryRegion::TILE_ROM));
        mTabs.emplace_back("Program", gSimCore.Memory(MemoryRegion::PROGRAM_ROM));
    }

    void Draw()
    {
        if (ImGui::BeginTabBar("rom_tabs"))
        {
            for (auto &it : mTabs)
            {
                if (ImGui::BeginTabItem(it.mName))
                {
                    it.mEditor->DrawContents();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
};

ROMWindow gRomWindow;

class RAMWindow : public Window
{
  public:
    struct Tab
    {
        const char *mName;
        std::unique_ptr<MemoryInterfaceEditor> mEditor;

        Tab(const char *name, MemoryInterface &memory) : mName(name), mEditor(std::make_unique<MemoryInterfaceEditor>(memory))
        {
        }
    };

    std::vector<Tab> mTabs;

    RAMWindow() : Window("RAM View")
    {
    }

    void Init()
    {
        mTabs.clear();
        mTabs.emplace_back("Work", gSimCore.Memory(MemoryRegion::WORK_RAM));
        mTabs.emplace_back("Video", gSimCore.Memory(MemoryRegion::VIDEO_RAM));
        mTabs.emplace_back("Audio", gSimCore.Memory(MemoryRegion::AUDIO_RAM));
        mTabs.emplace_back("Palette", gSimCore.Memory(MemoryRegion::PALETTE_RAM));
    }

    void Draw()
    {
        if (ImGui::BeginTabBar("memory_tabs"))
        {
            for (auto &it : mTabs)
            {
                if (ImGui::BeginTabItem(it.mName))
                {
                    it.mEditor->DrawContents();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
};

RAMWindow gRamWindow;
