#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl.h>
#include <implot.h>
#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif
#include <cstdio>

#include "app.h"
#include "dashboard.h"
#include "dashboardpage.h"
#include "fair_header.h"
#include "flowgraph.h"
#include "flowgraph/datasink.h"
#include "flowgraph/datasource.h"
#include "flowgraph/fftblock.h"
#include "flowgraph/sumblock.h"
#include "flowgraphitem.h"
#include "opendashboardpage.h"

CMRC_DECLARE(ui_assets);

namespace DigitizerUi {

struct SDLState {
    SDL_Window   *window    = NULL;
    SDL_GLContext glContext = NULL;
};

} // namespace DigitizerUi

static void main_loop(void *);

ImFont     *addDefaultFont(float pixel_size) {
    ImGuiIO     &io = ImGui::GetIO();
    ImFontConfig config;
    config.SizePixels = pixel_size;
    // high oversample to have better looking text when zooming in on the flowgraph
    config.OversampleH = config.OversampleV = 4;
    config.PixelSnapH                       = true;
    ImFont *font                            = io.Fonts->AddFontDefault(&config);
    return font;
}

int main(int argc, char **argv) {
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // For the browser using Emscripten, we are going to use WebGL1 with GL ES2.
    // It is very likely the generated file won't work in many browsers.
    // Firefox is the only sure bet, but I have successfully run this code on
    // Chrome for Android for example.
    const char *glsl_version = "#version 100";
    // const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_WindowFlags window_flags = (SDL_WindowFlags) (SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    DigitizerUi::SDLState sdlState;
    sdlState.window    = SDL_CreateWindow("opendigitizer UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    sdlState.glContext = SDL_GL_CreateContext(sdlState.window);
    if (!sdlState.glContext) {
        fprintf(stderr, "Failed to initialize WebGL context!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    // For an Emscripten build we are disabling file-system access, so let's not
    // attempt to do a fopen() of the imgui.ini file. You may manually call
    // LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = NULL;

    // Setup Dear ImGui style
    // ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();
    ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(sdlState.window, sdlState.glContext);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // io.Fonts->AddFontDefault();
#ifndef IMGUI_DISABLE_FILE_FUNCTIONS
    // io.Fonts->AddFontFromFileTTF("fonts/Roboto-Medium.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("fonts/Cousine-Regular.ttf", 15.0f);
    // io.Fonts->AddFontFromFileTTF("fonts/DroidSans.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("fonts/ProggyTiny.ttf", 10.0f);
    // ImFont* font = io.Fonts->AddFontFromFileTTF("fonts/ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    // IM_ASSERT(font != NULL);
#endif

    DigitizerUi::App app = {
#ifdef EMSCRIPTEN
        .executable = "index.html",
#else
        .executable = argv[0],
#endif
        .flowGraph     = {},
        .fgItem        = { &app.flowGraph },
        .dashboardPage = DigitizerUi::DashboardPage(&app.flowGraph),
        .sdlState      = &sdlState
    };

    app.fgItem.newSinkCallback = [&]() mutable {
        int  n    = app.flowGraph.sinkBlocks().size() + 1;
        auto name = fmt::format("sink {}", n);
        app.flowGraph.addSinkBlock(std::make_unique<DigitizerUi::DataSink>(name));
        name = fmt::format("source for sink {}", n);
        app.flowGraph.addSourceBlock(std::make_unique<DigitizerUi::DataSinkSource>(name));
    };

#ifndef EMSCRIPTEN
    app.flowGraph.loadBlockDefinitions(BLOCKS_DIR);
#endif
    // app.flowGraph.parse(opencmw::URI<opencmw::STRICT>("http://localhost:8080/flowgraph"));

    DigitizerUi::DataSource::registerBlockType(&app.flowGraph);
    DigitizerUi::DataSink::registerBlockType(&app.flowGraph);
    DigitizerUi::DataSinkSource::registerBlockType(&app.flowGraph);

    app.flowGraph.addBlockType([]() {
        auto t         = std::make_unique<DigitizerUi::BlockType>("sum sigs");
        t->createBlock = [t = t.get()](std::string_view name) {
            return std::make_unique<DigitizerUi::SumBlock>(name, t);
        };
        t->inputs.resize(2);
        t->inputs[0].name = "in1";
        t->inputs[0].type = "float";

        t->inputs[1].name = "in2";
        t->inputs[1].type = "float";

        t->outputs.resize(1);
        t->outputs[0].name = "out";
        t->outputs[0].type = "float";
        return t;
    }());

    app.flowGraph.addBlockType([]() {
        auto t         = std::make_unique<DigitizerUi::BlockType>("FFT");
        t->createBlock = [t = t.get()](std::string_view name) {
            return std::make_unique<DigitizerUi::FFTBlock>(name, t);
        };
        t->inputs.resize(1);
        t->inputs[0].name = "in1";
        t->inputs[0].type = "float";

        t->outputs.resize(1);
        t->outputs[0].name = "out";
        t->outputs[0].type = "float";
        return t;
    }());

    app.font12 = addDefaultFont(12);
    app.font14 = addDefaultFont(14);
    app.font16 = addDefaultFont(16);

    auto loadIconsFont = [&](auto name) {
        static const ImWchar glyphRanges[] = {
            0xf005, 0xf2ed, // 0xf005 is "", 0xf2ed is "trash can"
            0
        };

        auto fs   = cmrc::ui_assets::get_filesystem();
        auto file = fs.open(name);
        return io.Fonts->AddFontFromMemoryTTF(const_cast<char *>(file.begin()), file.size(), 12, nullptr, glyphRanges);
    };
    app.fontIcons      = loadIconsFont("assets/fontawesome/fa-regular-400.ttf");
    app.fontIconsSolid = loadIconsFont("assets/fontawesome/fa-solid-900.ttf");

    app_header::load_header_assets();

    if (argc > 1) {
        const char *url = argv[1];
        app.loadDashboard(url);
    }

    // This function call won't return, and will engage in an infinite loop, processing events from the browser, and dispatching them.
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &app, 0, true);
#else
    SDL_GL_SetSwapInterval(1); // Enable vsync

    while (app.running) {
        main_loop(&app);
    }
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(sdlState.glContext);
    SDL_DestroyWindow(sdlState.window);
    SDL_Quit();
#endif
    // emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 10);
}

static void main_loop(void *arg) {
    DigitizerUi::App *app = static_cast<DigitizerUi::App *>(arg);

    ImGuiIO &io  = ImGui::GetIO();

    // Poll and handle events (inputs, window resize, etc.)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            app->running = false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(app->sdlState->window))
            app->running = false;
        // Capture events here, based on io.WantCaptureMouse and io.WantCaptureKeyboard
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({ 0, 0 });
    int width, height;
    SDL_GetWindowSize(app->sdlState->window, &width, &height);
    ImGui::SetNextWindowSize({ float(width), float(height) });
    ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    app_header::draw_header_bar("OpenDigitizer", app->font16);

    const bool dashboardLoaded = app->dashboard != nullptr;
    if (!dashboardLoaded) {
        ImGui::BeginDisabled();
    }
    ImGui::BeginTabBar("maintabbar");
    if (ImGui::BeginTabItem("View")) {
        if (dashboardLoaded) {
            app->dashboardPage.draw(app->dashboard.get());
        }

        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Flowgraph")) {
        if (dashboardLoaded) {
            auto contentRegion = ImGui::GetContentRegionAvail();

            app->fgItem.draw(contentRegion);
        }

        ImGui::EndTabItem();
    }
    if (!dashboardLoaded) {
        ImGui::EndDisabled();
    }

    if (ImGui::BeginTabItem("File", nullptr, dashboardLoaded ? 0 : ImGuiTabItemFlags_SetSelected)) {
        app->openDashboardPage.draw(app);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    ImGui::End();

    // Rendering
    ImGui::Render();
    SDL_GL_MakeCurrent(app->sdlState->window, app->sdlState->glContext);
    glViewport(0, 0, (int) io.DisplaySize.x, (int) io.DisplaySize.y);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(app->sdlState->window);
}
