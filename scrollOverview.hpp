#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <vector>

#include "IOverview.hpp"

class CMonitor;

class CScrollOverview : public IOverview {
  public:
    CScrollOverview(PHLWORKSPACE startedOn_, bool swipe = false);
    virtual ~CScrollOverview();

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

  private:
    void   updateHoverFocus();
    void   redrawWorkspace(PHLWORKSPACE w, bool forcelowres = false);
    void   redrawAll(bool forcelowres = false);
    void   onWorkspaceChange();
    void   highlightHoverDebug();
    void   moveViewportWorkspace(bool up);

    bool   damageDirty              = false;
    size_t viewportCurrentWorkspace = 0;

    struct SWindowImage {
        PHLWINDOWREF             pWindow;
        SP<Render::IFramebuffer> fb;
        bool                     highlight = false;
        CHyprSignalListener      windowCommit;
        Vector2D                 lastWindowPosition, lastWindowSize;
    };

    void redrawWindowImage(SP<SWindowImage>);

    struct SWorkspaceImage {
        PHLWORKSPACE                  pWorkspace;
        CBox                          box;
        std::vector<SP<SWindowImage>> windowImages;
    };

    SP<Render::IFramebuffer>         backgroundFb;

    Vector2D                         lastMousePosLocal = Vector2D{};

    PHLWINDOWREF                     closeOnWindow;
    PHLWORKSPACE                     closeOnWorkspace;

    std::vector<SP<SWorkspaceImage>> images;
    SP<SWorkspaceImage>              imageForWorkspace(PHLWORKSPACE w);

    PHLWORKSPACE                     startedOn;

    PHLANIMVAR<float>                scale;
    PHLANIMVAR<Vector2D>             viewOffset;

    bool                             closing = false;

    CHyprSignalListener              mouseMoveHook;
    CHyprSignalListener              mouseButtonHook;
    CHyprSignalListener              touchMoveHook;
    CHyprSignalListener              touchDownHook;
    CHyprSignalListener              mouseAxisHook;
    CHyprSignalListener              windowOpenHook;
    CHyprSignalListener              windowFocusHook;

    bool                             swipe             = false;
    bool                             swipeWasCommenced = false;

    // Window Dragging State
    bool                             isDragging = false;
    PHLWINDOWREF                     draggedWindow;
    PHLWORKSPACE                     draggedWindowOriginalWorkspace;
    Vector2D                         dragStartCursorPos;
    Vector2D                         dragOffset;

    PHLWINDOWREF                     hoveredWindow; // The window being hovered over during a drag
    bool                             insertBefore = false; // Whether to insert before or after the hovered window

    CHyprSignalListener              touchUpHook;

    friend class CScrollOverviewPassElement;
};
