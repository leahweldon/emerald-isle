#include "render/Shader.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace emerald {

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open shader file: " + path.string());
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// Replaces each line of the form #include "name" with that file from the same directory (one
// level, no guards), and inserts the defines after the #version line.
std::string preprocess(const std::filesystem::path& path, const std::string& defines)
{
    std::istringstream in(readFile(path));
    std::string out;
    std::string line;
    bool versionSeen = false;
    while (std::getline(in, line)) {
        const std::string directive = "#include \"";
        if (line.compare(0, directive.size(), directive) == 0) {
            const std::size_t end = line.find('"', directive.size());
            if (end == std::string::npos) {
                throw std::runtime_error("Malformed #include in " + path.string() + ": " + line);
            }
            out += readFile(path.parent_path() / line.substr(directive.size(), end - directive.size()));
            out += '\n';
            continue;
        }
        out += line;
        out += '\n';
        if (!versionSeen && line.compare(0, 8, "#version") == 0) {
            versionSeen = true;
            out += defines;
        }
    }
    return out;
}

GLuint compileStage(GLenum stage, const std::filesystem::path& path, const std::string& defines)
{
    const std::string source = preprocess(path, defines);
    const char* sourcePtr = source.c_str();

    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Failed to compile " + path.string() + ":\n" + log);
    }
    return shader;
}

} // namespace

Shader::Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath,
               const std::string& defines)
{
    const GLuint vertex = compileStage(GL_VERTEX_SHADER, vertexPath, defines);
    GLuint fragment = 0;
    try {
        fragment = compileStage(GL_FRAGMENT_SHADER, fragmentPath, defines);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
        glGetProgramInfoLog(m_program, length, nullptr, log.data());
        glDeleteProgram(m_program);
        throw std::runtime_error("Failed to link " + vertexPath.string() + " + " +
                                 fragmentPath.string() + ":\n" + log);
    }
}

Shader::~Shader()
{
    if (m_program != 0) {
        glDeleteProgram(m_program);
    }
}

Shader::Shader(Shader&& other) noexcept
    : m_program(std::exchange(other.m_program, 0))
    , m_locations(std::move(other.m_locations))
{
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other) {
        if (m_program != 0) {
            glDeleteProgram(m_program);
        }
        m_program = std::exchange(other.m_program, 0);
        m_locations = std::move(other.m_locations);
    }
    return *this;
}

void Shader::use() const
{
    glUseProgram(m_program);
}

GLint Shader::location(const std::string& name)
{
    const auto cached = m_locations.find(name);
    if (cached != m_locations.end()) {
        return cached->second;
    }
    const GLint loc = glGetUniformLocation(m_program, name.c_str());
    m_locations.emplace(name, loc);
    return loc;
}

void Shader::set(const std::string& name, int value) { glUniform1i(location(name), value); }
void Shader::set(const std::string& name, float value) { glUniform1f(location(name), value); }
void Shader::set(const std::string& name, const glm::vec2& value) { glUniform2fv(location(name), 1, glm::value_ptr(value)); }
void Shader::set(const std::string& name, const glm::vec3& value) { glUniform3fv(location(name), 1, glm::value_ptr(value)); }
void Shader::set(const std::string& name, const glm::vec4& value) { glUniform4fv(location(name), 1, glm::value_ptr(value)); }
void Shader::set(const std::string& name, const glm::mat3& value) { glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }
void Shader::set(const std::string& name, const glm::mat4& value) { glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }

} // namespace emerald
