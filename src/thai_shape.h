// Simple Thai mark positioning for glyph-by-glyph renderers.
// Swaps tone marks / upper vowels for the font's private-use variants
// (U+F700-F71A) so marks don't collide with upper vowels or tall consonants.
#pragma once
#include <string>

std::string thaiShape(const std::string &utf8);
