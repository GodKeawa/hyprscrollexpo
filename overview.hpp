#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <vector>

#include "IOverview.hpp"

// saves on resources, but is a bit broken rn with blur.
// hyprland's fault, but cba to fix.
constexpr bool ENABLE_LOWRES = false;

class CMonitor;

class COverview : public IOverview {
  public:
    COverview(PHLWORKSPACE startedOn_, bool swipe = false);
    virtual ~COverview();

    virtual void render();
    virtual void damage();
    virtual void onDamageReported();
    virtual void onPreRender();

    virtual void setClosing(bool closing);

    virtual void resetSwipe();
    virtual void onSwipeUpdate(double delta);
    virtual void onSwipeEnd();

    // close without a selection
    virtual void    close(bool switchToSelection = true);
    virtual void    selectHoveredWorkspace();
    virtual int64_t selectedWorkspaceID() const;

    virtual void    fullRender();

    struct SWorkspaceImage {
        SP<Render::IFramebuffer> fb;
        int64_t                  workspaceID = -1;
        PHLWORKSPACE             pWorkspace;
        CBox                     box;
    };

    int                          SIDE_LENGTH = 3;
    int                          GAP_WIDTH   = 5;
    CHyprColor                   BG_COLOR    = CHyprColor{0.1, 0.1, 0.1, 1.0};

    bool                         damageDirty = false;

    Vector2D                     lastMousePosLocal = Vector2D{};

    int                          openedID  = -1;
    int                          closeOnID = -1;

    std::vector<SWorkspaceImage> images;

    PHLWORKSPACE                 startedOn;

    PHLANIMVAR<Vector2D>         size;
    PHLANIMVAR<Vector2D>         pos;

    bool                         closing = false;

    CHyprSignalListener          mouseMoveHook;
    CHyprSignalListener          mouseButtonHook;
    CHyprSignalListener          touchMoveHook;
    CHyprSignalListener          touchDownHook;

    bool                         swipe             = false;
    bool                         swipeWasCommenced = false;

  private:
    void redrawID(int id, bool forcelowres = false);
    void redrawAll(bool forcelowres = false);
    void onWorkspaceChange();

    friend class COverviewPassElement;
};
