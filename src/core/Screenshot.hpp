#pragma once

#include <string>

namespace emerald {

// Reads a framebuffer (0: the default back buffer) and writes it to a PNG file.
// Used for the --screenshot option, which produces README images and CI artefacts.
bool saveFramebufferPng(unsigned framebuffer, int width, int height, const std::string& path);

} // namespace emerald
