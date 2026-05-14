#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

inline HANDLE PHANDLE      = nullptr;
inline bool   IS_SCROLLING = false;

static struct {
    SP<Config::Values::CStringValue> layout;
    // grid
    SP<Config::Values::CIntValue>    columns;
    SP<Config::Values::CIntValue>    gapSize;
    SP<Config::Values::CColorValue>  bgCol;
    SP<Config::Values::CStringValue> workspaceMethod;
    SP<Config::Values::CIntValue>    skipEmpty;
    SP<Config::Values::CIntValue>    gestureDistance;
    // scrolling
    SP<Config::Values::CIntValue>   scrollMovesUpDown;
    SP<Config::Values::CFloatValue> defaultZoom;
    SP<Config::Values::CIntValue>   followMouse;
    SP<Config::Values::CColorValue> activeColor;
    SP<Config::Values::CColorValue> inactiveColor;
} configValues;