#include "sim_video.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool SimVideo::save_screenshot(const char *filename)
{
    if (!pixels || !width || !height)
        return false;

    int actual_width = width;
    int actual_height = height;

    if (rotated)
    {
        std::swap(actual_width, actual_height);
    }

    uint8_t *rgb_data = new uint8_t[actual_width * actual_height * 3];

    for (int y = 0; y < actual_height; y++)
    {
        for (int x = 0; x < actual_width; x++)
        {
            uint32_t pixel;

            if (rotated)
            {
                int src_x = actual_height - 1 - y;
                int src_y = x;
                pixel = pixels[src_y * width + src_x];
            }
            else
            {
                pixel = pixels[y * width + x];
            }

            int dst_idx = (y * actual_width + x) * 3;
            rgb_data[dst_idx + 0] = (pixel >> 24) & 0xFF;
            rgb_data[dst_idx + 1] = (pixel >> 16) & 0xFF;
            rgb_data[dst_idx + 2] = (pixel >> 8) & 0xFF;
        }
    }

    int result = stbi_write_png(filename, actual_width, actual_height, 3, rgb_data, actual_width * 3);

    delete[] rgb_data;

    if (result)
    {
        screenshot_status = "Screenshot saved";
        screenshot_status_timer = 120;
    }
    else
    {
        screenshot_status = "Screenshot failed";
        screenshot_status_timer = 120;
    }

    return result != 0;
}

std::string SimVideo::generate_screenshot_filename(const char *game_name)
{
    mkdir("screenshots", 0755);

    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);

    std::stringstream filename;
    filename << "screenshots/" << game_name << "_" << std::put_time(timeinfo, "%Y%m%d_%H%M%S") << ".png";

    return filename.str();
}