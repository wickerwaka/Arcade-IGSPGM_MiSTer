#if !defined(M68K_H)
#define M68K_H 1

#include <string>
#include <map>
#include <vector>

#include "sim_memory.h"

struct M68KInstruction
{
    std::string mDisasm;
    uint32_t mAddress;
    uint32_t mLength;
};

struct M68KMemory
{
    uint32_t mAddress;
    uint32_t mMask;
    const MemoryInterface& mMemory;
};

union M68KRegisters
{
    uint32_t r[17];
    struct
    {
        uint32_t D0;
        uint32_t D1;
        uint32_t D2;
        uint32_t D3;
        uint32_t D4;
        uint32_t D5;
        uint32_t D6;
        uint32_t D7;
        
        uint32_t A0;
        uint32_t A1;
        uint32_t A2;
        uint32_t A3;
        uint32_t A4;
        uint32_t A5;
        uint32_t A6;

        uint32_t USP;
        uint32_t SSP;
    };
};

class M68K
{
    public:
        M68K();

        void MapMemory(uint32_t address, uint32_t mask, const MemoryInterface& memory);

        uint32_t GetPC();
        M68KRegisters GetRegisters();
        const M68KInstruction& GetInstruction(uint32_t address);

    private:
        std::map<uint32_t, M68KInstruction> mDisasmCache;
        std::vector<M68KMemory> mMemory;
};

#endif
