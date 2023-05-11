#include "app.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace DigitizerUi {

App::App() {
    setStyle(Style::Light);
}

App &App::instance() {
    static App app;
    return app;
}

void App::openNewWindow() {
#ifdef EMSCRIPTEN
    std::string script = fmt::format("window.open('{}').focus()", executable);
    ;
    emscripten_run_script(script.c_str());
#else
    if (fork() == 0) {
        execl(executable.c_str(), executable.c_str(), nullptr);
    }
#endif
}

void App::loadEmptyDashboard() {
    loadDashboard(DashboardDescription::createEmpty("New dashboard"));
}

void App::loadDashboard(const std::shared_ptr<DashboardDescription> &desc) {
    fgItem.clear();
    dashboard = std::make_unique<Dashboard>(desc);
    dashboard->load();
}

void App::loadDashboard(std::string_view url) {
    namespace fs = std::filesystem;
    fs::path path(url);

    auto     source = DashboardSource::get(path.parent_path().native());
    DashboardDescription::load(source, path.filename(), [this, source](std::shared_ptr<DashboardDescription> &&desc) {
        if (desc) {
            loadDashboard(desc);
            openDashboardPage.addSource(source->path);
        }
    });
}

void App::closeDashboard() {
    dashboard = {};
}

void App::setStyle(Style style) {
    switch (style) {
    case Style::Dark:
        ImGui::StyleColorsDark();
        break;
    case Style::Light:
        ImGui::StyleColorsLight();
        break;
    }
    m_style = style;
    fgItem.setStyle(style);
}

void App::schedule(fu2::unique_function<void()> &&cb) {
    std::lock_guard lock(m_callbacksMutex);
    m_callbacks[0].push_back(std::move(cb));
}

void App::fireCallbacks() {
    {
        std::lock_guard lock(m_callbacksMutex);
        std::swap(m_callbacks[0], m_callbacks[1]);
    }
    for (auto &cb : m_callbacks[1]) {
        cb();
    }
    m_callbacks[1].clear();
}

} // namespace DigitizerUi
