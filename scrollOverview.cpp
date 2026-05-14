#include "scrollOverview.hpp"
#include <any>
#define private   public
#define protected public
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>

#undef private
#undef protected

#include "OverviewPassElement.hpp"

using namespace Hyprutils::String;

static void clearWithColor(const CHyprColor& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    if (g_pOverview)
        g_pOverview->damage();
}

static void removeOverview(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    g_pEventLoopManager->doLater([] { g_pOverview.reset(); });
}

CScrollOverview::~CScrollOverview() {
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    images.clear(); // otherwise we get a vram leak
    Cursor::overrideController->unsetOverride(Cursor::CURSOR_OVERRIDE_UNKNOWN);
    if (pMonitor)
        pMonitor->m_blurFBDirty = true;
}

CScrollOverview::CScrollOverview(PHLWORKSPACE startedOn_, bool swipe_) : startedOn(startedOn_), swipe(swipe_) {
    // -----------------------------------------------------
    // [模块]: 初始化和设置
    // 负责收集工作区状态、初始化动画变量以及挂载各种输入/窗口事件监听器
    // -----------------------------------------------------
    static const CConfigValue<Config::FLOAT>   PDEFAULTZOOM("plugin:hyprexpo:scrolling:default_zoom");
    static const CConfigValue<Config::INTEGER> ACOL("plugin:hyprexpo:scrolling:active_color");
    static const CConfigValue<Config::INTEGER> ICOL("plugin:hyprexpo:scrolling:inactive_color");
    ACTIVE_COLOR   = CHyprColor(*ACOL);
    INACTIVE_COLOR = CHyprColor(*ICOL);

    const auto PMONITOR = Desktop::focusState()->monitor();
    pMonitor            = PMONITOR;

    for (const auto& w : g_pCompositor->m_workspaces) {
        if (w && w->m_monitor == pMonitor && !w->m_isSpecialWorkspace)
            images.emplace_back(makeShared<SWorkspaceImage>(w.lock()));
    }

    std::sort(images.begin(), images.end(), [](const auto& a, const auto& b) { return a->pWorkspace->m_id < b->pWorkspace->m_id; });

    g_pAnimationManager->createAnimation(1.F, scale, Config::animationTree()->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);
    g_pAnimationManager->createAnimation(Vector2D{}, viewOffset, Config::animationTree()->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);

    scale->setUpdateCallback(damageMonitor);
    viewOffset->setUpdateCallback(damageMonitor);

    if (!swipe)
        *scale = std::clamp((float)*PDEFAULTZOOM, 0.1F, 0.9F);

    lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;

    auto onCursorMove = [this](Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled    = true;
        lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
        updateHoverFocus();
    };

    auto onCursorSelect = [this](Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled = true;

        selectHoveredWorkspace();

        close();
    };

    mouseMoveHook = Event::bus()->m_events.input.mouse.move.listen([onCursorMove](Vector2D pos, Event::SCallbackInfo& info) { onCursorMove(info); });
    touchMoveHook = Event::bus()->m_events.input.touch.motion.listen([onCursorMove](ITouch::SMotionEvent e, Event::SCallbackInfo& info) { onCursorMove(info); });

    mouseAxisHook = Event::bus()->m_events.input.mouse.axis.listen([this](IPointer::SAxisEvent e, Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled = true;

        static const CConfigValue<Config::INTEGER> PZOOM("plugin:hyprexpo:scrolling:scroll_moves_up_down");

        if (e.axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
            *viewOffset = viewOffset->value() + Vector2D{e.delta, 0.0};
        } else if (!*PZOOM) {
            const float VAL = std::clamp(sc<float>(scale->value() + e.delta / -500.F), 0.05F, 0.95F);
            *scale          = VAL;
        } else
            moveViewportWorkspace(e.delta > 0);
    });

    mouseButtonHook = Event::bus()->m_events.input.mouse.button.listen([onCursorSelect](IPointer::SButtonEvent e, Event::SCallbackInfo& info) { onCursorSelect(info); });
    touchDownHook   = Event::bus()->m_events.input.touch.down.listen([onCursorSelect](ITouch::SDownEvent e, Event::SCallbackInfo& info) { onCursorSelect(info); });

    windowOpenHook = Event::bus()->m_events.window.open.listen([this](PHLWINDOW w) {
        if (closing)
            return;

        redrawAll();
    });

    windowFocusHook = Event::bus()->m_events.window.active.listen([this](PHLWINDOW w, Desktop::eFocusReason reason) {
        if (closing || !w || !g_pOverview)
            return;

        if (w->m_workspace->m_monitor != pMonitor)
            return;

        damage(); // Ensure border is updated
    });

    Cursor::overrideController->setOverride("left_ptr", Cursor::CURSOR_OVERRIDE_UNKNOWN);

    redrawAll();

    size_t activeIdx = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i]->pWorkspace && images[i]->pWorkspace == startedOn) {
            activeIdx = i;
            break;
        }
    }

    viewportCurrentWorkspace = activeIdx;
}

