Rebuild tools for the Win95 HDD-music shim. Not copied to the 95 machine.

build.cmd
  Open Watcom build of ..\wincd.dll (PE 4.0) and re-stamps quake2.exe's
  import name to wincd.dll if needed.

prepare_pcm_tracks.py
  Converts music\Track02.ogg-Track21.ogg to 22050 Hz 16-bit stereo WAVs.
  The install ships the WAVs; the OGGs are not kept.

Requires Open Watcom (default C:\WATCOM). ffmpeg is only needed to
recreate the WAVs from OGGs.
