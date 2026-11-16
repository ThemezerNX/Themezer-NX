#!/usr/bin/env python3
"""Writes the notices of what the app is built with, from the one list ENTRIES below, so the two cannot drift apart:
romfs/licenses.txt, which the app's About screen shows, and THIRD_PARTY_NOTICES.md, which also covers the parts that
only live in the repository. The license texts are devkitPro's portlib license files, the sources, or written out below.

    python3 tools/make_licenses.py

Needs devkitPro (DEVKITPRO, default /opt/devkitpro) with the portlibs the app links.
"""
import os, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEVKITPRO = pathlib.Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
PORTLIBS = DEVKITPRO / "portlibs" / "switch" / "licenses"
OUT = ROOT / "themezer-nx" / "romfs" / "licenses.txt"
OUT_NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"

ISC = """Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE."""

MIT = """Permission is hereby granted, free of charge, to any person obtaining a copy
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
SOFTWARE."""

ZLIB = """This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution."""

CURL = """COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2020, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder."""

WEBP = """Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google nor the names of its contributors may
    be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."""

HARFBUZZ = """HarfBuzz is licensed under the so-called "Old MIT" license. Its copyright
holders are listed in the COPYING file of its source, among them Google, Inc.,
Red Hat, Inc., Mozilla Foundation, Behdad Esfahbod and many others.

Permission is hereby granted, without written agreement and without
license or royalty fees, to use, copy, modify, and distribute this
software and its documentation for any purpose, provided that the
above copyright notice and the following two paragraphs appear in
all copies of this software.

IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS."""


def portlib(name, *files):
    return "\n\n".join((PORTLIBS / name / f).read_text(errors="replace").strip() for f in files)


def cjson():
    header = (ROOT / "themezer-nx" / "source" / "libs" / "cJSON.h").read_text()
    return re.search(r"/\*(.*?)\*/", header, re.S).group(1).strip("\n").replace("\n  ", "\n").strip()


THUMBHASH = ("MIT License\n\nCopyright (c) 2023 Stefano Verna\n\n" + MIT
             + "\n\nThe reference implementation is available under the MIT License too:\n\n"
             "MIT License\n\nCopyright (c) 2023 Evan Wallace\n\n" + MIT)

INTRO = ("This repository is distributed under the GNU General Public License v3.0. Some files include or adapt "
         "code from third-party projects under compatible licenses.")


def entry(name, use, text="", prose="", files=()):
    """text and use go into both files; prose and files only into THIRD_PARTY_NOTICES.md. No text: only there."""
    return name, use, text, prose, files


