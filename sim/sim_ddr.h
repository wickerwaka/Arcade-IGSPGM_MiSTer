#ifndef SIM_DDR_H
#define SIM_DDR_H

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include "file_search.h"
#include "sim_memory.h"

// Class to simulate a 64-bit wide memory device
class SimDDR : public MemoryInterface
{
  public:
    SimDDR(uint32_t base, uint32_t size_bytes)
    {
        // Initialize memory with size rounded up to multiple of 8 bytes
        size = (size_bytes + 7) & ~7; // Round up to multiple of 8
        memory.resize(size, 0);
        base_addr = base;

        // Reset state
        read_complete = false;
        busy = false;
        busy_counter = 0;
        burst_counter = 0;
        burst_size = 0;
    }

    bool load_data(const std::vector<uint8_t> &data, uint32_t offset = 0, uint32_t stride = 1)
    {
        uint32_t mem_offset = offset - base_addr;

        // Check if the file will fit in memory with the stride
        if (mem_offset + (data.size() - 1) * stride + 1 > size)
        {
            printf("Data too large (%u) to fit in memory at specified offset 0x%08x (0x%08x) with "
                   "stride %u\n",
                   (uint32_t)data.size(), offset, mem_offset, stride);
            return false;
        }

        if (stride == 1)
        {
            // Fast path for stride=1 (contiguous data)
            std::copy(data.begin(), data.end(), memory.begin() + mem_offset);
        }
        else
        {
            // Copy to memory with stride
            for (size_t i = 0; i < data.size(); i++)
            {
                memory[mem_offset + i * stride] = data[i];
            }
        }
        return true;
    }

    // Load data from a file into memory at specified offset with optional
    // stride
    bool load_data(const std::string &filename, uint32_t offset = 0, uint32_t stride = 1)
    {
        std::vector<uint8_t> buffer;
        if (!g_fs.LoadFile(filename, buffer))
        {
            printf("Failed to find file: %s\n", filename.c_str());
            return false;
        }

        if (load_data(buffer, offset, stride))
        {
            printf("Loaded %zu bytes from %s at offset 0x%08X with stride %u\n", buffer.size(), filename.c_str(), offset, stride);
            return true;
        }
        return false;
    }

    // Save memory data to a file
    bool save_data(const std::string &filename, uint32_t offset = 0, size_t length = 0)
    {
        uint32_t mem_offset = offset - base_addr;

        if (length == 0)
            length = size - mem_offset;

        if (mem_offset + length > size)
        {
            printf("Invalid offset/length for memory save\n");
            return false;
        }

        FILE *fp = fopen(filename.c_str(), "wb");
        if (!fp)
        {
            printf("Failed to open file for saving memory: %s\n", filename.c_str());
            return false;
        }

        size_t bytes_written = fwrite(&memory[mem_offset], 1, length, fp);
        fclose(fp);

        if (bytes_written != length)
        {
            printf("Failed to write entire data to file: %s\n", filename.c_str());
            return false;
        }

        printf("Saved %zu bytes to %s from offset 0x%08X\n", bytes_written, filename.c_str(), offset);
        return true;
    }