void CScrollOverview::selectHoveredWorkspace() {
    size_t activeIdx = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i]->pWorkspace && images[i]->pWorkspace == startedOn) {
            activeIdx = i;
            break;
        }
    }

    const auto VIEWPORT_CENTER = CBox{{}, pMonitor->m_size}.middle();

    float      yoff = -(float)activeIdx * pMonitor->m_size.y * scale->value();

    // First pass: Check if we hit any workspace background
    for (const auto& wimg : images) {
        CBox workspaceBox = {{}, pMonitor->m_size};
        workspaceBox.translate(-VIEWPORT_CENTER).scale(scale->value()).translate(VIEWPORT_CENTER).translate(-viewOffset->value() * scale->value());
        workspaceBox.translate({0.F, yoff});

        if (workspaceBox.containsPoint(lastMousePosLocal)) {
            closeOnWorkspace = wimg->pWorkspace;
            break;
        }
        yoff += pMonitor->m_size.y * scale->value();
    }

    // Second pass: Check if we hit any windows
    yoff       = -(float)activeIdx * pMonitor->m_size.y * scale->value();
    bool found = false;
    for (const auto& wimg : images) {
        for (auto it = wimg->windowImages.rbegin(); it != wimg->windowImages.rend(); ++it) {
            const auto& img    = *it;
            CBox        texbox = {img->pWindow->m_realPosition->value() - pMonitor->m_position, img->pWindow->m_realSize->value()};

            // scale the box to the viewport center
            texbox.translate(-VIEWPORT_CENTER).scale(scale->value()).translate(VIEWPORT_CENTER).translate(-viewOffset->value() * scale->value());

            texbox.translate({0.F, yoff});

            if (texbox.containsPoint(lastMousePosLocal)) {
                closeOnWindow    = img->pWindow;
                closeOnWorkspace = wimg->pWorkspace;
                found            = true;
                break;
            }
        }
        if (found)
            break;
        yoff += pMonitor->m_size.y * scale->value();
    }
}

void CScrollOverview::moveViewportWorkspace(bool up) {
    // -----------------------------------------------------
    // [模块]: 视口逻辑控制
    // 根据用户的滚轮或手势操作，在工作区间垂直滑动，同时控制焦点与桌面切换
    // -----------------------------------------------------
    size_t activeIdx = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i]->pWorkspace && images[i]->pWorkspace == startedOn) {
            activeIdx = i;
            break;
        }
    }

    if (viewportCurrentWorkspace == 0 && !up)
        return;
    if (viewportCurrentWorkspace == images.size() - 1 && up)
        return;

    if (up)
        viewportCurrentWorkspace++;
    else
        viewportCurrentWorkspace--;

    *viewOffset = {viewOffset->value().x, (sc<double>(viewportCurrentWorkspace) - sc<double>(activeIdx)) * pMonitor->m_size.y};

    static const CConfigValue<Config::INTEGER> PFOLLOWMOUSE("plugin:hyprexpo:scrolling:follow_mouse");

    if (!*PFOLLOWMOUSE) {
        // Auto focus workspace ONLY when follow_mouse is OFF
        const auto   PMONITOR = pMonitor.lock();
        PHLWORKSPACE pWS      = images[viewportCurrentWorkspace]->pWorkspace;
        if (pWS && pWS != PMONITOR->m_activeWorkspace) {
            PMONITOR->changeWorkspace(pWS, true, true, true);

            // Find first window to focus
            for (auto& w : g_pCompositor->m_windows) {
                if (w->m_workspace == pWS && validMapped(w)) {
                    Desktop::focusState()->fullWindowFocus(w, Desktop::FOCUS_REASON_KEYBIND);
                    break;
                }
            }
            damage();
        }
    }
}

