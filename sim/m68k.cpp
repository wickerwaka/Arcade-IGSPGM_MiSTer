#include "m68k.h"
#include "sim_core.h"
#include "sim_hierarchy.h"
#include "dis68k/dis68k.h"
#include "PGM.h"
#include "PGM___024root.h"

M68K::M68K() {}

void M68K::MapMemory(uint32_t address, uint32_t mask, const MemoryInterface& memory)
{
    mMemory.push_back({address, mask, memory});
}

uint32_t M68K::GetPC()
{
    return G_PGM_SIGNAL(m68000, excUnit, PcL) |
           (G_PGM_SIGNAL(m68000, excUnit, PcH) << 16);
}

M68KRegisters M68K::GetRegisters()
{
    M68KRegisters regs;
    for( int i = 0; i < 17; i++ )
    {
        regs.r[i] = G_PGM_SIGNAL(m68000, excUnit, regs68L)[i] |
            (G_PGM_SIGNAL(m68000, excUnit, regs68H)[i] << 16);
    }

    return regs;
}

const M68KInstruction& M68K::GetInstruction(uint32_t address)
{
    // Check cache first
    auto it = mDisasmCache.find(address);
    if (it != mDisasmCache.end())
        return it->second;

    // Find memory region and read bytes for disassembly
    uint8_t buffer[16];
    bool found = false;
    for (const auto& mem : mMemory) {
        if ((address & mem.mMask) == mem.mAddress) {
            uint32_t offset = address & ~mem.mMask;
            mem.mMemory.Read(offset, sizeof(buffer), buffer);
            found = true;
            break;
        }
    }

    M68KInstruction inst;
    inst.mAddress = address;

    if (found) {
        Dis68k dis(buffer, buffer + sizeof(buffer), address);
        char decoded[128];
        uint32_t instAddr;
        dis.disasm(&instAddr, decoded, sizeof(decoded));
        inst.mDisasm = decoded;
        inst.mLength = dis.getaddress() - address;
    } else {
        inst.mDisasm = "???\n";
        inst.mLength = 2;
    }

    mDisasmCache[address] = inst;
    return mDisasmCache[address];
}
