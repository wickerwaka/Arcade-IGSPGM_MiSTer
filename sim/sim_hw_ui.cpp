
#include "sim_core.h"
#include "sim_hierarchy.h"
#include "imgui_wrap.h"
#include "PGM.h"
#include "PGM___024root.h"
#include "sim_sdram.h"
#include "dis68k/dis68k.h"

#define SWAP32(x) (((x) & 0xff000000) >> 16) | (((x) & 0x00ff0000) >> 16) | (((x) & 0x0000ff00) << 16) | (((x) & 0x000000ff) << 16)

struct SimDebug
{
    uint32_t modified;
    uint32_t zoomy;
    uint32_t dy;
    uint32_t y;
    uint32_t zoomx;
    uint32_t dx;
    uint32_t x;
};

extern SimDebug *sim_debug_data;

struct PresetZoom
{
    uint16_t y;
    uint8_t zoomy;
    uint8_t dy;
    uint16_t x;
    uint8_t zoomx;
    uint8_t dx;
};

class MiscDebugWindow : public Window
{
  public:
    MiscDebugWindow() : Window("Misc Debug")
    {
    }

    void Draw()
    {
        int step = 1;
        int step_fast = 16;
        bool modified = false;

        int v = SWAP32(sim_debug_data->y) & 0xffff;
        if (ImGui::InputScalar("BG1 Y", ImGuiDataType_U32, &v, &step, &step_fast, "%04X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            sim_debug_data->y = SWAP32(v);
            modified = true;
        }

        v = SWAP32(sim_debug_data->zoomy) & 0xff;
        if (ImGui::InputScalar("BG1 ZoomY", ImGuiDataType_U32, &v, &step, &step_fast, "%02X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            sim_debug_data->zoomy = SWAP32(v);
            modified = true;
        }

        v = SWAP32(sim_debug_data->dy) & 0xff;
        if (ImGui::InputScalar("BG1 DY", ImGuiDataType_U32, &v, &step, &step_fast, "%02X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            sim_debug_data->dy = SWAP32(v);
            modified = true;
        }

        v = SWAP32(sim_debug_data->x) & 0xffff;
        if (ImGui::InputScalar("BG1 X", ImGuiDataType_U32, &v, &step, &step_fast, "%04X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            sim_debug_data->x = SWAP32(v);
            modified = true;
        }

        v = SWAP32(sim_debug_data->zoomx) & 0xff;
        if (ImGui::InputScalar("BG1 ZoomX", ImGuiDataType_U32, &v, &step, &step_fast, "%02X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            sim_debug_data->zoomx = SWAP32(v);
            modified = true;
        }

        v = SWAP32(sim_debug_data->dx) & 0xff;
        if (ImGui::InputScalar("BG1 DX", ImGuiDataType_U32, &v, &step, &step_fast, "%02X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            sim_debug_data->dx = SWAP32(v);
            modified = true;
        }

        const PresetZoom presets[] = {
            {0xfed7, 0x00, 0xff, 0x0000, 0x00, 0x00}, {0xfed7, 0x00, 0x00, 0x0000, 0x00, 0x00}, {0xfed9, 0x00, 0x00, 0x0000, 0x00, 0x00},
            {0xff9b, 0x7f, 0x00, 0xffc4, 0xc0, 0xcf}, {0xff9b, 0x7f, 0x00, 0x0003, 0x00, 0x00}, {0xff9b, 0x7f, 0x00, 0x0073, 0x00, 0x00},
            {0xff9b, 0x7f, 0x00, 0xffdb, 0x80, 0x80}, {0xff9b, 0x7f, 0x00, 0xffca, 0xb7, 0xc7}, {0xff9b, 0x7f, 0x00, 0xffbd, 0xe0, 0xe0},
            {0xff9b, 0x7f, 0x00, 0xffba, 0xe0, 0x60},
        };

        if (ImGui::BeginCombo("Preset", "Select Preset", 0))
        {
            for (int n = 0; n < IM_ARRAYSIZE(presets); n++)
            {
                char label[64];
                snprintf(label, sizeof(label), "%04X,%02X,%02X,%04X,%02X,%02X", presets[n].y, presets[n].zoomy, presets[n].dy, presets[n].x,
                         presets[n].zoomx, presets[n].dx);
                if (ImGui::Selectable(label, false))
                {
                    sim_debug_data->zoomy = SWAP32(presets[n].zoomy);
                    sim_debug_data->dy = SWAP32(presets[n].dy);
                    sim_debug_data->y = SWAP32(presets[n].y);
                    sim_debug_data->zoomx = SWAP32(presets[n].zoomx);
                    sim_debug_data->dx = SWAP32(presets[n].dx);
                    sim_debug_data->x = SWAP32(presets[n].x);
                    modified = true;
                }
            }
            ImGui::EndCombo();
        }

        if (modified)
        {
            sim_debug_data->modified++;
            gSimCore.mTop->rootp->sim_top__DOT__pgm_inst__DOT__rom_cache__DOT__version++;
        }
    }
};

// MiscDebugWindow s_MiscDebugWindow;

class M68000Window : public Window
{
  public:
    M68000Window() : Window("68000")
    {
    }

    void Init() {};

    void Draw()
    {
        uint32_t cur_pc = G_PGM_SIGNAL(m68000, excUnit, PcL) | (G_PGM_SIGNAL(m68000, excUnit, PcH) << 16);
        uint32_t regs[18];

        for( int i = 0; i < 18; i++ )
        {
            regs[i] = G_PGM_SIGNAL(m68000, excUnit, regs68L)[i] | (G_PGM_SIGNAL(m68000, excUnit, regs68H)[i] << 16);
            ImGui::LabelText("Reg", "%08X", regs[i]);
        }

        ImGui::LabelText("PC", "%08X", cur_pc);
        Dis68k dis(gSimCore.mSDRAM->mData + cur_pc, gSimCore.mSDRAM->mData + cur_pc + 64, cur_pc);
        char optxt[128];
        uint32_t addr;
        dis.disasm(&addr, optxt, sizeof(optxt));
        uint32_t end_pc = dis.getaddress();
        ImGui::TextUnformatted(optxt);

        if (ImGui::Button("Step"))
        {
            gSimCore.TickUntil([&]{
                uint32_t pc = G_PGM_SIGNAL(m68000, excUnit, PcL) | (G_PGM_SIGNAL(m68000, excUnit, PcH) << 16);
                return (pc < cur_pc) || (pc >= end_pc);
            });
        }

    }
};

M68000Window s_M68000Window;