ENTRIES = [
    entry("libnx", "Switch system services", "Copyright 2017-2018 libnx Authors\n\n" + ISC),
    entry("SDL2, SDL2_image, SDL2_ttf", "the screen, sound and input; pictures; text",
     "Simple DirectMedia Layer\nCopyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>\n\n" + ZLIB
     + "\n\n" + portlib("switch-sdl2_image", "COPYING.txt") + "\n\n" + portlib("switch-sdl2_ttf", "LICENSE.txt")),
    entry("FreeType", "fonts", "Portions of this software are copyright (C) The FreeType Project (www.freetype.org). "
     "All rights reserved. It is used under the FreeType License:\n\n" + portlib("switch-freetype", "FTL.TXT")),
    entry("HarfBuzz", "text shaping, for SDL2_ttf", HARFBUZZ),
    entry("libpng", "PNG pictures", portlib("switch-libpng", "LICENSE")),
    entry("libjpeg-turbo", "JPEG pictures", portlib("switch-libjpeg-turbo", "LICENSE.md")),
    entry("libwebp", "WebP pictures", WEBP),
    entry("zlib", "compression", portlib("switch-zlib", "LICENSE")),
    entry("bzip2", "compression, for FreeType", portlib("switch-bzip2", "LICENSE")),
    entry("curl", "downloads", CURL),
    entry("Mbed TLS", "HTTPS for curl", portlib("switch-mbedtls", "LICENSE")),
    entry("Mesa and libdrm_nouveau", "the Switch's GPU, for SDL2",
     "Copyright (C) 1999-2007 Brian Paul and the Mesa contributors; libdrm: Copyright the libdrm authors.\n"
     "Both are under the MIT license:\n\n" + MIT),
    entry("cJSON", "JSON", cjson()),
    entry("ThumbHash", "the blurred placeholders and backdrops (source/libs/thumbhash.c): the decoder adapted from "
     "datocms/fast_thumbhash, the encoder ported from evanw/thumbhash", THUMBHASH,
          "The ThumbHash decoder implementation in this file is adapted from https://github.com/datocms/fast_thumbhash\n\n"
          "Its encoder is ported from the reference implementation of ThumbHash, whose algorithm was created by "
          "Evan Wallace: https://github.com/evanw/thumbhash\n\n"
          "The adapted code from datocms/fast_thumbhash is available under the MIT License, and the reference "
          "implementation under the MIT License too:", ("themezer-nx/source/libs/thumbhash.c",)),
    entry("Lucide", "the icons (romfs/icons)",
     "ISC License\n\nCopyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as part of Feather (MIT). "
     "All other copyright (c) for Lucide are held by Lucide Contributors 2022.\n\n" + ISC
     + "\n\nThe parts that come from Feather:\n\nThe MIT License (MIT)\n\nCopyright (c) 2013-2023 Cole Bemis\n\n" + MIT,
          "The icons are Lucide's (https://lucide.dev), drawn through "
          "tools/make_icons.py. Lucide is available under the ISC License:", ("themezer-nx/romfs/icons/*.png",)),
    entry("C and C++ runtime", "newlib and libstdc++, from devkitA64",
     "The app is built with devkitA64. Its C library is newlib, whose parts are under BSD-style and similar "
     "permissive licenses (https://sourceware.org/newlib/COPYING.NEWLIB). Its C++ library, libstdc++, is under the "
     "GPL version 3 with the GCC Runtime Library Exception (https://www.gnu.org/licenses/gcc-exception-3.1.html)."),
    # Only in THIRD_PARTY_NOTICES.md: no license text of their own.
    entry("Themezer logo", "",
          prose="Themezer's logo is rendered from the Themezer monorepo's design files by tools/make_brand.mjs.",
          files=("themezer-nx/romfs/brand/*.png",)),
    entry("In the App", "",
          prose="The app's About page shows themezer-nx/romfs/licenses.txt: the notices of the libraries it's built "
          "with (libnx, SDL2, FreeType, HarfBuzz, libpng, libjpeg-turbo, libwebp, zlib, bzip2, curl, Mbed TLS, Mesa, "
          "cJSON and the above), made by tools/make_licenses.py from devkitPro's license files."),
]


def main():
    libs = [e for e in ENTRIES if e[2]]
    lines = [
        "Third-party notices",
        "",
        "Themezer-NX is free software under the GNU General Public License, version 3 (see LICENSE in its source). "
        "It is built with the libraries "
        "below, which keep their own licenses; their notices follow.",
        "",
    ]
    lines += [f"- {name}: {use}" for name, use, *_ in libs]
    for name, _, text, *_ in libs:
        lines += ["", "", f"== {name} ==", "", text]
    OUT.write_text("\n".join(lines) + "\n")
    print(f"{OUT}: {len(lines)} lines")

    md = ["# Third-Party Notices", INTRO]
    for name, use, text, prose, files in libs + [e for e in ENTRIES if not e[2]]:
        md.append(f"## {name}")
        if use:
            md.append(f"Used for: {use}.")
        if files:
            md.append("Files:\n\n" + "\n".join(f"- `{f}`" for f in files))
        if prose:
            md.append(prose)
        if text:
            md.append(f"```text\n{text}\n```")
    OUT_NOTICES.write_text("\n\n".join(md) + "\n")
    print(f"{OUT_NOTICES}: {len(md)} blocks")


if __name__ == "__main__":
    main()
