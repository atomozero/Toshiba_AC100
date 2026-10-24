#!/bin/bash
# Copia un file dell'host in un'immagine disco contenente una partizione BFS.
# Uso: bfsput.sh <immagine> <offset partizione in byte> <file host> <percorso nel BFS>
set -e
G=$HOME/haiku-arm/haiku/generated.arm
T=$G/objects/linux/x86_64/release/tools
export LD_LIBRARY_PATH=$G/objects/linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
BFS_SHELL=$T/bfs_shell/bfs_shell
FS_CMD=$T/fs_shell/fs_shell_command

fifo=/tmp/bfsput-$$-fifo
mkfifo $fifo-to $fifo-from
sleep 3<$fifo-from 1 &
exec 6>$fifo-from 3<$fifo-from
sleep 5<$fifo-to 1 &
exec 4>$fifo-to 5<$fifo-to
rm -f $fifo-to $fifo-from

$BFS_SHELL 3>&5 4<&6 --start-offset "$2" -n "$1" &
SHELL_PID=$!
$FS_CMD 3<&3 4>&4 5>&- 6>&- cp -f ":$3" "/myfs/$4"
$FS_CMD 3<&3 4>&4 5>&- 6>&- sync || true
$FS_CMD 3<&3 4>&4 5>&- 6>&- quit || true
wait $SHELL_PID 2>/dev/null || true
