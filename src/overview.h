#ifndef SCROLLER_OVERVIEW_H
#define SCROLLER_OVERVIEW_H

#include <hyprland/src/SharedDefs.hpp>
#include <vector>

class Overview {
public:
    Overview();
    ~Overview();
    bool is_initialized() const { return initialized; }
    bool enable(WORKSPACEID workspace);
    void disable(WORKSPACEID workspace);
    bool overview_enabled(WORKSPACEID workspace) const;
    void set_scale(WORKSPACEID workspace, float scale);

    struct OverviewData {
        WORKSPACEID workspace = WORKSPACE_INVALID;
        bool overview = false;
        float scale = 1.0F;
        float scale_i = 1.0F; // inverse scale
    };
    OverviewData& data_for(WORKSPACEID workspace);

private:
    bool overview_enabled() const;
    bool enable_hooks();
    void disable_hooks();

    bool initialized = false;
    std::vector<OverviewData> _workspaceData;
};

#endif // SCROLLER_OVERVIEW_H
