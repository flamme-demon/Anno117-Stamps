#!/usr/bin/env bash
# Install the extracted release on Linux/Proton. Requires Bash and coreutils.
set -Eeuo pipefail

mod_id=anno117-stamps-native-ui
package_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
game_dir='' prefix='' documents='' profile_name=''
dry_run=0
usage() {
    cat <<'HELP'
Usage: bash install-linux.sh --game GAME_DIR --prefix PROTON_PFX [options]
  --game DIR       Game folder containing Bin/Win64/dbghelp.dll
  --prefix DIR     Proton prefix containing drive_c (usually compatdata/ID/pfx)
  --documents DIR  Override the game's Documents/Anno 117 - Pax Romana folder
  --profile NAME   Select a mod profile; otherwise keep the active profile
  --dry-run        Show destinations without changing the installation
Close Anno first. No sudo is needed when the game belongs to your user.
HELP
}
fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
while (($#)); do
    case $1 in
        --game|--prefix|--documents|--profile)
            (($# >= 2)) && [[ -n $2 ]] || fail "Missing value for $1"
            case $1 in
                --game) game_dir=$2;; --prefix) prefix=$2;;
                --documents) documents=$2;; --profile) profile_name=$2;;
            esac
            shift 2;;
        --dry-run) dry_run=1; shift;;
        --help|-h) usage; exit 0;;
        *) fail "Unknown option: $1";;
    esac
