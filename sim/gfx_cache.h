#if !defined(GFX_CACHE_H)
#define GFX_CACHE_H 1

#include <SDL.h>

#include "sim_core.h"
#include "sim_hierarchy.h"

#include "PGM.h"
#include "PGM___024root.h"

struct GfxCacheEntry
{
    SDL_Texture *mTexture;
    uint64_t mLastUsed;

    GfxCacheEntry() : mTexture(nullptr), mLastUsed(0)
    {
    }

    GfxCacheEntry(SDL_Texture *tex, uint64_t used) : mTexture(tex), mLastUsed(used)
    {
    }

    GfxCacheEntry(GfxCacheEntry &&o)
    {
        mTexture = o.mTexture;
        mLastUsed = o.mLastUsed;
        o.mTexture = nullptr;
    }

    ~GfxCacheEntry()
    {
        if (mTexture)
            SDL_DestroyTexture(mTexture);
    }

    GfxCacheEntry(const GfxCacheEntry &) = delete;
};

struct GfxPalette
{
    uint32_t mRGB[32];
    uint64_t mHash;
    uint64_t mCount;
};

#define FNV_64_PRIME ((uint64_t)0x100000001b3ULL)

enum class GfxCacheFormat
{
    IGS023_BG,
    IGS023_FG,
    IGS023_OBJ,
};

class GfxCache
{
  public:
    std::map<uint64_t, GfxCacheEntry> mCache;
    GfxPalette mPalettes[256];

    MemoryInterface *mColormem;
    SDL_Renderer *mRenderer = nullptr;
    uint64_t mUsedIdx = 0;

    static uint64_t CalcHash(const void *buf, size_t len, uint64_t hval)
    {
        const unsigned char *bp = (const unsigned char *)buf;
        const unsigned char *be = bp + len;

        while (bp < be)
        {
            hval *= FNV_64_PRIME;
            hval ^= (uint64_t)*bp++;
        }

        return hval;
    }

    const GfxPalette *GetPalette(uint16_t index)
    {
        index %= 256;

        GfxPalette *entry = &mPalettes[index];
        entry->mCount++;

        if (entry->mHash != 0 && (entry->mCount & 0xff) != 0)
        {
            return entry;
        }

        uint16_t addr = index * 32;
        uint8_t rawpal[64];
        mColormem->Read(addr, 64, rawpal);

        uint64_t hash = CalcHash(rawpal, sizeof(rawpal), 0);

        if (hash == entry->mHash)
        {
            return entry;
        }

        entry->mHash = hash;

        for (int i = 0; i < 32; i++)
        {
            uint8_t r, g, b;
            uint16_t p = (rawpal[i * 2 + 1]) | (rawpal[i * 2 + 0] << 8);

            r = ((p & 0x7c00) >> 7);
            g = ((p & 0x03e0) >> 2);
            b = ((p & 0x001f) << 3);

            uint32_t c = (r << 24) | (g << 16) | (b << 8);
            if (i & 15)
                entry->mRGB[i] = c | 0xff;
            else
                entry->mRGB[i] = 0xff0000ff;
        }
        return entry;
    }

    void Init(SDL_Renderer *renderer, MemoryInterface &colormem)
    {
        mCache.clear();
        mUsedIdx = 0;
        mRenderer = renderer;
        mColormem = &colormem;
    }

    void PruneCache()
    {
        if (mCache.size() < 2048)
            return;

        size_t numToRemove = 128;

        std::vector<std::pair<uint64_t, uint64_t>> hashAges;
        for (const auto &it : mCache)
        {
            hashAges.push_back({it.second.mLastUsed, it.first});
        }
        std::sort(hashAges.begin(), hashAges.end());
        for (size_t i = 0; i < numToRemove; i++)
        {
            mCache.erase(hashAges[i].second);
        }
    }

    SDL_Texture *GetTexture(MemoryRegion region, GfxCacheFormat format, uint16_t code, uint8_t paletteIdx)
    {
        return GetTexture(gSimCore.Memory(region), format, code, paletteIdx);
    }

    SDL_Texture *GetTexture(MemoryInterface &gfxmem, GfxCacheFormat format, uint16_t code, uint8_t paletteIdx)
    {
        const GfxPalette *palette = GetPalette(paletteIdx);

        int size;
        int bytesize;

        switch (format)
        {
        case GfxCacheFormat::IGS023_BG:
        {
            size = 32;
            bytesize = 640;
            break;
        }

        case GfxCacheFormat::IGS023_FG:
        {
            size = 8;
            bytesize = 8 * 4;
            break;
        }

        case GfxCacheFormat::IGS023_OBJ:
        {
            size = 8;
            bytesize = 8 * 2;
            break;
        }
        }

        uint32_t addr = (code * bytesize);
        uint8_t srcData[640];

        uint64_t hash;

        hash = CalcHash(&code, sizeof(code), palette->mHash);

        auto it = mCache.find(hash);
        if (it != mCache.end())
        {
            it->second.mLastUsed = mUsedIdx;
            mUsedIdx++;
            return it->second.mTexture;
        }

        gfxmem.Read(addr, bytesize, srcData);

        uint32_t pixels[32 * 32];
        const uint32_t *pal32 = palette->mRGB;

        uint32_t *dest = pixels;
        const uint8_t *src = srcData;

        if (format == GfxCacheFormat::IGS023_BG)
        {
            for (int i = 0; i < 32 * 4; i++)
            {
                uint64_t bits = (uint64_t)src[0] << 0 | (uint64_t)src[1] << 8 | (uint64_t)src[2] << 16 | (uint64_t)src[3] << 24 | (uint64_t)src[4] << 32;
                
                dest[0] = pal32[(bits >> 0) & 0x1f];
                dest[1] = pal32[(bits >> 5) & 0x1f];
                dest[2] = pal32[(bits >> 10) & 0x1f];
                dest[3] = pal32[(bits >> 15) & 0x1f];
                dest[4] = pal32[(bits >> 20) & 0x1f];
                dest[5] = pal32[(bits >> 25) & 0x1f];
                dest[6] = pal32[(bits >> 30) & 0x1f];
                dest[7] = pal32[(bits >> 35) & 0x1f];

                dest += 8;
                src += 5;
            }
        }
        else if (format == GfxCacheFormat::IGS023_FG)
        {
            for (int i = 0; i < 8; i++)
            {
                dest[0] = pal32[src[0] & 0xf];
                dest[1] = pal32[(src[0] >> 4) & 0xf];
                dest[2] = pal32[src[1] & 0xf];
                dest[3] = pal32[(src[1] >> 4) & 0xf];
                dest[4] = pal32[src[2] & 0xf];
                dest[5] = pal32[(src[2] >> 4) & 0xf];
                dest[6] = pal32[src[3] & 0xf];
                dest[7] = pal32[(src[3] >> 4) & 0xf];

                dest += 8;
                src += 4;
            }
        }

        SDL_Texture *tex = SDL_CreateTexture(mRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, size, size);

        SDL_UpdateTexture(tex, nullptr, pixels, size * 4);

        auto r = mCache.emplace(hash, GfxCacheEntry(tex, mUsedIdx));
        return r.first->second.mTexture;
    }
};

#endif
