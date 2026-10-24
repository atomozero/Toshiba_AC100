#!/bin/bash
# Copia un file da una partizione BFS (immagine raw) all'host.
# Uso: bfsget.sh <partizione.bfs> <percorso nel BFS, es. system/var/log/syslog> <file host>
set -e
G=$HOME/haiku-arm/haiku/generated.arm
T=$G/objects/linux/x86_64/release/tools
export LD_LIBRARY_PATH=$G/objects/linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
BFS_SHELL=$T/bfs_shell/bfs_shell
FS_CMD=$T/fs_shell/fs_shell_command

fifo=/tmp/bfsget-$$-fifo
mkfifo $fifo-to $fifo-from
sleep 3<$fifo-from 1 &
exec 6>$fifo-from 3<$fifo-from
sleep 5<$fifo-to 1 &
exec 4>$fifo-to 5<$fifo-to
rm -f $fifo-to $fifo-from

$BFS_SHELL 3>&5 4<&6 -n "$1" &
SHELL_PID=$!
$FS_CMD 3<&3 4>&4 5>&- 6>&- cp -f "/myfs/$2" ":$3"
$FS_CMD 3<&3 4>&4 5>&- 6>&- quit || true
wait $SHELL_PID 2>/dev/null || true