void CScrollOverview::highlightHoverDebug() {
    // ...
}

SP<CScrollOverview::SWorkspaceImage> CScrollOverview::imageForWorkspace(PHLWORKSPACE w) {
    for (const auto& i : images) {
        if (i->pWorkspace == w)
            return i;
    }
    return nullptr;
}

void CScrollOverview::updateHoverFocus() {
    if (closing)
        return;

    static const CConfigValue<Config::INTEGER> PFOLLOWMOUSE("plugin:hyprexpo:scrolling:follow_mouse");
    if (!*PFOLLOWMOUSE)
        return;

    const auto VIEWPORT_CENTER = CBox{{}, pMonitor->m_size}.middle();
    size_t     activeIdx       = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i]->pWorkspace == startedOn) {
            activeIdx = i;
            break;
        }
    }

    float yoff = -(float)activeIdx * pMonitor->m_size.y * scale->value();
    for (const auto& wimg : images) {
        for (auto it = wimg->windowImages.rbegin(); it != wimg->windowImages.rend(); ++it) {
            const auto& img    = *it;
            CBox        texbox = {img->pWindow->m_realPosition->value() - pMonitor->m_position, img->pWindow->m_realSize->value()};
            texbox.translate(-VIEWPORT_CENTER).scale(scale->value()).translate(VIEWPORT_CENTER).translate(-viewOffset->value() * scale->value());
            texbox.translate({0.F, yoff});
            if (texbox.containsPoint(lastMousePosLocal)) {
                if (img->pWindow != Desktop::focusState()->window()) {
                    Desktop::focusState()->fullWindowFocus(img->pWindow.lock(), Desktop::FOCUS_REASON_KEYBIND);
                }
                return;
            }
        }
        yoff += pMonitor->m_size.y * scale->value();
    }
}

