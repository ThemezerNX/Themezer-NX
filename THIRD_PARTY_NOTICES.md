Third-Party Notices

This repository is distributed under the GNU General Public License v3.0. Some
files include or adapt code from third-party projects under compatible licenses.

ThumbHash Decoder

File:
themezer-nx/source/thumbhash.c

The ThumbHash decoder implementation in this file is adapted from:
https://github.com/datocms/fast_thumbhash

The underlying ThumbHash algorithm was created by Evan Wallace:
https://github.com/evanw/thumbhash

The adapted code from datocms/fast_thumbhash is available under the MIT License:

MIT License

Copyright (c) 2023 Stefano Verna

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

UI Sounds and Music

Files:
themezer-nx/romfs/sfx/*.wav
themezer-nx/romfs/bgm.wav

The UI sounds are synthesized by tools/sfx/make_sfx.py from Themezer-3DS
(GPL-3.0-or-later, Copyright (C) 2026 Themezer-3DS contributors) and resampled
to 48 kHz. The background music is the loop of "Aether" (made with Suno) that
Themezer-3DS plays, cut by its tools/music/make_loop.py. tools/make_audio.sh
regenerates both from a Themezer-3DS checkout.