    // Clock the memory, processing read/write operations
    void clock(uint32_t addr, const uint64_t &wdata, uint64_t &rdata, bool read, bool write, uint8_t &busy_out, uint8_t &read_complete_out,
               uint8_t burstcnt = 1, uint8_t byteenable = 0xFF)
    {
        // Update busy status - simulate memory with occasional busy cycles
        if (busy)
        {
            busy_counter--;
            if (busy_counter == 0)
            {
                // If we're completing a read operation
                if (pending_read)
                {
                    read_complete = true;

                    // Prepare read data from the current burst address
                    uint32_t current_burst_addr = (pending_addr & ~0x7) + (burst_size - burst_counter) * 8;
                    current_burst_addr -= base_addr;
                    if (current_burst_addr + 8 <= size)
                    {
                        // Assemble 64-bit word from memory
                        pending_rdata = 0;
                        for (int i = 0; i < 8; i++)
                        {
                            pending_rdata |= static_cast<uint64_t>(memory[current_burst_addr + i]) << (i * 8);
                        }
                    }
                    else
                    {
                        pending_rdata = 0;
                    }

                    // Decrement burst counter
                    burst_counter--;

                    // If burst is complete, clear pending read flag
                    if (burst_counter == 0)
                    {
                        pending_read = false;
                        busy = false;
                    }
                    else
                    {
                        // Otherwise, set up for next word in burst
                        busy_counter = read_latency;
                    }
                }
                else if (burst_counter > 0)
                {
                    // Writing in burst mode, move to next word
                    burst_counter--;

                    if (burst_counter == 0)
                    {
                        busy = false;
                    }
                    else
                    {
                        // Ready for next write in the burst
                        busy = false;
                        busy_counter = 0;
                    }
                }
                else
                {
                    // Normal operation completion
                    busy = false;
                }
            }
        }
        else
        {
            if (read && !pending_read)
            {
                // Start new read operation in burst mode
                busy = true;
                busy_counter = read_latency;
                pending_read = true;
                pending_addr = addr;
                read_complete = false;
                burst_counter = burstcnt;
                burst_size = burstcnt;
            }
            else if (write && (burst_counter == 0 || !busy))
            {
                // Handle start of burst write or individual word in burst
                uint32_t current_burst_addr;

                if (burst_counter == 0)
                {
                    // Starting a new burst write
                    pending_addr = addr;
                    burst_counter = burstcnt - 1; // First word is written now
                    burst_size = burstcnt;
                    current_burst_addr = (addr & ~0x7) - base_addr;
                }
                else
                {
                    // Writing next word in an existing burst
                    current_burst_addr = (pending_addr & ~0x7) + (burst_size - burst_counter) * 8;
                    current_burst_addr -= base_addr;
                    burst_counter--;
                }

                // Perform write operation
                if (current_burst_addr + 8 <= size)
                {
                    // Write 64-bit word to memory, respecting byte enable
                    // signal
                    for (int i = 0; i < 8; i++)
                    {
                        // Only write byte if corresponding bit in byteenable is
                        // set
                        if (byteenable & (1 << i))
                        {
                            memory[current_burst_addr + i] = (wdata >> (i * 8)) & 0xFF;
                        }
                    }
                }

                // If this is the last word in the burst or not a burst
                // operation
                if (burst_counter == 0)
                {
                    // Simulate write latency
                    // TODO - busy usage doesn't match DE-10
                    // busy = true;
                    // busy_counter = write_latency;
                }
            }
        }

        // Set outputs
        busy_out = 0; // busy ? 1 : 0; // TODO - busy_out doesn't match DE-10 DDR
        read_complete_out = read_complete ? 1 : 0;

        if (read_complete)
        {
            rdata = pending_rdata;
            read_complete = false; // Clear completion flag after it's been seen
        }
    }

    // Direct access to memory for debugging/testing
    uint8_t &operator[](size_t addr)
    {
        static uint8_t dummy = 0;
        if (addr < base_addr)
            return dummy;
        size_t offset = addr - base_addr;
        if (offset >= size)
            return dummy;
        return memory[offset];
    }

    // Memory parameters
    void set_read_latency(int cycles)
    {
        read_latency = cycles;
    }
    void set_write_latency(int cycles)
    {
        write_latency = cycles;
    }

    // ------------------------------------------------------------------
    // MemoryInterface
    virtual void Read(uint32_t address, uint32_t sz, void *data) const
    {
        if (address < base_addr)
            return;
        address = address - base_addr;
        sz = ClampSize(size, address, sz);
        memcpy(data, memory.data() + address, sz);
    }

    virtual void Write(uint32_t address, uint32_t sz, const void *data)
    {
        if (address < base_addr)
            return;
        address = address - base_addr;
        sz = ClampSize(size, address, sz);
        memcpy(memory.data() + address, data, sz);
    }

    virtual uint32_t GetSize() const
    {
        return size + base_addr;
    }
    virtual bool IsReadonly() const
    {
        return false;
    }

  private:
    std::vector<uint8_t> memory;
    uint32_t size;
    uint32_t base_addr;

    // Memory timing parameters
    int read_latency = 2;  // Default read latency in clock cycles
    int write_latency = 1; // Default write latency in clock cycles

    // Internal state
    bool busy;
    int busy_counter;
    bool read_complete;
    bool pending_read;
    uint32_t pending_addr;
    uint64_t pending_rdata;

    // Burst operation state
    uint8_t burst_counter; // Counter for remaining words in burst
    uint8_t burst_size;    // Total size of current burst
};

// SimDDR global instance is now provided via sim_core.h

#endif // SIM_DDR_H
