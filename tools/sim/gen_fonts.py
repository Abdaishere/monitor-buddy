#!/usr/bin/env python3
"""Copies the U8g2 fonts src/main.cpp names out of U8g2's 39 MB u8g2_fonts.c.

usage: gen_fonts.py MAIN_CPP U8G2_FONTS_C OUT_H OUT_C
"""
import re
import sys

main_cpp, fonts_c, out_h, out_c = sys.argv[1:5]
names = sorted(set(re.findall(r"\bu8g2_font_[A-Za-z0-9_]+", open(main_cpp, encoding="utf-8").read())))
source = open(fonts_c, encoding="latin-1").read()
header, body = [], ['#include "U8g2lib.h"\n']
for name in names:
    match = re.search(r"const uint8_t %s\[(\d+)\] U8G2_FONT_SECTION\(\"%s\"\) =\s*\n(.*?);\n" % (name, name), source, re.S)
    if not match:
        sys.exit("gen_fonts: %s is not in %s" % (name, fonts_c))
    header.append("extern const uint8_t %s[%s];\n" % (name, match.group(1)))
    body.append("const uint8_t %s[%s] =\n%s;\n" % (name, match.group(1), match.group(2)))
open(out_h, "w", encoding="latin-1").write("".join(header))
open(out_c, "w", encoding="latin-1").write("".join(body))
print("gen_fonts: %d fonts" % len(names))
