#!/bin/sh
# A clip of the game into clips/ (git-ignored): king.mp4 with the PC speaker, and king.gif, a
# six-second preview. Uses the original's pictures from original/: the clips are attached to a
# GitHub release, never committed. Needs ffmpeg and build/king.
#   tools/clips.sh [SECONDS] [SEED] [PREVIEW_START]
set -e
cd "$(dirname "$0")/.."
SECONDS_=${1:-45}
SEED=${2:-7}
AT=${3:-12}
mkdir -p clips
./build/king original --record clips "$SECONDS_" "$SEED"
ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 640x350 -r 30 -i clips/king.rgb \
    -f s16le -ar 44100 -ac 1 -i clips/king.s16 \
    -vf scale=960:720:flags=neighbor -c:v libx264 -pix_fmt yuv420p -crf 20 \
    -c:a aac -b:a 96k -shortest -movflags +faststart clips/king.mp4
ffmpeg -loglevel error -y -ss "$AT" -t 6 -f rawvideo -pix_fmt rgb24 -s 640x350 -r 30 -i clips/king.rgb \
    -vf "fps=15,scale=640:480:flags=neighbor,split[a][b];[a]palettegen=max_colors=32:stats_mode=full[p];[b][p]paletteuse=dither=none" \
    -loop 0 clips/king.gif
rm clips/king.rgb clips/king.s16
ls -la clips/king.mp4 clips/king.gif