void CScrollOverview::redrawWorkspace(PHLWORKSPACE workspace, bool forcelowres) {
    // -----------------------------------------------------
    // [模块]: 工作区及窗口重绘缓冲
    // 为特定工作区内的所有窗口创建独立的 Framebuffer
    // 实现对不同层级窗口的画面捕获与渲染更新，避开屏幕材质遮挡
    // -----------------------------------------------------
    blockOverviewRendering = true;

    Render::GL::g_pHyprOpenGL->makeEGLCurrent();

    auto image = imageForWorkspace(workspace);

    if (!image) {
        blockOverviewRendering = false;
        return;
    }

    image->windowImages.clear();

    const auto     PMONITOR      = pMonitor.lock();
    const auto     OLD_WORKSPACE = PMONITOR->m_activeWorkspace;
    const bool     OLD_VISIBLE   = workspace->m_visible;
    const Vector2D OLD_MON_SIZE  = PMONITOR->m_size;

    PMONITOR->m_activeWorkspace = workspace;
    workspace->m_visible        = true;
    PMONITOR->m_size            = {30000, 30000}; // Hack to bypass isWindowVisible for off-screen scrolling windows

    std::vector<PHLWINDOW> windows;
    for (const auto& w : g_pCompositor->m_windows) {
        if (!validMapped(w) || w->m_workspace != workspace)
            continue;
        windows.emplace_back(w);
    }

    // Sort windows to preserve rendering order (z-order): Tiled -> Floating -> Fullscreen
    std::stable_sort(windows.begin(), windows.end(), [](const PHLWINDOW& a, const PHLWINDOW& b) {
        auto getZLevel = [](const PHLWINDOW& w) -> int {
            if (w->isFullscreen())
                return 2;
            if (w->m_isFloating)
                return 1;
            return 0;
        };
        return getZLevel(a) < getZLevel(b);
    });

    for (const auto& w : windows) {
        auto img     = image->windowImages.emplace_back(makeShared<SWindowImage>());
        img->pWindow = w;
        img->fb      = g_pHyprRenderer->createFB("hyprexpo");
        img->fb->alloc(pMonitor->m_pixelSize.x, pMonitor->m_pixelSize.y, pMonitor->m_output->state->state().drmFormat);

        if (w->wlSurface() && w->wlSurface()->resource()) {
            img->windowCommit = w->wlSurface()->resource()->m_events.commit.listen([wk = WP<SWindowImage>{img}]() {
                if (!wk || !wk->pWindow)
                    return;

                if (wk->pWindow->wlSurface()->resource()->m_current.accumulateBufferDamage().empty())
                    return;

                // this is a bit hacky but works
                ((CScrollOverview*)g_pOverview.get())->redrawWindowImage(wk.lock());
                g_pOverview->damage();
            });
        }

        redrawWindowImage(img);
    }

    PMONITOR->m_activeWorkspace = OLD_WORKSPACE;
    workspace->m_visible        = OLD_VISIBLE;
    PMONITOR->m_size            = OLD_MON_SIZE;

    blockOverviewRendering = false;
}

void CScrollOverview::redrawWindowImage(SP<SWindowImage> img) {
    if (!img->pWindow)
        return;

    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->beginRender(pMonitor.lock(), fakeDamage, Render::RENDER_MODE_FULL_FAKE, nullptr, img->fb);

    clearWithColor(CHyprColor{0, 0, 0, 0});

    g_pHyprRenderer->renderWindow(img->pWindow.lock(), pMonitor.lock(), Time::steadyNow(), true, Render::RENDER_PASS_ALL, true, true);

    g_pHyprRenderer->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();

    img->lastWindowPosition = img->pWindow->m_realPosition->value();
    img->lastWindowSize     = img->pWindow->m_realSize->value();
}

void CScrollOverview::redrawAll(bool forcelowres) {
    if (!pMonitor)
        return;

    for (const auto& img : images) {
        redrawWorkspace(img->pWorkspace);
    }

    // redraw bg
    if (!backgroundFb || backgroundFb->m_size != pMonitor->m_pixelSize) {
        backgroundFb = g_pHyprRenderer->createFB("hyprexpo-bg");
        backgroundFb->alloc(pMonitor->m_pixelSize.x, pMonitor->m_pixelSize.y, pMonitor->m_output->state->state().drmFormat);
    }

    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->beginRender(pMonitor.lock(), fakeDamage, Render::RENDER_MODE_FULL_FAKE, nullptr, backgroundFb);

    g_pHyprRenderer->renderBackground(pMonitor.lock());

    // 只针对 background 图层 (壁纸层 layer 0) 进行渲染，避免错误渲染其他无用底层导致状态异常或崩溃
    for (auto& ls : pMonitor->m_layerSurfaceLayers[0]) {
        if (validMapped(ls))
            g_pHyprRenderer->renderLayer(ls.lock(), pMonitor.lock(), Time::steadyNow());
    }

    g_pHyprRenderer->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();
}

void CScrollOverview::damage() {
    blockDamageReporting = true;
    g_pHyprRenderer->damageMonitor(pMonitor.lock());
    blockDamageReporting = false;
}

void CScrollOverview::onDamageReported() {
    ;
}

int64_t CScrollOverview::selectedWorkspaceID() const {
    if (closeOnWorkspace)
        return closeOnWorkspace->m_id;
    if (!closeOnWindow)
        return startedOn->m_id;
    return closeOnWindow->m_workspace->m_id;
}

