#include <cstdio>
#if !defined(SIM_SDRAM_H)
#define SIM_SDRAM_H 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "file_search.h"
#include "sim_memory.h"

class SimSDRAM : public MemoryInterface
{
  public:
    SimSDRAM(uint32_t sz)
    {
        mSize = sz;
        mMask = sz - 1;
        mData = new uint8_t[mSize];
    }

    ~SimSDRAM()
    {
        delete[] mData;
        mData = nullptr;
    }

    void update_channel_16(int ch, int dly, uint32_t addr, uint8_t req, uint8_t rw, uint8_t be, uint16_t din, uint16_t *dout, uint8_t *ack)
    {
        if (req == *ack)
            return;

        mDelay[ch]--;
        if (mDelay[ch] > 0)
            return;
        mDelay[ch] = rand() % dly;

        addr &= mMask;
        addr &= 0xfffffffe;

        if (rw)
        {
            *dout = (mData[addr + 1] << 8) | mData[addr];
            *ack = req;
        }
        else
        {
            if (be & 1)
                mData[addr + 0] = din & 0xff;
            if (be & 2)
                mData[addr + 1] = (din >> 8) & 0xff;
            *ack = req;
        }
    }

    void update_channel_32(int ch, int dly, uint32_t addr, uint8_t req, uint8_t rw, uint8_t be, uint32_t din, uint32_t *dout, uint8_t *ack)
    {
        if (req == *ack)
            return;

        mDelay[ch]--;
        if (mDelay[ch] > 0)
            return;
        mDelay[ch] = rand() % dly;

        addr &= mMask;
        addr &= 0xfffffffe;

        if (rw)
        {
            *dout = (mData[addr + 3] << 24) | (mData[addr + 2] << 16) | (mData[addr + 1] << 8) | (mData[addr + 0]);
            *ack = req;
        }
        else
        {
            if (be & 1)
                mData[addr + 0] = din & 0xff;
            if (be & 2)
                mData[addr + 1] = (din >> 8) & 0xff;
            if (be & 4)
                mData[addr + 2] = (din >> 16) & 0xff;
            if (be & 8)
                mData[addr + 3] = (din >> 24) & 0xff;
            *ack = req;
        }
    }

    void update_channel_64(int ch, int dly, uint32_t addr, uint8_t req, uint8_t rw, uint8_t be, uint64_t din, uint64_t *dout, uint8_t *ack)
    {
        if (req == *ack)
            return;

        mDelay[ch]--;
        if (mDelay[ch] > 0)
            return;
        mDelay[ch] = rand() % dly;

        addr &= mMask;
        addr &= 0xfffffffe;

        if (rw)
        {
            *dout = ((uint64_t)mData[addr + 7] << 56) | ((uint64_t)mData[addr + 6] << 48) | ((uint64_t)mData[addr + 5] << 40) |
                    ((uint64_t)mData[addr + 4] << 32) | ((uint64_t)mData[addr + 3] << 24) | ((uint64_t)mData[addr + 2] << 16) |
                    ((uint64_t)mData[addr + 1] << 8) | ((uint64_t)mData[addr + 0]);
            *ack = req;
        }
        else
        {
            if (be & 0x01)
                mData[addr + 0] = din & 0xff;
            if (be & 0x02)
                mData[addr + 1] = (din >> 8) & 0xff;
            if (be & 0x04)
                mData[addr + 2] = (din >> 16) & 0xff;
            if (be & 0x08)
                mData[addr + 3] = (din >> 24) & 0xff;
            if (be & 0x10)
                mData[addr + 4] = (din >> 32) & 0xff;
            if (be & 0x20)
                mData[addr + 5] = (din >> 40) & 0xff;
            if (be & 0x40)
                mData[addr + 6] = (din >> 48) & 0xff;
            if (be & 0x80)
                mData[addr + 7] = (din >> 56) & 0xff;
            *ack = req;
        }
    }

    bool load_data(const char *name, int offset, int stride)
    {
        std::vector<uint8_t> buffer;
        if (!g_fs.LoadFile(name, buffer))
        {
            printf("Failed to find file: %s\n", name);
            return false;
        }

        uint32_t addr = offset;
        for (uint8_t byte : buffer)
        {
            mData[addr & mMask] = byte;
            addr += stride;
        }

        printf("Loaded %zu bytes from %s at offset 0x%08X with stride %d\n", buffer.size(), name, offset, stride);
        return true;
    }

    bool load_data16be(const char *name, int offset, int stride)
    {
        std::vector<uint8_t> buffer;
        if (!g_fs.LoadFile(name, buffer))
        {
            printf("Failed to find file: %s\n", name);
            return false;
        }

        // Ensure the buffer mSize is even
        if (buffer.size() % 2 != 0)
        {
            buffer.push_back(0); // Pad with zero if odd
        }

        uint32_t addr = offset;
        for (size_t i = 0; i < buffer.size(); i += 2)
        {
            // Store in big-endian format (swapping bytes)
            mData[addr & mMask] = buffer[i + 1];
            mData[(addr + 1) & mMask] = buffer[i + 0];
            addr += stride;
        }

        printf("Loaded %zu bytes (16-bit BE) from %s at offset 0x%08X\n", buffer.size(), name, offset);
        return true;
    }

    bool save_data(const char *filename)
    {
        FILE *fp = fopen(filename, "wb");
        if (fp == nullptr)
        {
            return false;
        }

        if (fwrite(mData, 1, mSize, fp) != mSize)
        {
            fclose(fp);
            return false;
        }

        fclose(fp);
        return true;
    }

    // ------------------------------------------------------------------
    // MemoryInterface
    virtual void Read(uint32_t address, uint32_t size, void *data) const
    {
        size = ClampSize(mSize, address, size);
        memcpy(data, mData + address, size);
    }

    virtual void Write(uint32_t address, uint32_t size, const void *data)
    {
        size = ClampSize(mSize, address, size);
        memcpy(mData + address, data, size);
    }

    virtual uint32_t GetSize() const
    {
        return mSize;
    }
    virtual bool IsReadonly() const
    {
        return false;
    }

    uint32_t mSize;
    uint32_t mMask;
    uint8_t *mData;
    int mDelay[8];
};

// SimSDRAM global instance is now provided via sim_core.h

#endif
