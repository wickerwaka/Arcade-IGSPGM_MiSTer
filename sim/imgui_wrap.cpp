#include "imgui_wrap.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <stdio.h>
#include <SDL.h>

SDL_Window *sdl_window;
SDL_Renderer *sdl_renderer;

#define BTN_RIGHT 0x0001
#define BTN_LEFT 0x0002
#define BTN_DOWN 0x0004
#define BTN_UP 0x0008
#define BTN_BTN1 0x0010
#define BTN_START 0x00010000

static uint32_t buttons;

bool imgui_init(const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return false;
    }

    // Create window with SDL_Renderer graphics context
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window *window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr)
    {
        SDL_Log("Error creating SDL_Renderer!");
        return false;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable
    // Keyboard Controls io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; //
    // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    sdl_window = window;
    sdl_renderer = renderer;

    Window::SortWindows();

    ImGuiSettingsHandler ini_handler;
    ini_handler.TypeName = "SimWindow";
    ini_handler.TypeHash = ImHashStr("SimWindow");
    ini_handler.ClearAllFn = nullptr;
    ini_handler.ReadOpenFn = Window::SettingsHandler_ReadOpen;
    ini_handler.ReadLineFn = Window::SettingsHandler_ReadLine;
    ini_handler.ApplyAllFn = nullptr;
    ini_handler.WriteAllFn = Window::SettingsHandler_WriteAll;
    ImGui::AddSettingsHandler(&ini_handler);

    for (Window *window : Window::s_windows)
    {
        window->Init();
    }

    return true;
}

bool imgui_begin_frame()
{
    static uint64_t prev_ticks = 0;

    uint64_t ticks = SDL_GetTicks64();
    uint64_t delta = ticks - prev_ticks;
    if (delta < 16)
    {
        SDL_Delay((int)(16 - delta));
    }

    prev_ticks = SDL_GetTicks64();

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            return false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(sdl_window))
            return false;

        if (!ImGui::GetIO().WantCaptureKeyboard)
        {
            uint32_t bits = 0;
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_LEFT:
                    bits = BTN_LEFT;
                    break;
                case SDLK_RIGHT:
                    bits = BTN_RIGHT;
                    break;
                case SDLK_UP:
                    bits = BTN_UP;
                    break;
                case SDLK_DOWN:
                    bits = BTN_DOWN;
                    break;
                case SDLK_1:
                    bits = BTN_START;
                    break;
                case SDLK_LCTRL:
                    bits = BTN_BTN1;
                    break;
                }

                if (event.type == SDL_KEYDOWN)
                    buttons |= bits;
                else
                    buttons &= ~bits;
            }
        }
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Windows"))
        {
            for (Window *window : Window::s_windows)
            {
                ImGui::MenuItem(window->m_title.c_str(), nullptr, &window->m_enabled);
            }
            ImGui::EndMenu();
        }

        char status[128];
        snprintf(status, sizeof(status), "FPS: %.1f", ImGui::GetIO().Framerate);
        float width = ImGui::CalcTextSize(status).x + 5;
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - width, 1));
        ImGui::TextUnformatted(status);
        ImGui::EndMainMenuBar();
    }

    for (Window *window : Window::s_windows)
    {
        window->Update();
    }

    return true;
}

uint32_t imgui_get_buttons()
{
    return buttons;
}

void imgui_end_frame()
{
    ImGui::Render();

    ImGuiIO &io = ImGui::GetIO();
    SDL_RenderSetScale(sdl_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 0);
    SDL_RenderClear(sdl_renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
    SDL_RenderPresent(sdl_renderer);
}

SDL_Renderer *imgui_get_renderer()
{
    return sdl_renderer;
}

void imgui_set_title(const char *title)
{
    SDL_SetWindowTitle(sdl_window, title);
}

std::vector<Window *> Window::s_windows;
Window *Window::s_head = nullptr;

Window::Window(const char *name, ImGuiWindowFlags flags)
{
    m_title = name;
    m_enabled = true;
    m_flags = flags;

    m_next = s_head;
    s_head = this;

    if (s_windows.size() > 0)
    {
        SortWindows();
    }
}

Window::~Window()
{
    // probably should clean up the linked list
}

void Window::Update()
{
    if (m_enabled)
    {
        if (ImGui::Begin(m_title.c_str(), &m_enabled, m_flags))
        {
            Draw();
        }
        ImGui::End();
    }
}

void Window::SortWindows()
{
    s_windows.clear();
    Window *window = s_head;
    while (window)
    {
        s_windows.push_back(window);
        window = window->m_next;
    }

    std::sort(s_windows.begin(), s_windows.end(), [](auto a, auto b) { return a->m_title < b->m_title; });
}

void *Window::SettingsHandler_ReadOpen(ImGuiContext *, ImGuiSettingsHandler *, const char *name)
{
    ImGuiID id = ImHashStr(name);

    for (Window *window : s_windows)
    {
        if (window->m_title == name)
        {
            return window;
        }
    }

    return nullptr;
}

void Window::SettingsHandler_ReadLine(ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line)
{
    Window *window = (Window *)entry;
    int en;
    if (sscanf(line, "IsEnabled=%d", &en) == 1)
    {
        window->m_enabled = en != 0;
    }
}

void Window::SettingsHandler_WriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buf)
{
    // Write to text buffer
    for (Window *window : s_windows)
    {
        buf->appendf("[%s][%s]\n", handler->TypeName, window->m_title.c_str());
        buf->appendf("IsEnabled=%d\n\n", window->m_enabled ? 1 : 0);
    }
}
