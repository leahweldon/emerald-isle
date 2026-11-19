#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace emerald {

// A linked GLSL program built from a vertex and fragment shader on disk.
// Compile and link errors are reported with the file name and the driver's log.
// Sources may #include "file" from their own directory; defines (e.g. "#define X\n") are inserted
// after the #version line of both stages.
class Shader {
public:
    Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath,
           const std::string& defines = "");
    // Vertex-only program for transform feedback: the named outputs are captured, interleaved
    // in this order. Draw it with GL_RASTERIZER_DISCARD enabled.
    Shader(const std::filesystem::path& vertexPath, const std::vector<std::string>& feedbackVaryings,
           const std::string& defines = "");
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void use() const;

    void set(const std::string& name, int value);
    void set(const std::string& name, float value);
    void set(const std::string& name, const glm::vec2& value);
    void set(const std::string& name, const glm::vec3& value);
    void set(const std::string& name, const glm::vec4& value);
    void set(const std::string& name, const glm::mat3& value);
    void set(const std::string& name, const glm::mat4& value);

    GLuint id() const { return m_program; }

    // Cached location, for callers that set the same uniform many times per frame.
    GLint uniformLocation(const std::string& name) { return location(name); }

private:
    GLint location(const std::string& name);
    void checkLink(const std::string& name); // throws with the driver's log on failure

    GLuint m_program = 0;
    std::unordered_map<std::string, GLint> m_locations;
};

} // namespace emerald
