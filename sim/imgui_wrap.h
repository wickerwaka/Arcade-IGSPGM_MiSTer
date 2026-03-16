#if !defined(IMGUI_WRAP_H)
#define IMGUI_WRAP_H 1

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_internal.h"

#include <string>
#include <vector>

bool imgui_init(const char *title);
bool imgui_begin_frame();
void imgui_end_frame();
void imgui_set_title(const char *title);

uint32_t imgui_get_buttons();

struct SDL_Renderer;
SDL_Renderer *imgui_get_renderer();

class Window
{
  public:
    Window(const char *name, ImGuiWindowFlags flags = 0);
    virtual ~Window();

    // Called before first frame and after sim is initialized

    void Update();

    virtual void Init() = 0;
    virtual void Draw() = 0;

    static void SortWindows();
    static void *SettingsHandler_ReadOpen(ImGuiContext *, ImGuiSettingsHandler *, const char *name);
    static void SettingsHandler_ReadLine(ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line);
    static void SettingsHandler_WriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buf);

    std::string m_title;
    bool m_enabled;
    ImGuiWindowFlags m_flags;

    Window *m_next;
    static Window *s_head;
    static std::vector<Window *> s_windows;
};

#endif // IMGUI_WRAP_H
