#!/usr/bin/env bash
# ===========================================================================
#  AxisFlow -- one-command Linux installer (prebuilt tarball).
#
#      curl -fsSL https://raw.githubusercontent.com/LeonidWiebe/AxisFlow/main/install.sh | bash
#
#  Downloads the prebuilt workbench from GitHub Releases and drops it into your
#  FreeCAD Mod folder. AxisFlow is a COMPILED module (AxisFlow.so) tied to the
#  Python/Qt/Coin ABI of the FreeCAD it was built against -- this prebuilt
#  targets FreeCAD 1.1.x (x86_64). On any other FreeCAD the install is refused
#  (override with --force). Qt/Coin/Python are NOT installed; they come from
#  your FreeCAD.
#
#  Options:
#    --mod-dir=DIR    install into DIR instead of the auto-detected Mod folder
#    --freecad=PATH   path to freecadcmd (or a FreeCAD AppImage) for detection
#    --version=TAG    install a specific release tag (e.g. v0.1.3) not "latest"
#    --force          install even if the FreeCAD version can't be matched
#    --uninstall      remove an installed AxisFlow
#    -h, --help       show this help
#
#  Safe pattern (review before running):
#    curl -fsSLO https://raw.githubusercontent.com/LeonidWiebe/AxisFlow/main/install.sh
#    less install.sh && bash install.sh
# ===========================================================================
set -euo pipefail

REPO="${AXISFLOW_REPO:-LeonidWiebe/AxisFlow}"
TARGET_MM="1.1"                 # ABI target: FreeCAD major.minor
ARCH="$(uname -m)"
APPNAME="AxisFlow"

MOD_DIR=""
FREECAD_BIN=""
REL_TAG=""
FORCE=0
DO_UNINSTALL=0

msg()  { printf '\033[1m%s\033[0m\n' "$*"; }
info() { printf '  %s\n' "$*"; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

usage() { sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

# ---- args -----------------------------------------------------------------
for a in "$@"; do
  case "$a" in
    --mod-dir=*)  MOD_DIR="${a#*=}" ;;
    --freecad=*)  FREECAD_BIN="${a#*=}" ;;
    --version=*)  REL_TAG="${a#*=}" ;;
    --force)      FORCE=1 ;;
    --uninstall)  DO_UNINSTALL=1 ;;
    -h|--help)    usage 0 ;;
    *) die "unknown option: $a (see --help)" ;;
  esac
done

# ---- prerequisites --------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1 || die "'$1' is required but not found."; }
need tar
need curl
SHA_TOOL=""
if command -v sha256sum >/dev/null 2>&1; then SHA_TOOL="sha256sum"
elif command -v shasum   >/dev/null 2>&1; then SHA_TOOL="shasum -a 256"
fi

# ---- locate a FreeCAD we can query ----------------------------------------
# Returns the command (as an array) that runs a headless FreeCAD script, or
# nothing if none was found.
find_freecad_cmd() {
  if [ -n "$FREECAD_BIN" ]; then
    case "$FREECAD_BIN" in
      *.AppImage|*.appimage) printf '%s\ncmd\n' "$FREECAD_BIN" ;;  # AppImage subcommand
      *)                     printf '%s\n' "$FREECAD_BIN" ;;
    esac
    return
  fi
  local c
  for c in freecadcmd FreeCADCmd freecad-cmd; do
    if command -v "$c" >/dev/null 2>&1; then printf '%s\n' "$c"; return; fi
  done
}

# Probe installed FreeCAD for version + user data dir. Sets FC_VER / FC_DATADIR.
FC_VER=""
FC_DATADIR=""
probe_freecad() {
  mapfile -t fc < <(find_freecad_cmd)
  [ "${#fc[@]}" -gt 0 ] || return 1

  local probe out
  probe="$(mktemp --suffix=.py)"
  cat >"$probe" <<'PY'
import FreeCAD
v = FreeCAD.Version()
print("AXF_VERSION=%s.%s.%s" % (v[0], v[1], str(v[2]).split()[0] or "0"))
print("AXF_DATADIR=%s" % FreeCAD.getUserAppDataDir())
PY
  # freecadcmd prints a banner; we only read our AXF_ markers.
  out="$("${fc[@]}" "$probe" 2>/dev/null || true)"
  rm -f "$probe"

  FC_VER="$(printf '%s\n' "$out"     | sed -n 's/^AXF_VERSION=//p' | head -1)"
  FC_DATADIR="$(printf '%s\n' "$out" | sed -n 's/^AXF_DATADIR=//p' | head -1)"
  [ -n "$FC_VER" ] || return 1
}

# ---- resolve Mod dir ------------------------------------------------------
resolve_mod_dir() {
  if [ -n "$MOD_DIR" ]; then return; fi
  if [ -n "$FC_DATADIR" ]; then
    MOD_DIR="${FC_DATADIR%/}/Mod"
    return
  fi
  # Fallback to the XDG default used by most native/AppImage installs.
  MOD_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/FreeCAD/Mod"
}

