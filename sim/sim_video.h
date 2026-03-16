#if !defined(SIM_VIDEO_H)
#define SIM_VIDEO_H 1

#include <stdint.h>
#include <SDL.h>
#include <string>

#include "imgui_wrap.h"

class SimVideo : public Window
{
  public:
    SimVideo() : Window("Video Output", ImGuiWindowFlags_NoScrollbar)
    {
    }

    ~SimVideo()
    {
        deinit();
    }

    void Init()
    {
    }

    void init(int w, int h, SDL_Renderer *renderer)
    {
        width = w;
        height = h;
        pixels = new uint32_t[width * height];
        if (renderer)
        {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        }
        else
        {
            texture = nullptr; // Headless mode
        }
        x = 0;
        y = 0;
        rotated = false;
        in_vsync = false;
        in_hsync = false;
        in_ce = false;
    }

    void deinit()
    {
        if (pixels)
            delete[] pixels;
        if (texture)
            SDL_DestroyTexture(texture);

        pixels = nullptr;
        texture = nullptr;
    }

    void clock(bool ce, bool hsync, bool vsync, uint8_t r, uint8_t g, uint8_t b)
    {
        if (!ce)
        {
            in_ce = false;
            return;
        }

        if (in_ce)
            return;

        if (hsync)
        {
            x = 0;
            if (!in_hsync)
                y++;
        }

        if (vsync)
        {
            if (!in_vsync)
                x = 0;
            y = 0;
        }

        in_hsync = hsync;
        in_vsync = vsync;
        in_ce = ce;

        if (!hsync && !vsync)
        {
            uint32_t c = r << 24 | g << 16 | b << 8;
            if (x < width && y < height)
                pixels[(y * width) + x] = c;
            x++;
        }
    }

    void update_texture()
    {
        if (!texture)
            return; // Skip in headless mode

        SDL_Rect region;

        int line_count = height;
        int line_start = 0;
        region.x = 0;
        region.y = line_start;
        region.w = width;
        region.h = line_count;

        void *work;
        int pitch;

        SDL_LockTexture(texture, &region, &work, &pitch);

        for (int line = 0; line < line_count; line++)
        {
            uint8_t *dest = ((uint8_t *)work) + (pitch * line);
            uint32_t *src = pixels + ((line + line_start) * width);
            if (!in_vsync && ((line + line_start) == y))
            {
                memset(dest, 0x2f, pitch);
            }
            else
            {
                memcpy(dest, src, pitch);
            }
        }

        SDL_UnlockTexture(texture);
    }

    bool save_screenshot(const char *filename);
    std::string generate_screenshot_filename(const char *game_name);

    void Draw()
    {
        ImGui::Checkbox("TATE", &rotated);
        ImGui::SameLine();
        ImGui::Text("X: %03d Y: %03d", x, y);

        if (!screenshot_status.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", screenshot_status.c_str());
            if (--screenshot_status_timer <= 0)
                screenshot_status.clear();
        }

        ImVec2 avail_size = ImGui::GetContentRegionAvail();
        int w = avail_size.x;
        int h = rotated ? ((w * 4) / 3) : ((w * 3) / 4);

        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (!window->SkipItems)
        {

            const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, h));
            ImGui::ItemSize(bb);
            if (ImGui::ItemAdd(bb, 0))
            {
                // Render
                ImVec2 uv0, uv1, uv2, uv3;

                if (rotated)
                {
                    uv0 = ImVec2(1, 0);
                    uv1 = ImVec2(1, 1);
                    uv2 = ImVec2(0, 1);
                    uv3 = ImVec2(0, 0);
                }
                else
                {
                    uv0 = ImVec2(0, 0);
                    uv1 = ImVec2(1, 0);
                    uv2 = ImVec2(1, 1);
                    uv3 = ImVec2(0, 1);
                }

                window->DrawList->AddImageQuad((ImTextureID)texture, bb.GetTL(), bb.GetTR(), bb.GetBR(), bb.GetBL(), uv0, uv1, uv2, uv3,
                                               ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
            }
        }
    }

    int width, height;
    uint32_t *pixels = nullptr;

    bool rotated;

    int x, y;
    bool in_hsync, in_vsync, in_ce;
    SDL_Texture *texture = nullptr;

    std::string screenshot_status;
    int screenshot_status_timer = 0;
};

#endif
