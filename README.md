# ffmpeg-player
A minimal video player using ffmpeg and sdl2

This project is my attempt to understand some of the details of how audio/video works in the modern software stack. In particular,
I focus on ffmpeg, which is a dependency of Firefox, Chrome, and VLC, among others.

Like many who attempt to write an ffmpeg video player, I was inspired by the [tutorial series](http://dranger.com/ffmpeg/) by Dranger
on this topic. However, ffmpeg has changed significantly since that tutorial was last updated. SDL has also had a major version update,
so the GUI code is in need of an update too.

Another good resource on this is the [tutorial](https://github.com/leandromoreira/ffmpeg-libav-tutorial) by Leandro Moreira. This is
up to date at the time of this writing, has lots of really good explanation, and is highly recommended. However, this tutorial doesn't
focus on the media player part. It shows you how to decode media with ffmpeg, and moves on to other applications of ffmpeg, like transcoding
and transmuxing.

This is where I decide to write a media player based on ffmpeg from scratch. Documentation on ffmpeg comes from auto-generated doxygen
pages. However, the header files still provide enough explanation and hints that one could get going writing a media player from it.

## Trying it out
To try it out yourself, clone the repository, and run `make`. To make a release build, run `make BUILD=release`. At this point, you can
play a media file by running `./player <filename>`. As of now, the following keys are recognized during playback-
* `q`: quit player
* `space`: pause/play
* `m`: mute
* `f`: toggle fullscreen
* `9`: decrease volume 5%
* `0`: increase volume 5%
* `left arrow`: seek backward 10 seconds
* `right arrow`: seek forward 10 seconds
* `down arrow`: seek backward 1 minute
* `up arrow`: seek forward 1 minute
* `page down`: seek backward 10 minutes
* `page up`: seek forward 10 minutes

## Future plans
In addition to tying some loose ends (like rendering subtitles and such), I also plan to write a step by step account of how the program
works, both as a reference for myself in future, and in the hope that it might be useful to someone else who is about to undertake the same
journey.
