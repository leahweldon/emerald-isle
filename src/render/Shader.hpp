#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace emerald {

// A linked GLSL program built from a vertex and fragment shader on disk.
// Compile and link errors are reported with the file name and the driver's log.
class Shader {
public:
    Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
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
    void set(const std::string& name, const glm::mat3& value);
    void set(const std::string& name, const glm::mat4& value);

    GLuint id() const { return m_program; }

    // Cached location, for callers that set the same uniform many times per frame.
    GLint uniformLocation(const std::string& name) { return location(name); }

private:
    GLint location(const std::string& name);

    GLuint m_program = 0;
    std::unordered_map<std::string, GLint> m_locations;
};

} // namespace emerald
