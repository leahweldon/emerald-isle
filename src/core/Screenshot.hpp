#pragma once

#include <string>

namespace emerald {

// Reads a framebuffer (0: the default back buffer) and writes it to a PNG file.
// Used for the --screenshot option, which produces README images and CI artefacts.
// report prints the path once saved.
bool saveFramebufferPng(unsigned framebuffer, int width, int height, const std::string& path, bool report = true);

} // namespace emerald
