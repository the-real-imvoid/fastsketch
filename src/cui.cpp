#include "cui.hpp"
#include "rgba.h"
#include "file.h"
#include <string>
#include "../external/imgui_impl_sdl2.h"
#include "../external/imgui_impl_sdlrenderer2.h"
#include "../external/imgui.h"
#include "../external/imfilebrowser.h"
#include "../include/undo.h"
#include <SDL_image.h>

#define R(c) (((c) >> 24) & 0xFF)
#define G(c) (((c) >> 16) & 0xFF)
#define B(c) (((c) >> 8 ) & 0xFF)
#define A(c) (((c)      ) & 0xFF)

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;

extern "C" {

    static int brush_size = 4;
    static float color[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // RGBA
    static float color_second[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static bool block_drag = false;
    static int density = 5;
    SDL_Texture *atlas;
    ImTextureID tex;
    ImGui::FileBrowser file_save(ImGuiFileBrowserFlags_EnterNewFilename);
    ImGui::FileBrowser file_open;
    static bool g_ui_initialized = false;
    static SDL_Texture *tex_minimize = nullptr;
    static SDL_Texture *tex_restore  = nullptr;
    static SDL_Texture *tex_close    = nullptr;

    #include <stdint.h>
    #include <stdbool.h>
    #include <math.h>

    enum SaveFormat
    {
        FMT_PNG,
        FMT_JPG,
        FMT_BMP
    };

    bool ends_with(const std::string &s, const std::string &suffix)
    {
        if (s.size() < suffix.size()) return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    SaveFormat get_format(const std::string &path)
    {
        if (path.size() >= 4)
        {
            if (ends_with(path, "jpg") || ends_with(path, "jpeg"))
                return FMT_JPG;
            if (ends_with(path, "bmp"))
                return FMT_BMP;
        }
        return FMT_PNG;
    }

    static std::string ensure_extension(std::string path)
    {
        auto ends = [&](const char *s)
        {
            return path.size() >= strlen(s) &&
            path.compare(path.size() - strlen(s), strlen(s), s) == 0;
        };

        if (ends(".png") || ends(".jpg") || ends(".jpeg") || ends(".bmp"))
            return path;

        return path + ".png"; // default
    }

    static void save_image(const std::string &path,
                           uint32_t *canvas,
                           int width,
                           int height)
    {
        switch (get_format(path))
        {
            case FMT_JPG:
                export_jpg(path.c_str(), canvas, width, height);
                break;

            case FMT_BMP:
                export_bmp(path.c_str(), canvas, width, height);
                break;

            case FMT_PNG:
            default:
                export_png(path.c_str(), canvas, width, height);
                break;
        }
    }

    void set_color(uint32_t c)
    {
        color[0] = R(c)/255.0f;
        color[1] = G(c)/255.0f;
        color[2] = B(c)/255.0f;
        color[3] = A(c)/255.0f;
    }

    int get_brush_size(void)
    {
        return brush_size - 1;
    }

    uint32_t get_color(void)
    {
        uint8_t r = (uint8_t)(color[0] * 255.0f);
        uint8_t g = (uint8_t)(color[1] * 255.0f);
        uint8_t b = (uint8_t)(color[2] * 255.0f);
        uint8_t a = (uint8_t)(color[3] * 255.0f);
        return RGBA(r, g, b, a);
    }

    uint32_t get_secondary_color(void)
    {
        uint8_t r = (uint8_t)(color_second[0] * 255.0f);
        uint8_t g = (uint8_t)(color_second[1] * 255.0f);
        uint8_t b = (uint8_t)(color_second[2] * 255.0f);
        uint8_t a = (uint8_t)(color_second[3] * 255.0f);
        return RGBA(r, g, b, a);
    }

    int get_density(void)
    {
        return density;
    }

    bool ui_wants_mouse(void)
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

    void ui_process_event(SDL_Event *event)
    {
        if (!g_ui_initialized || ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }
        ImGui_ImplSDL2_ProcessEvent(event);
    }

    static inline void swap_colors(void)
    {
        for (int i = 0; i < 4; i++)
        {
            float temp = color[i];
            color[i] = color_second[i];
            color_second[i] = temp;
        }
    }

    static bool SelectableImageButton(const char* str_id, ImTextureID user_texture_id, const ImVec2& size, int current_tool, int target_tool, const ImVec2& uv0 = ImVec2(0,0), const ImVec2& uv1 = ImVec2(1,1))
    {
        bool is_selected = (current_tool == target_tool);

        if (is_selected)
        {
            // Highlight Light Blue background for selected items
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.76f, 0.87f, 1.00f, 1.00f));        // #C2DFFF
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.82f, 0.98f, 1.00f)); // Slightly darker blue
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.74f, 0.96f, 1.00f));
        }
        else
        {
            // Default standard light gray background for unselected tools
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.88f, 0.88f, 0.88f, 1.00f));        // #E1E1E1
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.94f, 0.94f, 0.94f, 1.00f)); // Light hover #F0F0F0
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.78f, 0.78f, 1.00f));
        }

        bool pressed = ImGui::ImageButton(str_id, user_texture_id, size, uv0, uv1);
        ImGui::PopStyleColor(3);
        return pressed;
    }

    void ui_init(SDL_Window *window, SDL_Renderer *renderer)
    {
        g_window = window;
        g_renderer = renderer;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsLight(&style);

        style.Colors[ImGuiCol_WindowBg]         = ImVec4(0.94f, 0.94f, 0.94f, 1.00f); // #F0F0F0
        style.Colors[ImGuiCol_ChildBg]          = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
        style.Colors[ImGuiCol_Border]           = ImVec4(0.78f, 0.78f, 0.78f, 1.00f); // #C7C7C7
        style.Colors[ImGuiCol_FrameBg]          = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // White fields
        style.Colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.96f, 0.98f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_Text]             = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // Soft off-black #202020
        style.Colors[ImGuiCol_PopupBg]          = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
        style.Colors[ImGuiCol_TitleBg]          = ImVec4(0.88f, 0.88f, 0.88f, 1.00f); // #E1E1E1
        style.Colors[ImGuiCol_TitleBgActive]    = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);

        style.WindowRounding = 0.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 4.0f;

        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);

        atlas = IMG_LoadTexture(g_renderer, "../assets/favicon.png");
        tex = (ImTextureID)atlas;
        SDL_SetTextureScaleMode(atlas, SDL_ScaleModeNearest);
        SDL_SetWindowResizable(window, SDL_TRUE);

        file_save.SetTitle("Save Image");
        file_save.SetTypeFilters({".png", ".bmp", ".jpg", ".jpeg"});

        file_open.SetTitle("Open Image");
        file_open.SetTypeFilters({".png", ".bmp", ".jpg", ".jpeg", ".tga", ".psd", ".gif", ".hdr", ".pic", ".pnm", ".ppm", ".pgm", ".pbm"});

        tex_minimize = IMG_LoadTexture(g_renderer, "../assets/windowbar/minimize.png");
        tex_restore  = IMG_LoadTexture(g_renderer, "../assets/windowbar/restore.png");
        tex_close    = IMG_LoadTexture(g_renderer, "../assets/windowbar/close.png");
        SDL_SetTextureScaleMode(tex_minimize, SDL_ScaleModeNearest);
        SDL_SetTextureScaleMode(tex_restore,  SDL_ScaleModeNearest);
        SDL_SetTextureScaleMode(tex_close,    SDL_ScaleModeNearest);

        g_ui_initialized = true;
    }

    void ui_begin_frame(Toolbar *tb, SDL_Window *window, int win_w, int win_h,
                        int *width, int *height,
                        uint32_t **canvas,
                        SDL_Renderer *renderer, SDL_Texture **texture)
    {
        block_drag = false;

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const float title_bar_height = 30.0f;
        const float toolbar_height = 58.0f;

        // ---- Top Toolbar ----
        ImGui::SetNextWindowPos(ImVec2(0, title_bar_height));
        ImGui::SetNextWindowSize(ImVec2((float)win_w, toolbar_height));

        ImGuiWindowFlags toolbar_flags = ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoScrollbar;

        // Strip borders out of background panels for a modern look
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("Toolbar", nullptr, toolbar_flags);

        ImVec2 btn_size(48, 48);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));

        static int new_w = 0;
        static int new_h = 0;
        ImGui::SetCursorPosY((toolbar_height - 48) * 0.5f);

        if (SelectableImageButton("new_btn", tex, btn_size, -1, 0, ImVec2(0, 0.6), ImVec2(0.1, 0.7)))
        {
            reset_canvas(*canvas, *width, *height);
            new_w = *width;
            new_h = *height;
            ImGui::OpenPopup("ResizePopup");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New Canvas (Clears and Resizes)");
        ImGui::SameLine();

        if (SelectableImageButton("open_btn", tex, btn_size, -1, 0, ImVec2(0.1, 0.6), ImVec2(0.2, 0.7)))
            file_open.Open();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open Image File");
        ImGui::SameLine();

        if (SelectableImageButton("save_btn", tex, btn_size, -1, 0, ImVec2(0.2, 0.6), ImVec2(0.3, 0.7)))
            file_save.Open();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save Image File");
        ImGui::SameLine();

        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], "|");
        ImGui::SameLine();

        if (SelectableImageButton("undo_btn", tex, btn_size, -1, 0, ImVec2(0.5, 0.3), ImVec2(0.6, 0.4)))
            undo(canvas, width, height);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo Action");
        ImGui::SameLine();

        if (SelectableImageButton("redo_btn", tex, btn_size, -1, 0, ImVec2(0.6, 0.3), ImVec2(0.7, 0.4)))
            redo(canvas, width, height);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo Action");
        ImGui::SameLine();

        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], "|");
        ImGui::SameLine();

        if (SelectableImageButton("sel_btn", tex, btn_size, tb->selected, TOOL_SELECTION, ImVec2(0.9, 0.2), ImVec2(1, 0.3)))
            tb->selected = TOOL_SELECTION;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Selection Tool");
        ImGui::SameLine();

        if (SelectableImageButton("brush_btn", tex, btn_size, tb->selected, TOOL_BRUSH, ImVec2(0, 0), ImVec2(0.1, 0.1)))
            tb->selected = TOOL_BRUSH;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paintbrush Tool");
        ImGui::SameLine();

        if (SelectableImageButton("pencil_btn", tex, btn_size, tb->selected, TOOL_PENCIL, ImVec2(0.1, 0), ImVec2(0.2, 0.1)))
            tb->selected = TOOL_PENCIL;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pencil Tool (Low Opacity)");
        ImGui::SameLine();

        if (SelectableImageButton("eraser_btn", tex, btn_size, tb->selected, TOOL_ERASER, ImVec2(0.2, 0), ImVec2(0.3, 0.1)))
            tb->selected = TOOL_ERASER;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eraser Tool");
        ImGui::SameLine();

        if (SelectableImageButton("airbrush_btn", tex, btn_size, tb->selected, TOOL_AIRBRUSH, ImVec2(0.9, 0.1), ImVec2(1, 0.2)))
            tb->selected = TOOL_AIRBRUSH;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Airbrush Tool");
        ImGui::SameLine();

        if (SelectableImageButton("fill_btn", tex, btn_size, tb->selected, TOOL_FILL, ImVec2(0.3, 0), ImVec2(0.4, 0.1)))
            tb->selected = TOOL_FILL;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Flood Fill Tool");
        ImGui::SameLine();

        if (SelectableImageButton("eyedropper_btn", tex, btn_size, tb->selected, TOOL_EYEDROPPER, ImVec2(0.4, 0), ImVec2(0.5, 0.1)))
            tb->selected = TOOL_EYEDROPPER;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eyedropper Color Picker");
        ImGui::SameLine();

        if (SelectableImageButton("rect_btn", tex, btn_size, tb->selected, TOOL_RECT, ImVec2(0.5, 0), ImVec2(0.6, 0.1)))
            tb->selected = TOOL_RECT;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rectangle Tool");
        ImGui::SameLine();

        if (SelectableImageButton("line_btn", tex, btn_size, tb->selected, TOOL_LINE, ImVec2(0.6, 0.1), ImVec2(0.7, 0.2)))
            tb->selected = TOOL_LINE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Line Tool");
        ImGui::SameLine();

        if (SelectableImageButton("oval_btn", tex, btn_size, tb->selected, TOOL_CIRCLE, ImVec2(0.3, 0.1), ImVec2(0.4, 0.2)))
            tb->selected = TOOL_CIRCLE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oval / Circle Tool");
        ImGui::SameLine();

        if (SelectableImageButton("brightness_btn", tex, btn_size, tb->selected, TOOL_BRIGHTNESS, ImVec2(0.9, 0.8), ImVec2(1, 0.9)))
            tb->selected = TOOL_BRIGHTNESS;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Brightness Adjust Tool");
        ImGui::SameLine();

        if (SelectableImageButton("filter_btn", tex, btn_size, tb->selected, TOOL_FILTER, ImVec2(0.2, 0.8), ImVec2(0.3, 0.9)))
            tb->selected = TOOL_FILTER;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter Effects Tool");
        ImGui::SameLine();

        if (SelectableImageButton("smudge_btn", tex, btn_size, tb->selected, TOOL_SMUDGE, ImVec2(0.2, 0.2), ImVec2(0.3, 0.3)))
            tb->selected = TOOL_SMUDGE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smudge Tool");
        ImGui::SameLine();

        if (SelectableImageButton("mixer_btn", tex, btn_size, tb->selected, TOOL_MIXER, ImVec2(0.3, 0.2), ImVec2(0.4, 0.3)))
            tb->selected = TOOL_MIXER;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Color Mixer Tool");
        ImGui::SameLine();

        if (SelectableImageButton("resize_btn", tex, btn_size, -1, 0, ImVec2(0.8, 0.2), ImVec2(0.9, 0.3)))
        {
            new_w = *width;
            new_h = *height;
            ImGui::OpenPopup("ResizePopup");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resize Canvas Dimensions");
        ImGui::SameLine();

        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], "|");
        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(50.0f);
        static char buffer[32] = "4";
        if (ImGui::InputText("##brush_size", buffer, sizeof(buffer)))
        {
            brush_size = atoi(buffer);
            if (brush_size > 64)  brush_size = 64;
            if (brush_size < 1)   brush_size = 1;
        }
        ImGui::TextUnformatted("Size");
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adjust brush size diameter (0px to 64px)");
        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color[0], color[1], color[2], color[3]));
        if (ImGui::Button("##color_pick", ImVec2(32, 32)))
            ImGui::OpenPopup("color_picker");
        ImGui::PopStyleColor();
        ImGui::TextUnformatted("Color");
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Primary Paint Color");
        ImGui::SameLine();

        if (ImGui::BeginPopup("color_picker"))
        {
            ImGui::ColorPicker4("##picker", color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar);
            ImGui::EndPopup();
        }

        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color_second[0], color_second[1], color_second[2], color_second[3]));
        if (ImGui::Button("##color_pick_s", ImVec2(32, 32)))
            ImGui::OpenPopup("color_picker_s");
        ImGui::PopStyleColor();
        ImGui::TextUnformatted("S.Color");
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Secondary Palette Color");

        if (ImGui::BeginPopup("color_picker_s"))
        {
            ImGui::ColorPicker4("##picker_s", color_second, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::ImageButton("swap_btn", tex, btn_size, ImVec2(0, 0.5), ImVec2(0.1, 0.6)))
            swap_colors();

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderInt("##density_slider", &density, 1, 20, "%d");
        ImGui::TextUnformatted("Effect Strength");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("##opacity_slider", &color[3], 0.0f, 1.0f, "%.2f");
        ImGui::TextUnformatted("Opacity/Alpha");
        ImGui::EndGroup();

        ImGui::PopStyleVar(3);

        if (ImGui::BeginPopupModal("ResizePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputInt("Width",  &new_w);
            ImGui::InputInt("Height", &new_h);
            ImGui::Spacing();

            if (ImGui::Button("Resize"))
            {
                if (new_w > 0 && new_h > 0)
                {
                    resize_canvas(new_w, new_h, canvas, renderer, texture);
                    *width  = new_w;
                    *height = new_h;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)win_w, title_bar_height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("WindowBar", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

        ImVec2 text_size = ImGui::CalcTextSize("FastSketch");
        float window_width = ImGui::GetContentRegionAvail().x;
        float text_x = (window_width - text_size.x) * 0.5f;
        ImGui::SetCursorPosX(text_x > 0 ? text_x : 0);
        ImGui::Text("FastSketch");
        ImGui::SameLine(ImGui::GetWindowWidth() - 85);

        ImVec2 window_btn_size(20, 20);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // Transparent default
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.08f)); // Soft hover gray
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.15f));

        if (ImGui::ImageButton("##MinimizeBtn", (ImTextureID)tex_minimize, window_btn_size))
        {
            SDL_MinimizeWindow(window);
        }
        ImGui::SameLine();

        if (ImGui::ImageButton("##RestoreBtn", (ImTextureID)tex_restore, window_btn_size))
        {
            Uint32 flags = SDL_GetWindowFlags(window);
            if (flags & SDL_WINDOW_MAXIMIZED)
                SDL_RestoreWindow(window);
            else
                SDL_MaximizeWindow(window);
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.25f, 0.25f, 1.0f)); // Classic red close hover
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
        if (ImGui::ImageButton("##CloseBtn", (ImTextureID)tex_close, window_btn_size))
        {
            SDL_Event quit_event;
            quit_event.type = SDL_QUIT;
            SDL_PushEvent(&quit_event);
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsAnyItemHovered())
            block_drag = true;

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        ImGui::End();

        file_save.Display();

        if (file_save.HasSelected())
        {
            std::string path = file_save.GetSelected().string();
            path = ensure_extension(path);
            save_image(path, *canvas, *width, *height);
            file_save.ClearSelected();
        }

        file_open.Display();

        if (file_open.HasSelected())
        {
            std::string path = file_open.GetSelected().string();
            file_open.ClearSelected();
            if (path.empty()) return;

            int new_w = 0, new_h = 0;
            uint32_t *new_canvas = load_image(path.c_str(), &new_w, &new_h);
            if (!new_canvas) return;

            free(*canvas);
            *canvas = new_canvas;

            if (new_w != *width || new_h != *height)
            {
                SDL_DestroyTexture(*texture);
                *texture = SDL_CreateTexture(
                    renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    new_w, new_h);
                SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_BLEND);
            }

            *width  = new_w;
            *height = new_h;

            SDL_UpdateTexture(*texture, NULL, *canvas, new_w * (int)sizeof(uint32_t));
            take_snapshot(*canvas, *width, *height);
        }
    }

    void ui_end_frame(void)
    {
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
    }

    void ui_shutdown(void)
    {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    SDL_HitTestResult SDLCALL hit_test(SDL_Window *win, const SDL_Point *p, void *data)
    {
        (void)data;
        int w, h;
        SDL_GetWindowSize(win, &w, &h);

        const int border = 10;

        bool near_edge = (p->x < border || p->x > w - border ||
        p->y < border || p->y > h - border);

        if (!near_edge && p->y >= 0 && p->y < 30)
        {
            if (block_drag)
                return SDL_HITTEST_NORMAL;
            return SDL_HITTEST_DRAGGABLE;
        }

        return SDL_HITTEST_NORMAL;
    }

}