void CScrollOverview::close(bool switchToSelection) {
    // -----------------------------------------------------
    // [模块]: 退出视图模块
    // 负责切换焦点到选择的窗口并播放动画（放大、解除模糊等）
    // 随后注销 Overview 相关的钩子及动画释放缓存
    // -----------------------------------------------------
    closing = true;

    if (!closeOnWorkspace) {
        if (!closeOnWindow)
            closeOnWindow = Desktop::focusState()->window();
        if (closeOnWindow)
            closeOnWorkspace = closeOnWindow->m_workspace;
        else
            closeOnWorkspace = pMonitor->m_activeWorkspace;
    }

    if (switchToSelection && closeOnWorkspace) {

        if (closeOnWorkspace != startedOn) {
            auto OLDWS = startedOn;

            pMonitor->m_activeWorkspace = startedOn;
            Config::Actions::changeWorkspace(closeOnWorkspace);

            g_pDesktopAnimationManager->startAnimation(closeOnWorkspace, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
            g_pDesktopAnimationManager->startAnimation(OLDWS, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);
        }

        if (closeOnWindow && closeOnWindow->m_workspace == closeOnWorkspace) {
            Desktop::focusState()->fullWindowFocus(closeOnWindow.lock(), Desktop::FOCUS_REASON_KEYBIND);
        } else if (!closeOnWindow) {
            // Find first window to focus if we just clicked a workspace
            for (auto& w : g_pCompositor->m_windows) {
                if (w->m_workspace == closeOnWorkspace && validMapped(w)) {
                    Desktop::focusState()->fullWindowFocus(w, Desktop::FOCUS_REASON_KEYBIND);
                    break;
                }
            }
        }
    }

    if (closeOnWorkspace) {
        size_t activeIdx = 0;
        size_t targetIdx = 0;
        for (size_t i = 0; i < images.size(); ++i) {
            if (images[i]->pWorkspace && images[i]->pWorkspace == startedOn) {
                activeIdx = i;
            }
            if (images[i]->pWorkspace && images[i]->pWorkspace == closeOnWorkspace) {
                targetIdx = i;
            }
        }

        *viewOffset = Vector2D{viewOffset->value().x, (sc<double>(targetIdx) - sc<double>(activeIdx)) * pMonitor->m_size.y};
    } else {
        *viewOffset = Vector2D{};
    }

    *scale = 1.F;

    scale->setCallbackOnEnd(removeOverview);
}

void CScrollOverview::onPreRender() {
    ;
}

void CScrollOverview::onWorkspaceChange() {
    ;
}

void CScrollOverview::render() {
    bool needsDamage = false;
    for (const auto& img : images) {
        for (const auto& i : img->windowImages) {
            if (!i->pWindow)
                continue;

            if (i->lastWindowSize != i->pWindow->m_realSize->value() || i->lastWindowPosition != i->pWindow->m_realPosition->value()) {
                needsDamage           = true;
                i->lastWindowPosition = i->pWindow->m_realPosition->value();
                i->lastWindowSize     = i->pWindow->m_realSize->value();
            }
        }
    }

    if (needsDamage)
        damage();
}

void CScrollOverview::fullRender() {
    // -----------------------------------------------------
    // [模块]: 完全主渲染管道
    // 这是插件的核心绘制阶段：
    // 首先绘制纯色背景遮罩以及已经捕获的壁纸(BackgroundFb)
    // 随后依视窗缩放与视口偏移，分别绘制带有边框的窗口 Framebuffer
    // -----------------------------------------------------
    clearWithColor(CHyprColor{0, 0, 0, 1});

    CBox texbox = {{}, pMonitor->m_size};
    texbox.scale(pMonitor->m_scale);
    texbox.round();
    CRegion damage{0, 0, INT16_MAX, INT16_MAX};
    Render::GL::g_pHyprOpenGL->renderTextureInternal(backgroundFb->getTexture(), texbox, {.damage = &damage, .a = 1.0});

    const auto VIEWPORT_CENTER = CBox{{}, pMonitor->m_size}.middle();

    size_t     activeIdx = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i]->pWorkspace && images[i]->pWorkspace == startedOn) {
            activeIdx = i;
            break;
        }
    }

    // render all views
    float yoff = -(float)activeIdx * pMonitor->m_size.y * scale->value();
    for (const auto& wimg : images) {
        bool dirty = false;

        for (const auto& img : wimg->windowImages) {
            if (!img->pWindow) {
                dirty = true;
                continue;
            }

            CBox windowBox = {img->pWindow->m_realPosition->value() - pMonitor->m_position, img->pWindow->m_realSize->value()};

            // Render border
            {
                CBox borderBox = windowBox;
                borderBox.translate(-VIEWPORT_CENTER).scale(scale->value()).translate(VIEWPORT_CENTER).translate(-viewOffset->value() * scale->value());
                borderBox.translate({0.F, yoff});

                borderBox.expand(4 * scale->value());
                borderBox.scale(pMonitor->m_scale).round();

                CHyprColor col;
                if (img->pWindow == Desktop::focusState()->window()) {
                    col = ACTIVE_COLOR;
                } else {
                    col = INACTIVE_COLOR;
                }

                Render::GL::g_pHyprOpenGL->renderRect(borderBox, col, Render::GL::CHyprOpenGLImpl::SRectRenderData{.round = (int)(5 * pMonitor->m_scale)});
            }

            CBox texbox = {img->pWindow->m_realPosition->value() - pMonitor->m_position, pMonitor->m_size};

            // scale the box to the viewport center
            texbox.translate(-VIEWPORT_CENTER).scale(scale->value()).translate(VIEWPORT_CENTER).translate(-viewOffset->value() * scale->value());

            texbox.translate({0.F, yoff});

            texbox.scale(pMonitor->m_scale).round();
            CRegion damage{0, 0, INT16_MAX, INT16_MAX};
            Render::GL::g_pHyprOpenGL->renderTextureInternal(img->fb->getTexture(), texbox, {.damage = &damage, .a = (float)img->pWindow->m_alpha.value()});
        }

        yoff += pMonitor->m_size.y * scale->value();

        if (dirty)
            std::erase_if(wimg->windowImages, [](const auto& e) { return !e->pWindow; });
    }
}

