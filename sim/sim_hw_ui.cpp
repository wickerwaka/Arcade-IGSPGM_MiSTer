
#include "sim_core.h"
#include "sim_hierarchy.h"
#include "imgui_wrap.h"
#include "sim_sdram.h"

#include "m68k.h"

class M68000Window : public Window
{
  public:
    M68000Window() : Window("68000")
    {
    }

    void Init() {};

    void Draw()
    {
        uint32_t curPC = gSimCore.mCPU->GetPC();
        M68KRegisters regs = gSimCore.mCPU->GetRegisters();

        for( int i = 0; i < 17; i++ )
        {
            ImGui::LabelText("Reg", "%08X", regs.r[i]);
        }

        ImGui::LabelText("PC", "%08X", curPC);
        const M68KInstruction &inst = gSimCore.mCPU->GetInstruction(curPC);
        ImGui::TextUnformatted(inst.mDisasm.c_str());
        uint32_t endPC = curPC + inst.mLength;

        if (ImGui::Button("Step"))
        {
            gSimCore.TickUntil([curPC, endPC]{
                uint32_t pc = gSimCore.mCPU->GetPC();
                return (pc < curPC) || (pc >= endPC);
            }, 1000);
        }

    }
};

M68000Window gM68000Window;
