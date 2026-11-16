#pragma once

#include <string>

namespace emerald {

// Reads the current default framebuffer and writes it to a PNG file.
// Used for the --screenshot option, which produces README images and CI artefacts.
bool saveFramebufferPng(int width, int height, const std::string& path);

} // namespace emerald
