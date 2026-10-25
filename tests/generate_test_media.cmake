# Generates the clips the tests play and transcode, with the FFmpeg CLI of
# the FFmpeg build the project links. Run by the build:
#   cmake -DFFMPEG=<ffmpeg.exe> -DOUT_DIR=<dir> -P generate_test_media.cmake
# Every clip is synthetic (lavfi), small and deterministic.
cmake_minimum_required(VERSION 3.20)

if(NOT FFMPEG OR NOT OUT_DIR)
  message(FATAL_ERROR "usage: cmake -DFFMPEG=<ffmpeg> -DOUT_DIR=<dir> -P generate_test_media.cmake")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")

function(run_ffmpeg)
  execute_process(
    COMMAND "${FFMPEG}" -hide_banner -loglevel error -y ${ARGN}
    RESULT_VARIABLE rc
    ERROR_VARIABLE err
  )
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ffmpeg failed (${rc}): ${err}\narguments: ${ARGN}")
  endif()
endfunction()

# <out>: <seconds> of a moving test pattern plus a sine tone.
function(make_clip out seconds size rate tone sample_rate channels)
  run_ffmpeg(
    -f lavfi -i "testsrc2=size=${size}:rate=${rate}:duration=${seconds}"
    -f lavfi -i "sine=frequency=${tone}:sample_rate=${sample_rate}:duration=${seconds}"
    -c:v libx264 -preset veryfast -pix_fmt yuv420p -g 50 -bf 3
    -c:a aac -b:a 96k -ac ${channels}
    ${ARGN} "${out}"
  )
endfunction()

# <out>: a solid 0x3366CC frame encoded as BT.709 in the given range (tv|pc).
function(make_color_clip out range)
  run_ffmpeg(
    -f lavfi -i "color=c=0x3366CC:size=320x240:rate=25:duration=3"
    -vf "format=rgb24,scale=out_color_matrix=bt709:out_range=${range},format=yuv420p"
    -c:v libx264 -preset veryfast -crf 12
    -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range ${range}
    "${out}"
  )
endfunction()

function(concat out)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E cat ${ARGN} OUTPUT_FILE "${out}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "concatenating ${ARGN} failed")
  endif()
endfunction()

set(D "${OUT_DIR}")

# Main clip: H.264 with B-frames + 44.1 kHz AAC.
make_clip("${D}/h264_aac_25fps.mp4" 6 640x360 25 440 44100 2)
# Long enough to cancel a transcode half-way.
make_clip("${D}/long_30s.mp4" 30 640x360 25 440 44100 2)
# Frames that last 0.5 s.
make_clip("${D}/h264_aac_2fps.mp4" 6 320x240 2 440 44100 2)
# Transport stream whose timestamps start at 10 s.
make_clip("${D}/ts_offset10.ts" 4 640x360 25 440 44100 2 -output_ts_offset 10 -f mpegts)

# Two segments that both start at the same timestamps, back to back.
make_clip("${D}/seg_a.ts" 3 640x360 25 440 44100 2 -f mpegts)
make_clip("${D}/seg_b.ts" 3 640x360 25 880 44100 2 -f mpegts)
concat("${D}/ts_discont.ts" "${D}/seg_a.ts" "${D}/seg_b.ts")

# Continuous timestamps, but the second half is 320x240 / 48 kHz mono.
make_clip("${D}/seg_c.ts" 3 640x360 25 440 44100 2 -f mpegts)
make_clip("${D}/seg_d.ts" 3 320x240 25 660 48000 1 -output_ts_offset 3 -f mpegts)
concat("${D}/ts_format_change.ts" "${D}/seg_c.ts" "${D}/seg_d.ts")

make_color_clip("${D}/color_bt709_tv.mp4" tv)
make_color_clip("${D}/color_bt709_pc.mp4" pc)
# The same colour as JPEG-style full-range BT.601 (yuvj422p).
run_ffmpeg(
  -f lavfi -i "color=c=0x3366CC:size=320x240:rate=25:duration=3"
  -vf "format=rgb24,scale=out_color_matrix=bt601:out_range=pc,format=yuvj422p"
  -c:v mjpeg -q:v 2 "${D}/color_bt601_mjpeg.avi"
)

# Other codecs and containers: 4 s each, video codec / audio codec / extras.
function(make_codec_clip out vcodec acodec)
  run_ffmpeg(
    -f lavfi -i "testsrc2=size=640x360:rate=25:duration=4"
    -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=4"
    -c:v ${vcodec} -c:a ${acodec} ${ARGN} "${out}"
  )
endfunction()

make_codec_clip("${D}/hevc_main10.mkv" libx265 aac -pix_fmt yuv420p10le -preset ultrafast
                -x265-params log-level=error)
make_codec_clip("${D}/av1.mkv" libsvtav1 aac -pix_fmt yuv420p -preset 12)
make_codec_clip("${D}/vp9_opus.webm" libvpx-vp9 libopus -deadline realtime -cpu-used 8 -b:v 1M)
make_codec_clip("${D}/mpeg2_mp2.ts" mpeg2video mp2 -b:v 2M -f mpegts)
make_codec_clip("${D}/mjpeg_pcm.avi" mjpeg pcm_s16le -pix_fmt yuvj422p -q:v 4)

file(REMOVE "${D}/seg_a.ts" "${D}/seg_b.ts" "${D}/seg_c.ts" "${D}/seg_d.ts")
file(WRITE "${D}/media.stamp" "generated\n")