static float hyprlerp(const float& from, const float& to, const float perc) {
    return (to - from) * perc + from;
}

static Vector2D hyprlerp(const Vector2D& from, const Vector2D& to, const float perc) {
    return Vector2D{hyprlerp(from.x, to.x, perc), hyprlerp(from.y, to.y, perc)};
}

void CScrollOverview::setClosing(bool closing_) {
    closing = closing_;
}

void CScrollOverview::resetSwipe() {
    static const CConfigValue<Config::FLOAT> PDEFAULTZOOM("plugin:hyprexpo:scrolling:default_zoom");

    if (closing) {
        close();
        return;
    }

    (*scale)    = (float)*PDEFAULTZOOM;
    m_isSwiping = false;
}

void CScrollOverview::onSwipeUpdate(double delta) {
    static const CConfigValue<Config::FLOAT>   PDEFAULTZOOM("plugin:hyprexpo:scrolling:default_zoom");
    static const CConfigValue<Config::INTEGER> PDISTANCE("plugin:hyprexpo:gesture_distance");

    m_isSwiping = true;

    const float PERC = closing ? std::clamp(delta / (double)*PDISTANCE, 0.0, 1.0) : 1.0 - std::clamp(delta / (double)*PDISTANCE, 0.0, 1.0);

    scale->setValueAndWarp(hyprlerp(1.F, (float)*PDEFAULTZOOM, PERC));
}

void CScrollOverview::onSwipeEnd() {
    static const CConfigValue<Config::FLOAT> PDEFAULTZOOM("plugin:hyprexpo:scrolling:default_zoom");

    if (closing) {
        close();
        return;
    }

    (*scale)    = (float)*PDEFAULTZOOM;
    m_isSwiping = false;
}