# ---- uninstall ------------------------------------------------------------
if [ "$DO_UNINSTALL" -eq 1 ]; then
  probe_freecad || true
  resolve_mod_dir
  target="${MOD_DIR%/}/${APPNAME}"
  if [ -e "$target/${APPNAME}.so" ] || [ -e "$target/package.xml" ]; then
    rm -rf "$target"
    msg "Removed $target"
    info "Restart FreeCAD to unload the workbench."
  else
    die "no AxisFlow install found at $target (use --mod-dir=... if it's elsewhere)."
  fi
  exit 0
fi

# ---- ABI / version gate ---------------------------------------------------
msg "AxisFlow installer (prebuilt, FreeCAD ${TARGET_MM}.x, ${ARCH})"

if [ "$ARCH" != "x86_64" ]; then
  [ "$FORCE" -eq 1 ] || die "no prebuilt for '${ARCH}'. Only x86_64 is published; build from source, or use --force to try anyway."
  warn "architecture is '${ARCH}', not x86_64 -- continuing because --force was given."
fi

if probe_freecad; then
  info "Detected FreeCAD ${FC_VER}"
  fc_mm="${FC_VER%.*}"
  if [ "$fc_mm" != "$TARGET_MM" ]; then
    if [ "$FORCE" -eq 1 ]; then
      warn "FreeCAD ${FC_VER} != target ${TARGET_MM}.x -- continuing because --force was given (it may fail to load)."
    else
      die "this prebuilt is for FreeCAD ${TARGET_MM}.x, but you have ${FC_VER}.
       A compiled workbench does not load across FreeCAD versions.
       Options: install a release built for ${FC_VER}, build from source,
       or re-run with --force if you know the ABI matches."
    fi
  elif [ "$FC_VER" != "${TARGET_MM}.1" ]; then
    info "Note: built against ${TARGET_MM}.1; ${FC_VER} shares the ${TARGET_MM} ABI, proceeding."
  fi
else
  if [ "$FORCE" -eq 1 ]; then
    warn "could not determine your FreeCAD version -- continuing because --force was given."
  else
    die "could not run FreeCAD to check its version.
       Point --freecad=/path/to/freecadcmd (or a FreeCAD AppImage), or, if you
       are sure it is FreeCAD ${TARGET_MM}.x, re-run with --force and optionally
       --mod-dir=/path/to/FreeCAD/Mod."
  fi
fi

resolve_mod_dir
info "Mod folder: ${MOD_DIR}"

# ---- download -------------------------------------------------------------
ASSET="${APPNAME}-${TARGET_MM/./_}-linux-${ARCH}-FC${TARGET_MM}.tar.gz"
# example: AxisFlow-1_1-linux-x86_64-FC1.1.tar.gz

if [ -z "$REL_TAG" ]; then
  # ask the API for the latest tag (no jq: tag_name appears once, early)
  api="https://api.github.com/repos/${REPO}/releases/latest"
  REL_TAG="$(curl -fsSL "$api" 2>/dev/null | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)"
  [ -n "$REL_TAG" ] || die "could not resolve the latest release tag from GitHub (network?). Pin one with --version=vX.Y.Z."
fi
info "Release: ${REL_TAG}"

base="https://github.com/${REPO}/releases/download/${REL_TAG}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

msg "Downloading ${ASSET}"
curl -fSL --progress-bar -o "$tmp/$ASSET" "$base/$ASSET" \
  || die "download failed: $base/$ASSET
       (no Linux/${ARCH} asset for ${REL_TAG}? check the Releases page.)"

# ---- verify checksum (if published) ---------------------------------------
if [ -n "$SHA_TOOL" ] && curl -fsSL -o "$tmp/SHA256SUMS.txt" "$base/SHA256SUMS.txt" 2>/dev/null; then
  want="$(grep -E "  ${ASSET}\$| ${ASSET}\$" "$tmp/SHA256SUMS.txt" | awk '{print $1}' | head -1)"
  if [ -n "$want" ]; then
    got="$(cd "$tmp" && $SHA_TOOL "$ASSET" | awk '{print $1}')"
    [ "$want" = "$got" ] || die "checksum mismatch for ${ASSET} (expected ${want}, got ${got}). Aborting."
    info "Checksum OK"
  else
    warn "SHA256SUMS.txt has no entry for ${ASSET}; skipping verification."
  fi
else
  [ -n "$SHA_TOOL" ] && warn "no SHA256SUMS.txt published; skipping checksum verification."
fi

# ---- install ---------------------------------------------------------------
# The tarball contains a top-level "AxisFlow/" directory.
target="${MOD_DIR%/}/${APPNAME}"
mkdir -p "$MOD_DIR"

# Replace any previous copy (guarded: only remove a dir that looks like ours).
if [ -e "$target" ]; then
  if [ -e "$target/${APPNAME}.so" ] || [ -e "$target/package.xml" ]; then
    rm -rf "$target"
  else
    die "$target exists and does not look like an AxisFlow install; refusing to overwrite."
  fi
fi

tar -xzf "$tmp/$ASSET" -C "$MOD_DIR"
[ -e "$target/${APPNAME}.so" ] || warn "installed, but ${APPNAME}.so not found in $target -- the tarball layout may be off."

msg "Installed AxisFlow ${REL_TAG} -> ${target}"
info "Restart FreeCAD, then pick 'AxisFlow' from the workbench selector."
info "Uninstall later with:  bash install.sh --uninstall"
