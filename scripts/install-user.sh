#!/bin/sh
set -eu
umask 077

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

[ "$(id -u)" -ne 0 ] || die "Run this installer as your regular CardputerZero user."

bin_dir=$HOME/.local/bin
case ${0##*/} in
    ai-wars-uninstall)
        prefix=$(dirname -- "$(readlink -f -- "$0")")
        data_home=$(dirname -- "$prefix")
        action=uninstall
        ;;
    *)
        case ${XDG_DATA_HOME:-} in
            /*) data_home=$XDG_DATA_HOME ;;
            *) data_home=$HOME/.local/share ;;
        esac
        data_home=$(realpath -m -- "$data_home")
        prefix=$data_home/ai-wars-app
        action=${1:-}
        ;;
esac

shortcut=$data_home/applications/org.aiwars.Game.desktop
game_link=$bin_dir/ai-wars
uninstall_link=$bin_dir/ai-wars-uninstall
launcher=$prefix/launch
uninstaller=$prefix/ai-wars-uninstall
marker=$prefix/.ai-wars-user-install

installed() {
    [ -d "$prefix" ] && [ ! -L "$prefix" ] && [ -f "$marker" ] && [ ! -L "$marker" ]
}

our_link() {
    [ -L "$1" ] && [ "$(readlink -- "$1")" = "$2" ]
}

our_shortcut() {
    [ -f "$shortcut" ] && [ ! -L "$shortcut" ] &&
        grep -qx 'X-AIWars-Managed=true' "$shortcut"
}

case $action in
    install) [ "$#" -eq 2 ] || die "Usage: sh install-user.sh install <ai-wars-arm64.deb>" ;;
    uninstall) ;;
    *) die "Usage: sh install-user.sh install <ai-wars-arm64.deb> | uninstall" ;;
esac

mkdir -p -- "$data_home"
lock_dir=$data_home/.ai-wars-install.lock
mkdir -p -- "$lock_dir"
[ ! -L "$lock_dir" ] || die "Cannot lock the user installation directory."
exec 9<"$lock_dir"
flock 9

case $action in
    uninstall)
        installed || die "AI Wars is not installed for this user."
        if our_shortcut; then rm -- "$shortcut"; fi
        if our_link "$game_link" "$launcher"; then rm -- "$game_link"; fi
        if our_link "$uninstall_link" "$uninstaller"; then rm -- "$uninstall_link"; fi
        rm -rf -- "$prefix"
        printf '%s\n' "Removed AI Wars. Your profiles and screenshots are still available."
        exit 0
        ;;
esac

archive=$(realpath -- "$2")
[ "$(dpkg-deb -f "$archive" Package)" = ai-wars ] &&
    [ "$(dpkg-deb -f "$archive" Architecture)" = arm64 ] ||
    die "Expected an AI Wars arm64 Debian package."

if [ -e "$prefix" ] || [ -L "$prefix" ]; then
    installed || die "An unrelated directory already exists at $prefix."
fi
if [ -e "$shortcut" ] || [ -L "$shortcut" ]; then
    our_shortcut || die "An unrelated shortcut already exists at $shortcut."
fi
if [ -e "$game_link" ] || [ -L "$game_link" ]; then
    our_link "$game_link" "$launcher" || die "An unrelated command already exists at $game_link."
fi
if [ -e "$uninstall_link" ] || [ -L "$uninstall_link" ]; then
    our_link "$uninstall_link" "$uninstaller" ||
        die "An unrelated command already exists at $uninstall_link."
fi

mkdir -p -- "$bin_dir" "$(dirname -- "$shortcut")"
stage=$(mktemp -d "$data_home/.ai-wars-stage.XXXXXX")

cleanup() {
    if [ -d "$stage/previous" ] && [ ! -e "$prefix" ]; then
        mv -- "$stage/previous" "$prefix" || return
    fi
    rm -rf -- "$stage"
}

trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# Extract the payload without invoking dpkg or any maintainer scripts.
dpkg-deb --extract "$archive" "$stage/payload"
payload=$stage/payload
[ -x "$payload/usr/bin/ai-wars" ] &&
    [ -r "$payload/usr/share/ai-wars/economy.json" ] &&
    [ -r "$payload/usr/share/APPLaunch/share/images/ai-wars.png" ] ||
    die "The package is missing its game, economy, or icon."
rm -rf -- "$payload/etc"
touch "$payload/.ai-wars-user-install"

cat > "$payload/launch" <<'EOF'
#!/bin/sh
set -eu
prefix=$(dirname -- "$(readlink -f -- "$0")")
if [ -z "${AI_WARS_ECONOMY:-}" ]; then
    if [ -r /etc/ai-wars/economy.json ]; then
        export AI_WARS_ECONOMY=/etc/ai-wars/economy.json
    else
        export AI_WARS_ECONOMY="$prefix/usr/share/ai-wars/economy.json"
    fi
fi
exec "$prefix/usr/bin/ai-wars" "$@"
EOF
cp -- "$0" "$payload/ai-wars-uninstall"
chmod 700 "$payload/launch" "$payload/ai-wars-uninstall"

desktop_exec() {
    printf '"'
    printf '%s' "$1" | sed 's/[\\`"$]/\\&/g; s/\\/\\\\/g; s/%/%%/g'
    printf '"'
}

{
    printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=AI Wars' 'Categories=Game;' \
        'Terminal=false' 'Actions=Uninstall;' 'X-AIWars-Managed=true'
    printf 'Exec=%s\n' "$(desktop_exec "$launcher")"
    printf 'Icon=%s\n' "$prefix/usr/share/APPLaunch/share/images/ai-wars.png" | sed 's/\\/\\\\/g'
    printf '\n%s\n' '[Desktop Action Uninstall]' 'Name=Uninstall AI Wars'
    printf 'Exec=%s\n' "$(desktop_exec "$uninstaller")"
} > "$stage/ai-wars.desktop"

if installed; then mv -- "$prefix" "$stage/previous"; fi
if ! mv -- "$payload" "$prefix"; then
    die "Could not install AI Wars in $prefix."
fi
mv -fT -- "$stage/ai-wars.desktop" "$shortcut"
ln -sfnT -- "$launcher" "$game_link"
ln -sfnT -- "$uninstaller" "$uninstall_link"
printf 'Installed AI Wars. Launch with %s or use the AI Wars desktop shortcut.\n' "$game_link"
