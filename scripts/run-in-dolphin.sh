#!/usr/bin/env bash
# Runs a built Wii .dol in Dolphin and prints the game's log output.
#
# By default the game runs headless: Dolphin's "headless" window platform renders
# offscreen, so nothing appears on the desktop and no display server is involved.
# Pass --window to watch the game instead.
#
# The game's log output (its SYS_Report() lines) only reaches stdout once the
# logger has been configured for it - scripts/configure-dolphin-automation.py
# does that. That script also turns off the "confirm stop" and panic dialogs
# that would otherwise block an unattended run.
#
# Dolphin is killed once the timeout expires; the game powers the console off
# itself when it exits, in which case this returns as soon as that happens.
#
# usage: scripts/run-in-dolphin.sh [options] <game.dol>
#   -w, --window        show Dolphin's window instead of running headless
#   -s, --seconds N     run for at most N seconds (default 15)
#   -d, --dump-frames   save every frame as a PNG and print where
#   -h, --help          show this help
set -euo pipefail

seconds=15
windowed=false
dump_frames=false

dol=""
while [ $# -gt 0 ]; do
	case $1 in
	-w | --window) windowed=true ;;
	-s | --seconds)
		if [ $# -lt 2 ] || ! [ "$2" -ge 1 ] 2>/dev/null; then
			echo "error: --seconds needs a whole number of seconds" >&2
			exit 2
		fi
		seconds=$2
		shift
		;;
	-d | --dump-frames) dump_frames=true ;;
	-h | --help)
		sed -n '/^# usage:/,/^#   -h/p' "$0" | cut -c3-
		exit 0
		;;
	-*)
		echo "error: unknown option $1" >&2
		exit 2
		;;
	*)
		if [ -n "$dol" ]; then
			echo "error: only one game file can be given" >&2
			exit 2
		fi
		dol=$1
		;;
	esac
	shift
done

if [ -z "$dol" ]; then
	echo "usage: $0 [--window] [--seconds N] [--dump-frames] <game.dol>" >&2
	exit 2
fi

if [ ! -f "$dol" ]; then
	echo "error: no such file: $dol" >&2
	exit 1
fi
dol=$(readlink -f "$dol")

snap_name=""
dolphin=$(command -v dolphin-emu-nogui || true)
if [ -z "$dolphin" ] && [ -x /snap/dolphin-fresh/current/usr/games/dolphin-emu-nogui ]; then
	# Inside the snap the binary finds the runtime it was built against, and the
	# snap's own profile is the only one allowed to signal it.
	dolphin=/snap/dolphin-fresh/current/usr/games/dolphin-emu-nogui
	snap_name=dolphin-fresh
fi
if [ -z "$dolphin" ]; then
	echo "error: dolphin-emu-nogui not found in PATH" >&2
	exit 1
fi

if [ -n "$snap_name" ]; then
	user_dir=$HOME/snap/$snap_name/common/.local/share/dolphin-emu
else
	user_dir=${XDG_DATA_HOME:-$HOME/.local/share}/dolphin-emu
fi

args=(-e "$dol")
if [ "$windowed" = false ]; then
	args+=(-p headless)
fi
if [ "$dump_frames" = true ]; then
	args+=(-C Dolphin.Movie.DumpFrames=True
		-C Dolphin.Movie.DumpFramesSilent=True
		-C Graphics.Settings.DumpFramesAsImages=True
		-C Graphics.Settings.DumpFormat=1)
fi

log=$(mktemp)
trap 'rm -f "$log"' EXIT

if [ -n "$snap_name" ]; then
	snap run --shell "$snap_name" -c "exec $(printf '%q ' "$dolphin" "${args[@]}")" >"$log" 2>&1 &
else
	"$dolphin" "${args[@]}" >"$log" 2>&1 &
fi
runner=$!
# Keep bash from printing a job-control notice when the wrapper is killed below.
disown "$runner" 2>/dev/null || true

deadline=$((SECONDS + seconds))
while kill -0 "$runner" 2>/dev/null && [ "$SECONDS" -lt "$deadline" ]; do
	sleep 0.2
done

# The emulator is killed by its command line rather than by the job: it runs as
# a grandchild of the snap wrapper and can outlive it, and inside the snap it can
# only be signalled through the snap's own profile. It is always started with our
# .dol as an argument, which is what the match below relies on; ps truncates the
# command name, hence the prefix match.
for pid in $(ps -eo pid=,comm=,args= |
	awk -v dol="$dol" '$2 ~ /^dolphin-emu-nog/ && index($0, dol) { print $1 }'); do
	if [ -n "$snap_name" ]; then
		timeout 30 snap run --shell "$snap_name" -c "kill -9 $pid" >/dev/null 2>&1 || true
	else
		kill -9 "$pid" 2>/dev/null || true
	fi
done

# Do not wait on the wrapper indefinitely if it outlives the emulator.
for _ in $(seq 1 50); do
	kill -0 "$runner" 2>/dev/null || break
	sleep 0.2
done
kill -9 "$runner" 2>/dev/null || true

echo "Dolphin warnings: $(grep -ac 'Warning: Invalid ' "$log" || true)"
echo "--- ${dol##*/} ---"
grep -a OSREPORT "$log" | sed 's/\r$//' | cut -d: -f5- | sed 's/^ //' || true
# A jump to a NULL pointer is logged as an exception rather than as an invalid
# access, so it has to be looked for separately - otherwise a crash looks like a
# clean run in the output above.
crashes=$(grep -acE '(ISI|DSI) exception at ' "$log" || true)
if [ "$crashes" != 0 ]; then
	echo "--- crashed ($crashes exceptions) ---"
	grep -a -A2 'Exception (\(ISI\|DSI\)) occurred' "$log" | sed 's/\r$//' | head -20
fi
if [ "$dump_frames" = true ]; then
	echo "--- frames ---"
	echo "$user_dir/Dump/Frames"
fi
