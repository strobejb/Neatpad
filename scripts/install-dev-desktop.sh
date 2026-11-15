#!/usr/bin/env bash
#
# Installs a desktop entry and icon for the GTK app as built in this
# checkout, so GNOME shows it in the app grid, the dock and Open With.
# The entry is named for the app's ID, as GNOME matches windows by it.
#
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

app_id="net.catch22.Neatpad"
default_build_dir="${repo_root}/build"
default_icon_source="${repo_root}/src/Neatpad/gtk/icons/neatpad-gnome-512.png"

if [[ $# -gt 2 ]]; then
    echo "Usage: ${0##*/} [build-dir] [icon-file]" >&2
    echo "       ${0##*/} [icon-file]" >&2
    exit 2
fi

build_dir="${1:-${default_build_dir}}"
icon_source="${2:-${default_icon_source}}"

if [[ $# -eq 1 && -f "$1" ]]; then
    build_dir="${default_build_dir}"
    icon_source="$1"
fi

binary="$(cd -- "${build_dir}" 2>/dev/null && pwd)/neatpad"

if [[ ! -x "${binary}" ]]; then
    echo "Neatpad binary not found or not executable: ${build_dir}/neatpad" >&2
    echo "Build first, or pass the build directory as the first argument." >&2
    exit 1
fi

if [[ ! -f "${icon_source}" ]]; then
    echo "Neatpad icon not found: ${icon_source}" >&2
    exit 1
fi

# The icon goes in the folder for its size: a PNG's width is bytes 16-19. The
# theme's largest is 512, and GTK scales a larger icon down from there.
icon_size="$(od -An -tu1 -j16 -N4 "${icon_source}" | awk '{ print $1 * 16777216 + $2 * 65536 + $3 * 256 + $4 }')"

if (( icon_size > 512 )); then
    icon_size=512
fi

data_dir="${XDG_DATA_HOME:-${HOME}/.local/share}"
applications_dir="${data_dir}/applications"
icons_dir="${data_dir}/icons/hicolor/${icon_size}x${icon_size}/apps"
desktop_file="${applications_dir}/${app_id}.desktop"
icon_file="${icons_dir}/${app_id}.png"
desktop_tmp=""
icon_tmp=""

cleanup() {
    [[ -n "${desktop_tmp}" && -e "${desktop_tmp}" ]] && rm -f "${desktop_tmp}"
    [[ -n "${icon_tmp}" && -e "${icon_tmp}" ]] && rm -f "${icon_tmp}"
    return 0
}
trap cleanup EXIT

mkdir -p "${applications_dir}" "${icons_dir}"

# an icon installed before at another size would still be used at that size
rm -f "${data_dir}"/icons/hicolor/*/apps/"${app_id}".png

icon_tmp="$(mktemp "${icons_dir}/${app_id}.png.XXXXXX")"
cp "${icon_source}" "${icon_tmp}"
chmod 0644 "${icon_tmp}"
mv -f "${icon_tmp}" "${icon_file}"
icon_tmp=""

desktop_tmp="$(mktemp "${applications_dir}/${app_id}.desktop.XXXXXX")"
cat > "${desktop_tmp}" <<EOF
[Desktop Entry]
Type=Application
Name=Neatpad
GenericName=Text Editor
Comment=Edit text files, however large
Exec=${binary} %F
Icon=${app_id}
StartupWMClass=neatpad
Categories=GTK;Utility;TextEditor;
MimeType=text/plain;
Keywords=text;editor;log;
StartupNotify=true
Terminal=false
EOF

chmod 0644 "${desktop_tmp}"
mv -f "${desktop_tmp}" "${desktop_file}"
desktop_tmp=""

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${applications_dir}" >/dev/null 2>&1 || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    # GTK trusts a cache over the folder, so an old one must be brought up to
    # date: -t, as a user's hicolor folder usually has no index.theme
    gtk-update-icon-cache -f -t -q "${data_dir}/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "Installed ${desktop_file}"
echo "Installed ${icon_file}"
