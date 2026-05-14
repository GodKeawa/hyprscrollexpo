# HyprExpoScroll
HyprExpoScroll is an overview plugin like niri. Forked from hyprland-plugins:hyprexpo as the original plugin is no longer maintained.

Only supports latest hyprland, not -git. Not actively maintained.
Currently supports Hyprland V0.55.0-4.

https://github.com/user-attachments/assets/c5102f6a-d43c-4c24-8f65-e7308691e8c5

## Usage
Same as normal plugins.

## Config
A great start to configure this plugin would be adding this code to the `plugin` section of your hyprland configuration file:  
```ini
# .config/hypr/hyprland.lua
    plugin = {
        hyprexpo = {
            columns = 3,
            gap_size = 5,
            bg_col = "rgba(33ccffee)",
            workspace_method = "center current",
            skip_empty = false,
            gesture_distance = 300,
            layout = "scrolling",
            scrolling = {
                scroll_moves_up_down = 1,
                follow_mouse = 1,
                default_zoom = 0.5,
                active_color = "rgba(33ccffee)",
                inactive_color = "rgba(595959aa)",
            },
        },
    },
```

### Properties
Note that when layout is set to scrolling, properties for grid mode are ignored(not used). 

| property | type | description | default |
| --- | --- | --- | --- |
columns | number | how many desktops are displayed on one line | `3`
gap_size | number | gap between desktops | `5`
bg_col | color | color in gaps (between desktops) | `rgb(000000)`
workspace_method | [center/first] [workspace] | position of the desktops | `center current`
skip_empty | boolean | whether the grid displays workspaces sequentially by id using selector "r" (`false`) or skips empty workspaces using selector "m" (`true`) | `false`
gesture_distance | number | how far is the max for the gesture | `300`
layout | string | overview rendering style, set to `scrolling` to use scrollable overview | `grid`
scrolling:scroll_moves_up_down | integer | `1` means scroll wheel moves workplaces vertically, `0` zooming | `1`
scrolling:follow_mouse | integer | `1` means focus follows mouse, `0` don't | `1`
scrolling:default_zoom | float | the zoom scale for windows in scroll layout | `0.5`
scrolling:active_color | color | the color of the border of the active window in scroll layout | `rgba(33ccffee)`
scrolling:inactive_color | color | the color of the border of the inactive windows in scroll layout | `rgba(595959aa)`

### Keywords

| name | description | arguments |
| -- | -- | -- | 
| hyprexpo-gesture | same as gesture, but for hyprexpo gestures. Supports: `expo`. | Same as gesture |

### Binding
Example:  
```lua
hl.bind("SUPER + g", function()
    hl.plugin.hyprexpo.expo("toggle")
end)
```

Here are a list of options you can use:  
| option | description |
| --- | --- |
toggle | displays if hidden, hide if displayed
select | selects the hovered desktop
bring | brings a window from the hovered desktop to the current desktop
off | hides the overview
disable | same as `off`
on | displays the overview
enable | same as `on`
