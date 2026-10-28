# Fixed system-shell worker. Source paths and executable input are never accepted.
set -efu
set -o pipefail
umask 077
export PATH=/usr/bin:/bin:/usr/sbin:/sbin LC_ALL=C HOME=/var/empty OPENSSL_CONF=/dev/null
TRUSTED_UID=0
TRUSTED_GID=0
LIVE='/Library/Application Support/Avid/Audio/Plug-Ins'
BASE='/Library/Application Support/Kirin/Hypha/acceptance-swap'
IPC_BASE='/Library/Application Support/Kirin/Hypha/acceptance-swap-ipc'
caller=$1 parent=$2 token=$3 payload_hash=$4 payload_size=$5 wire_size=$6
case "$caller:$parent:$payload_size:$wire_size" in *[!0-9:]*|'') exit 64;; esac
case "$token" in *[!a-f0-9]*|'') exit 64;; esac
case "$payload_hash" in *[!a-f0-9]*|'') exit 64;; esac
[ "${#token}" -eq 32 ] && [ "${#payload_hash}" -eq 64 ] || exit 64
[ "$caller" -gt 0 ] && [ "$parent" -gt 1 ] && [ "$payload_size" -gt 0 ] && [ "$payload_size" -le 805306368 ] && [ "$wire_size" -gt 0 ] && [ "$wire_size" -le 805306368 ] || exit 64
[ "$(/usr/bin/id -u)" = "$TRUSTED_UID" ] || exit 64
session="$BASE/$token" ipc="$IPC_BASE/$token"
old_pre= old_post= new_pre= new_post= originals_checked=0 restored=0 success=0
fail() { printf '%s\n' "$*" >&2; return 1; }
digest() { value=$(/usr/bin/openssl dgst -sha256) || return 1; printf '%s\n' "${value##* }"; }
no_acl() {
  listing=$(/bin/ls -lde "$1") || return 1
  # ls may show @ instead of + when both attributes and an ACL are present.
  # With one literal path and -d, every additional output line is an ACL record.
  case "$listing" in *'
'*) fail 'extended ACL refused';; esac
}
secure_object() {
  [ ! -L "$1" ] && [ -e "$1" ] || return 1
  object_uid=$(/usr/bin/stat -f %u "$1") || return 1
  [ "$object_uid" = "$TRUSTED_UID" ] || return 1
  mode=$(/usr/bin/stat -f %Lp "$1") || return 1
  [ "$((0$mode & 07022))" -eq 0 ] || return 1
  no_acl "$1"
}
secure_dir() { [ -d "$1" ] && secure_object "$1"; }
ensure_dir() {
  if [ ! -e "$1" ] && [ ! -L "$1" ]; then
    /bin/mkdir -m "$2" "$1"
    /usr/sbin/chown "$TRUSTED_UID:$TRUSTED_GID" "$1"
  fi
  secure_dir "$1"
}
system_guard() {
  /usr/bin/csrutil status | /usr/bin/grep -Fx 'System Integrity Protection status: enabled.' >/dev/null
  /usr/bin/csrutil authenticated-root status | /usr/bin/grep -Fx 'Authenticated Root status: enabled' >/dev/null
  /sbin/mount | /usr/bin/grep -E '^/dev/[^ ]+ on / \(apfs, sealed, .*read-only[,)]' >/dev/null
  system_device=$(/usr/bin/stat -f %d /) || return 1
  for tool in /bin/rmdir /bin/sh /bin/mv /bin/mkdir /bin/rm /bin/chmod /bin/ls /bin/sync /usr/bin/env /usr/bin/id /usr/bin/ditto /usr/bin/find /usr/bin/stat /usr/bin/openssl /usr/bin/base64 /usr/bin/xattr /usr/bin/sort /usr/bin/tr /usr/bin/mkfifo /usr/bin/csrutil /usr/bin/grep /usr/sbin/chown /sbin/mount; do
    [ -f "$tool" ] && [ ! -L "$tool" ] || return 1
    tool_identity=$(/usr/bin/stat -f '%u:%d' "$tool") || return 1
    [ "$tool_identity" = "0:$system_device" ] || return 1
  done
}
# Canonical tree records: C byte order; type, octal mode, relative name, byte hash;
# then attribute name/value hashes for that object. Hidden entries are included.
tree_hash() (
  trap - EXIT HUP INT TERM
  tree=$1
  secure_dir "$tree" || exit 1
  tree_device=$(/usr/bin/stat -f %d "$tree") || exit 1
  [ "$tree_device" = "$volume_device" ] || exit 1
  /usr/bin/find -x "$tree" -exec /bin/sh -c '
    cr=$(printf "\r"); tab=$(printf "\t")
    for item do case "$item" in *"$cr"*|*"$tab"*|*"
"*) exit 1;; esac; done
  ' -- {} + || exit 1
  /usr/bin/find -x "$tree" -print | /usr/bin/sort | while IFS= read -r item; do
    secure_object "$item" || exit 1
    item_device=$(/usr/bin/stat -f %d "$item") || exit 1
    [ "$item_device" = "$volume_device" ] || exit 1
    rel=${item#"$tree"}; rel=${rel#/}; [ -n "$rel" ] || rel=.
    mode=$(/usr/bin/stat -f %Lp "$item") || exit 1
    if [ -d "$item" ]; then
      printf 'D\t%s\t%s\t-\n' "$mode" "$rel"
    elif [ -f "$item" ]; then
      link_count=$(/usr/bin/stat -f %l "$item") || exit 1
      [ "$link_count" = 1 ] || exit 1
      hash=$(digest < "$item") || exit 1
      printf 'F\t%s\t%s\t%s\n' "$mode" "$rel" "$hash"
    else exit 1; fi
    attributes=$(/usr/bin/xattr "$item") || exit 1
    if [ -n "$attributes" ]; then
      printf '%s\n' "$attributes" | /usr/bin/sort | while IFS= read -r attribute; do
        case "$attribute" in com.apple.provenance|com.apple.FinderInfo|com.apple.ResourceFork|com.apple.quarantine|com.example.fixture) ;; *) exit 1;; esac
        hash=$(/usr/bin/xattr -px "$attribute" "$item" | /usr/bin/tr -d ' \n' | digest) || exit 1
        printf 'X\t%s\t%s\t%s\n' "$rel" "$attribute" "$hash"
      done || exit 1
    fi
  done | digest
)
# A digest printed by a failed scan is never a valid comparison value.
tree_matches() {
  actual=$(tree_hash "$1") || return 1
  [ "$actual" = "$2" ]
}
journal() { printf '%s\n' "$*" >> "$session/journal" || return 1; /bin/sync || return 1; }
status() { printf '{"status":"%s"}\n' "$1" >> "$ipc/status"; }
move_bundle() {
  [ ! -e "$2" ] && [ ! -L "$2" ] || return 1
  source_device=$(/usr/bin/stat -f %d "$1") || return 1
  destination_device=$(/usr/bin/stat -f %d "${2%/*}") || return 1
  [ "$source_device" = "$volume_device" ] && [ "$destination_device" = "$volume_device" ] || return 1
  /bin/mv -n "$1" "$2" || return 1
  [ ! -e "$1" ] && [ -d "$2" ] && [ ! -L "$2" ]
}
restore_pair() {
  # Validate BOTH backups before changing either installed entry.
  for role in PRE POST; do
    case "$role" in PRE) old=$old_pre new=$new_pre;; POST) old=$old_post new=$new_post;; esac
    target="$LIVE/Kirin Hypha $role.aaxplugin" backup="$session/original-$role"
    if [ -e "$backup" ] || [ -L "$backup" ]; then
      tree_matches "$backup" "$old" || return 1
      if [ -e "$target" ] || [ -L "$target" ]; then tree_matches "$target" "$new" || return 1; fi
    else tree_matches "$target" "$old" || return 1; fi
  done
  for role in PRE POST; do
    target="$LIVE/Kirin Hypha $role.aaxplugin" backup="$session/original-$role"
    if [ -d "$backup" ]; then
      journal "restore-intent $role" || return 1
      if [ -e "$target" ]; then move_bundle "$target" "$session/returned-$role" || return 1; fi
      move_bundle "$backup" "$target" || return 1
      journal "restored $role" || return 1
    fi
  done
  tree_matches "$LIVE/Kirin Hypha PRE.aaxplugin" "$old_pre" || return 1
  tree_matches "$LIVE/Kirin Hypha POST.aaxplugin" "$old_post" || return 1
  journal restored-verified || return 1
  restored=1
}
finish() {
  code=$?
  trap - EXIT HUP INT TERM
  if [ "$success" = 1 ]; then
    /bin/rm -rf "$session"
    /bin/rm -rf "$ipc"
    /bin/rmdir "$BASE/active"
  else
    if [ "$originals_checked" = 1 ] && [ "$restored" = 0 ]; then
      if restore_pair; then journal rollback-verified; else journal recovery-needed; fi
    fi
    journal "stopped $code"
    status error || :
    printf 'Recovery retained: %s\n' "$session" >&2
    [ "$code" -ne 0 ] || code=1
  fi
  exit "$code"
}
control() {
  deadline=$((SECONDS+$1))
  while [ "$SECONDS" -lt "$deadline" ]; do
    command=
    if IFS= read -r -t 2 -n 32 command <&3; then return 0; fi
    [ -z "$command" ] && kill -0 "$parent" 2>/dev/null || return 1
  done
  return 1
}
read_line() { IFS= read -r "$1" <&5; }
materialize() {
  role=$1
  read_line label && [ "$label" = "$role" ] || return 1
  read_line expected || return 1
  case "$expected" in *[!a-f0-9]*|'') return 1;; esac
  [ "${#expected}" -eq 64 ] || return 1
  case "$role" in PRE) new_pre=$expected;; POST) new_post=$expected;; esac
  inbound="$session/inbound-$role"
  /bin/mkdir -m 700 "$inbound"
  while read_line kind; do
    [ "$kind" != END ] || break
    case "$kind" in D|F|X) ;; *) return 1;; esac
    read_line rel || return 1
    case "$rel" in ''|/*|..|../*|*/../*|*/..|./*|*/./*|*/.|*//* ) return 1;; esac
    cr=$(printf '\r'); tab=$(printf '\t')
    case "$rel" in *"$cr"*|*"$tab"*) return 1;; esac
    file="$inbound/$rel"
    if [ "$kind" = X ]; then
      [ -e "$file" ] && [ ! -L "$file" ] || return 1
      read_line attribute && read_line hex || return 1
      case "$hex" in *[!a-fA-F0-9]*) return 1;; esac
      case "$attribute" in
        com.apple.provenance)
          actual=$(/usr/bin/xattr -px "$attribute" "$file" | /usr/bin/tr -d ' \n') || return 1
          [ "$actual" = "$hex" ] || return 1;;
        com.apple.FinderInfo|com.apple.ResourceFork|com.apple.quarantine|com.example.fixture)
          /usr/bin/xattr -wx "$attribute" "$hex" "$file";;
        *) return 1;;
      esac
      continue
    fi
    read_line mode || return 1
    case "$mode" in [0-7][0-7][0-7]) ;; *) return 1;; esac
    [ "$((0$mode & 022))" -eq 0 ] || return 1
    if [ "$kind" = D ]; then
      [ "$rel" = . ] || /bin/mkdir -m 700 "$file"
      [ "$((0$mode & 0500))" -eq 320 ] || return 1
      # Delayed modes keep directories writable during unprivileged fixtures.
      printf '%s\n%s\n' "$rel" "$mode" >> "$session/modes-$role"
    else
      [ "$rel" != . ] && [ ! -e "$file" ] && [ "$((0$mode & 0400))" -ne 0 ] || return 1
      read_line bytes || return 1
      printf '%s' "$bytes" | /usr/bin/base64 -D > "$file"
      /bin/chmod "$mode" "$file"
    fi
  done
  [ "$kind" = END ] || return 1
  while IFS= read -r rel && IFS= read -r mode; do /bin/chmod "$mode" "$inbound/$rel"; done < "$session/modes-$role"
  tree_matches "$inbound" "$expected" || return 1
  /usr/bin/ditto --rsrc --extattr --acl "$inbound" "$session/candidate-$role"
  tree_matches "$session/candidate-$role" "$expected" || return 1
}
system_guard
secure_dir /
secure_dir /Library
secure_dir '/Library/Application Support'
secure_dir '/Library/Application Support/Avid'
secure_dir '/Library/Application Support/Avid/Audio'
secure_dir "$LIVE"
ensure_dir '/Library/Application Support/Kirin' 755
ensure_dir '/Library/Application Support/Kirin/Hypha' 755
ensure_dir "$BASE" 700
base_identity=$(/usr/bin/stat -f '%u:%g:%Lp' "$BASE") || exit 1
[ "$base_identity" = "$TRUSTED_UID:$TRUSTED_GID:700" ]
ensure_dir "$IPC_BASE" 711
ipc_mode=$(/usr/bin/stat -f %Lp "$IPC_BASE") || exit 1
[ "$ipc_mode" = 711 ]
volume_device=$(/usr/bin/stat -f %d "$LIVE") || exit 1
base_device=$(/usr/bin/stat -f %d "$BASE") || exit 1
[ "$volume_device" = "$base_device" ]
/bin/mkdir -m 700 "$BASE/active"
/bin/mkdir -m 700 "$session"
/usr/sbin/chown "$TRUSTED_UID:$TRUSTED_GID" "$session"
trap finish EXIT
trap 'exit 130' HUP INT TERM
journal prepared
/bin/mkdir -m 711 "$ipc"
/usr/sbin/chown "$TRUSTED_UID:$TRUSTED_GID" "$ipc"
: > "$ipc/status"
/bin/chmod 644 "$ipc/status"
/usr/bin/mkfifo -m 600 "$ipc/request"
/usr/sbin/chown "$caller" "$ipc/request"
# RDWR avoids blocking the shell's open before the client connects.
exec 3<> "$ipc/request"
# The system shell reads at most 4096 ASCII bytes at a time. No background
# process is needed; parent loss and time limits are checked between reads.
received=0 deadline=$((SECONDS+600))
: > "$session/payload64"
while [ "$received" -lt "$wire_size" ]; do
  [ "$SECONDS" -lt "$deadline" ] && kill -0 "$parent" 2>/dev/null || exit 66
  count=$((wire_size-received)); [ "$count" -le 4096 ] || count=4096
  chunk=
  if IFS= read -r -t 2 -n "$count" chunk <&3; then :; else
    [ "$SECONDS" -lt "$deadline" ] && kill -0 "$parent" 2>/dev/null || exit 66
  fi
  case "$chunk" in *[!A-Za-z0-9+/=]*) exit 65;; esac
  printf '%s' "$chunk" >> "$session/payload64"
  received=$((received+${#chunk}))
done
/usr/bin/base64 -D < "$session/payload64" > "$session/payload"
payload_bytes=$(/usr/bin/stat -f %z "$session/payload") || exit 65
[ "$payload_bytes" = "$payload_size" ]
payload_digest=$(digest < "$session/payload") || exit 65
[ "$payload_digest" = "$payload_hash" ]
exec 5< "$session/payload"
read_line magic && [ "$magic" = HYPHA-AAX-SWAP-2 ] || exit 65
read_line old_pre && read_line old_post || exit 65
case "$old_pre$old_post" in *[!a-f0-9]*|'') exit 65;; esac
[ "${#old_pre}" -eq 64 ] && [ "${#old_post}" -eq 64 ] || exit 65
materialize PRE
materialize POST
if IFS= read -r extra <&5; then exit 65; fi
tree_matches "$LIVE/Kirin Hypha PRE.aaxplugin" "$old_pre" || exit 1
tree_matches "$LIVE/Kirin Hypha POST.aaxplugin" "$old_post" || exit 1
kill -0 "$parent" 2>/dev/null
originals_checked=1
journal originals-verified
for role in PRE POST; do
  journal "backup-intent $role"
  move_bundle "$LIVE/Kirin Hypha $role.aaxplugin" "$session/original-$role"
  journal "backup-complete $role"
  journal "install-intent $role"
  move_bundle "$session/candidate-$role" "$LIVE/Kirin Hypha $role.aaxplugin"
  journal "install-complete $role"
done
journal installed
status installed
control 43200 && [ "$command" = restore ] || exit 66
restore_pair
status restored
control 120 && [ "$command" = ack ] || exit 66
success=1
