
vxwm - Dynamic Window Manager for X11

vxwm is a lightweight, fast, and highly customizable dynamic window manager for X11. Built with efficiency and minimalism in mind, this version features a curated selection of advanced patches and performance optimizations designed to elevate your daily workflow without bloat.

---

## Key Feature: Integrated Systray

The latest update introduces native Systray (system tray) support, ported and adapted from the traditional dwm ecosystem. It seamlessly handles background application icons (e.g., NetworkManager, volume applets, messaging clients) directly within the status bar, offering flexible positioning, multi-monitor pinning, and adjustable icon spacing.

---

## Features and Patches Included

This build leverages conditional preprocessor directives to deliver a robust and modular feature set:

* **Native Systray:** System notification area integrated into the status bar.
* **Bar Padding and Height Customization:** Advanced vertical and horizontal margins for a modern, floating bar aesthetic.
* **Window Gaps:** Configurable spacing between tiled windows to improve visual clarity.
* **Infinite Tags:** Expanded virtual workspace navigation featuring live coordinates rendering in the bar.
* **Keyboard Move and Resize:** Precision movement and resizing of floating windows directly via keyboard shortcuts.
* **Hardware Media Controls:** Built-in hooks for system brightness, volume, and media playback control (playerctl, wpctl, brightnessctl).
* **Smart Floating Behavior:** Automated centering options for newly spawned floating windows.

---

## Prerequisites

To compile and run vxwm, ensure you have the following dependencies installed on your system:

* **X11 Development Libraries:** libx11-dev (Debian/Ubuntu) or libX11 (Arch Linux).
* **Required Fonts:** gallant12x22 and JetBrainsMono Nerd Font (configured for dmenu and status text).
* **Core Utilities:** st (default terminal), rofi, firefox, brightnessctl, wpctl, playerctl.

---

## Installation and Deployment

Clone the repository, clean any previous builds, and compile the source code:


```
git clone https://github.com/inalbaawetrust/vxwm-systray-patch.git
cd vxwm
make
sudo make clean install

```

### Xprofile / Xinitrc Setup

To initialize vxwm when starting your X session, append the following line to your ~/.xinitrc:

```bash
exec vxwm

```

---

## Configuration

System-wide adjustments are handled entirely at compile-time by modifying config.h.

### Systray Specific Parameters

The following parameters within config.h control the system tray behavior:

| Variable | Default Value | Description |
| --- | --- | --- |
| showsystray | 1 | Toggles the system tray visibility (1 = Enabled, 0 = Disabled). |
| systrayspacing | 2 | Defines the padding/spacing in pixels between system tray icons. |
| systrayonleft | 0 | Position: 0 places the tray on the far right; >0 positions it to the left of the status text. |
| systraypinning | 0 | Monitor binding: 0 follows the selected monitor; >0 pins the tray to a specific monitor ID. |

Note on Autostart: The configuration includes an autostart array pointing to a local status bar script (/home/albaa/.local/bin/statusbar). Ensure you update this path to match your environment before compiling.

---

## Core Keybindings

The primary modifier key (MODKEY) is mapped to the Super key (Windows Key). The secondary modifier (ALTERNATE_MODKEY) is mapped to Alt.

### System and Application Launchers

* Mod + Return: Launch Terminal (st)
* Mod + p: Launch Application Menu (rofi)
* Mod + w: Launch Web Browser (firefox)
* Mod + Shift + c: Terminate focused window
* Mod + b: Toggle status bar visibility
* Mod + Shift + q: Exit vxwm

### Layout and Gap Manipulation

* Mod + t: Switch to Floating layout (><>)
* Mod + f: Switch to Tile layout ([]=)
* Mod + m: Switch to Monocle layout ([M])
* Mod + Space: Toggle between current and previous layout
* Mod + Shift + Space: Toggle floating state on individual windows
* Mod + Minus (- ) / Equal (=): Decrease / Increase window gaps
* Mod + Shift + Equal (=): Reset window gaps to 0

### Floating Window Controls (Keyboard Move/Resize)

* Mod + Arrow Keys: Move floating window position
* Mod + Ctrl + Arrow Keys: Resize floating window dimensions

### Infinite Tags Navigation

* Mod + Shift + Arrow Keys: Move viewport position canvas (Left / Right / Up / Down)
* Mod + r: Reset canvas position back to origin (x:0, y:0)
"""
Original Codebase and Credits: https://codeberg.org/wh1tepearl/vxwm.git
