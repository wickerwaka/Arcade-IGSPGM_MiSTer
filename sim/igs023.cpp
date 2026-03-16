#include <SDL.h>

#include "imgui_wrap.h"
#include "sim_core.h"
#include "sim_hierarchy.h"

#include "gfx_cache.h"
#include "PGM.h"
#include "PGM___024root.h"

class IGS023ViewWindow : public Window
{
  public:
    int mLayer = 0;

    IGS023ViewWindow() : Window("IGS023 View")
    {
    }

    void Init()
    {
    }

    uint16_t MemWord(uint32_t addr)
    {
        addr = (addr & 0xffff) >> 1;
        uint8_t high = G_PGM_SIGNAL(vram, ram_h)[addr];
        uint8_t low = G_PGM_SIGNAL(vram, ram_l)[addr];

        return (high << 8) | low;
    }

    uint32_t LayerBaseAddr()
    {
        return mLayer == 0 ? 0x0000 : 0x4000;
    }

    void Draw()
    {
        const int numColumns = mLayer == 0 ? 64 : 32;
        const int numRows = 64;

        const char *layerNames[2] = {"BG", "FG"};
        ImGui::Combo("Layer", &mLayer, layerNames, 2);

        if (ImGui::BeginTable("layer", numColumns))
        {
            uint32_t baseAddr = LayerBaseAddr();
            for (int y = 0; y < numRows; y++)
            {
                ImGui::TableNextRow();
                for (int x = 0; x < numColumns; x++)
                {
                    uint32_t addr = baseAddr + (((y * numColumns) + x) * 4);
                    uint16_t attrib = MemWord(addr + 2);
                    uint16_t code = MemWord(addr);
                    uint16_t color = (attrib >> 1) & 0x1f;
                    uint16_t palette = mLayer == 0 ? ((color + 32) * 2) : (color + 128);
                    ImGui::TableNextColumn();
                    SDL_Texture *tex = gSimCore.mGfxCache->GetTexture(
                                MemoryRegion::TILE_ROM,
                                mLayer == 0 ? GfxCacheFormat::IGS023_BG : GfxCacheFormat::IGS023_FG,
                                code,
                                palette);
                    ImGui::Image((ImTextureID)tex, ImVec2(32, 32));
                    ImGui::Text("%04X", code);
                }
            }
            ImGui::EndTable();
        }
    }
};

IGS023ViewWindow s_IGS023ViewWindow;