done
[[ -n $game_dir && -n $prefix ]] || { usage; exit 1; }
[[ -d $game_dir && -d $prefix/drive_c ]] || fail 'Check --game and --prefix.'
game_dir=$(cd -- "$game_dir" && pwd -P)
drive_c=$(cd -- "$prefix/drive_c" && pwd -P)
documents=${documents:-$drive_c/users/steamuser/Documents/Anno 117 - Pax Romana}
[[ $documents == /* ]] || documents=$PWD/$documents
bin=$game_dir/Bin/Win64
current_dll=$bin/dbghelp.dll
original_dll=$bin/dbghelp_orig.dll
source_dll=$package_dir/dbghelp.dll
[[ -f $source_dll ]] || source_dll=$package_dir/build/native-stamps-ui/dbghelp.dll
mod_source=$package_dir/mods/$mod_id
runtime=$drive_c/anno117-stamps/native-ui
selector=$documents/mods/profile.txt
[[ -f $source_dll && -f $mod_source/modinfo.json && -f $mod_source/scripts/native-stamps-data.lua &&
   -f $mod_source/data/base/config/export/assets.xml && -f $mod_source/native-ui/icons.tsv &&
   -f $mod_source/native-ui/catalogue.tsv ]] || fail 'Incomplete package. Extract the entire release ZIP.'
[[ -f $current_dll ]] || fail "Game DLL not found: $current_dll"
for dll in "$source_dll" "$current_dll"; do
    [[ $(head -c 2 -- "$dll") == MZ ]] || fail "Invalid Windows DLL: $dll"
done
if [[ -e $original_dll ]]; then
    [[ -f $original_dll && $(head -c 2 -- "$original_dll") == MZ ]] || fail 'Invalid dbghelp_orig.dll.'
    ! LC_ALL=C grep -aFq 'dbghelp_orig' "$original_dll" || fail 'dbghelp_orig.dll is a proxy, not the original game DLL.'
else
    ! LC_ALL=C grep -aFq 'dbghelp_orig' "$current_dll" || fail 'Proxy already installed but dbghelp_orig.dll is missing. Restore the original game DLL first.'
fi
if command -v pgrep >/dev/null && { pgrep -x 'Anno117.exe' >/dev/null || pgrep -x 'Anno117_plus.ex' >/dev/null; }; then
    fail 'Close Anno before installing.'
fi
if [[ -z $profile_name && -f $selector ]]; then
    profile_name=$(awk '/^[ \t]*ActiveProfile[ \t]+/ {sub(/^[ \t]*ActiveProfile[ \t]+/, ""); sub(/[\r \t]+$/, ""); print; exit}' "$selector")
fi
profile_name=${profile_name:-default}
[[ $profile_name != . && $profile_name != .. && $profile_name != *[/:\\\<\>\"\|\?\*]* &&
   $profile_name != *[$'\t\r\n']* && $profile_name != *' ' && $profile_name != *. ]] || fail 'Invalid profile name.'
profile_file=$documents/mods/profiles/$profile_name.txt

stage=$(mktemp -d -t anno117-stamps-install.XXXXXXXX)
trap 'rm -rf -- "$stage"' EXIT
# The game accepts UTF-8 profiles with LF. Preserve other mods and comments.
profile_input=/dev/null
[[ ! -f $profile_file ]] || profile_input=$profile_file
awk -v id="$mod_id" '
    {line=$0; sub(/\r$/, "", line); if (NR==1) sub(/^\357\273\277/, "", line)
     if (line ~ "^[ \\t]*#?[ \\t]*" id "([ \\t]+#.*)?[ \\t]*$") {
         if (!found++) print id
     } else print line}
    END {if (!found) print id}
' "$profile_input" > "$stage/profile.txt"
selector_input=/dev/null
[[ ! -f $selector ]] || selector_input=$selector
awk -v name="$profile_name" '
    {line=$0; sub(/\r$/, "", line); if (NR==1) sub(/^\357\273\277/, "", line)
     if (line ~ "^[ \\t]*ActiveProfile([ \\t]|$)") {
         if (!found++) print "ActiveProfile " name
     } else print line}
    END {if (!found) print "ActiveProfile " name}
' "$selector_input" > "$stage/selector.txt"

sources=() targets=() existed=()
add_file() {
    [[ ! -d $2 ]] || fail "A directory occupies a destination file: $2"
    sources+=("$1"); targets+=("$2")
}
[[ -f $original_dll ]] || add_file "$current_dll" "$original_dll"
while IFS= read -r -d '' source; do
    relative=${source#"$mod_source"/}
    add_file "$source" "$documents/mods/$mod_id/$relative"
done < <(find "$mod_source" -type f -print0 | sort -z)
add_file "$mod_source/native-ui/icons.tsv" "$runtime/icons.tsv"
[[ -f $runtime/catalogue.tsv ]] || add_file "$mod_source/native-ui/catalogue.tsv" "$runtime/catalogue.tsv"
add_file "$stage/profile.txt" "$profile_file"
add_file "$stage/selector.txt" "$selector"
add_file "$source_dll" "$current_dll"
printf 'Game: %s\nDocuments: %s\nRuntime: %s\nProfile: %s\n' "$game_dir" "$documents" "$runtime" "$profile_name"
if ((dry_run)); then
    printf 'Would install: %s\n' "${targets[@]}"
    printf 'Dry run complete. No installation files changed.\n'
    exit 0
fi

backup=$documents/stamps-mod-backups/$(date -u +%Y%m%dT%H%M%SZ)-$$
mkdir -p -- "$backup/files"
printf 'Backup file\tDestination\tPreviously present\n' > "$backup/files.tsv"
for i in "${!targets[@]}"; do
    if [[ -f ${targets[i]} ]]; then
        cp -p -- "${targets[i]}" "$backup/files/$i.before"
        existed+=(1)
    else existed+=(0); fi
    printf 'files/%s.before\t%s\t%s\n' "$i" "${targets[i]}" "${existed[i]}" >> "$backup/files.tsv"
done
attempted=0
temporary=
write_file() {
    local parent
    parent=$(dirname -- "$2")
    mkdir -p -- "$parent"
    temporary=$(mktemp -- "$parent/.anno117-stamps.XXXXXXXX.tmp")
    cp -p -- "$1" "$temporary"
    mv -f -- "$temporary" "$2"
    temporary=
}
rollback() {
    local status=$? i
    ((status != 0)) || status=1
    trap - ERR INT TERM
    set +e
    [[ -z $temporary ]] || rm -f -- "$temporary"
    for ((i=attempted-1; i>=0; i--)); do
        if ((existed[i])); then
            write_file "$backup/files/$i.before" "${targets[i]}" || printf 'Restore manually: %s\n' "${targets[i]}" >&2
        else rm -f -- "${targets[i]}"; fi
    done
    printf 'Installation failed. Rollback attempted; backup: %s\n' "$backup" >&2
    exit "${status:-1}"
}
trap rollback ERR INT TERM
for i in "${!targets[@]}"; do
    attempted=$((i+1))
    write_file "${sources[i]}" "${targets[i]}"
done
mkdir -p -- "$documents/stamps"
trap - ERR INT TERM
printf 'Installed. Backup: %s\n' "$backup"
printf 'Steam launch option (merge with existing overrides if needed):\nWINEDLLOVERRIDES="dbghelp=n,b" %%command%%\n'
