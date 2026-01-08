#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

// Use the JUCE OpenGL namespace
using namespace juce::gl;

namespace
{
    struct ParticleCPU
    {
        float pos[4];
        float vel[4];
        float color[4];
    };

    // Returns the OpenGL compiler info log for a shader object (used for compile errors/warnings).
    static juce::String getInfoLogForShader (GLuint shader)
    {
        GLint length = 0;
        glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &length);

        if (length <= 1)
            return {};

        juce::HeapBlock<GLchar> buffer;
        buffer.calloc ((size_t) length);
        glGetShaderInfoLog (shader, length, nullptr, buffer.getData());
        return juce::String::fromUTF8 (buffer.getData());
    }

    // Returns the OpenGL linker info log for a program object (used for link errors/warnings).
    static juce::String getInfoLogForProgram (GLuint program)
    {
        GLint length = 0;
        glGetProgramiv (program, GL_INFO_LOG_LENGTH, &length);

        if (length <= 1)
            return {};

        juce::HeapBlock<GLchar> buffer;
        buffer.calloc ((size_t) length);
        glGetProgramInfoLog (program, length, nullptr, buffer.getData());
        return juce::String::fromUTF8 (buffer.getData());
    }

    // Compiles a GLSL shader stage from source, attaches it to 'program', and returns a readable error log on failure.
    static bool compileAndAttachShader (GLuint program, GLenum shaderType, const juce::String& source, juce::String& outError)
    {
        GLuint shader = glCreateShader (shaderType);
        if (shader == 0)
        {
            outError = "glCreateShader failed";
            return false;
        }

        auto utf8 = source.toRawUTF8();
        glShaderSource (shader, 1, &utf8, nullptr);
        glCompileShader (shader);

        GLint status = 0;
        glGetShaderiv (shader, GL_COMPILE_STATUS, &status);

        if (status == GL_FALSE)
        {
            outError = getInfoLogForShader (shader);
            if (outError.isEmpty())
                outError = "Shader compile failed (no log)";
            glDeleteShader (shader);
            return false;
        }

        glAttachShader (program, shader);
        glDeleteShader (shader); // program keeps it after attach
        return true;
    }

    // Links a GLSL program and returns a readable error log on failure.
    static bool linkProgram (GLuint program, juce::String& outError)
    {
        glLinkProgram (program);

        GLint status = 0;
        glGetProgramiv (program, GL_LINK_STATUS, &status);

        if (status == GL_FALSE)
        {
            outError = getInfoLogForProgram (program);
            if (outError.isEmpty())
                outError = "Program link failed (no log)";
            return false;
        }

        return true;
    }

    // Sets an int uniform only if it exists in the linked program (allows optional uniforms).
    static void setUniform1iIfPresent (GLuint program, const char* name, int v)
    {
        auto loc = glGetUniformLocation (program, name);
        if (loc >= 0)
            glUniform1i (loc, v);
    }

    // Sets a float uniform only if it exists in the linked program (allows optional uniforms).
    static void setUniform1fIfPresent (GLuint program, const char* name, float v)
    {
        auto loc = glGetUniformLocation (program, name);
        if (loc >= 0)
            glUniform1f (loc, v);
    }

    // Sets a vec3 uniform only if it exists in the linked program (allows optional uniforms).
    static void setUniform3fIfPresent (GLuint program, const char* name, juce::Vector3D<float> v)
    {
        auto loc = glGetUniformLocation (program, name);
        if (loc >= 0)
            glUniform3f (loc, v.x, v.y, v.z);
    }

    // Sets an ivec3 uniform only if it exists in the linked program (allows optional uniforms).
    static void setUniform3iIfPresent (GLuint program, const char* name, juce::Vector3D<int> v)
    {
        auto loc = glGetUniformLocation (program, name);
        if (loc >= 0)
            glUniform3i (loc, v.x, v.y, v.z);
    }

    // Sets a mat4 uniform only if it exists in the linked program (allows optional uniforms).
    static void setUniformMatrix4IfPresent (GLuint program, const char* name, const juce::Matrix3D<float>& m)
    {
        auto loc = glGetUniformLocation (program, name);
        if (loc >= 0)
            glUniformMatrix4fv (loc, 1, GL_FALSE, m.mat);
    }
}

//==============================================================================
// JUCE component constructor: configures the OpenGL context requirements, creates the overlay UI, and starts the shader hot-reload timer.
MainComponent::MainComponent()
{
    setSize(1024, 768);

    ensureGL43CoreContext();

    controlPanel = std::make_unique<BoidsControlPanel>();

    // Single source of truth for initial simulation settings: BoidsControlPanel::Params defaults.
    // We clamp/normalize once here so both the simulation and the UI start from the same values.
    {
        BoidsControlPanel::Params p;

        const int newCount = juce::jlimit (1, 100000, p.particleCount);

        currentParticleCount = newCount;
        requestedParticleCount.store (newCount);

        neighborRadius = juce::jlimit (0.05f, 50.0f, p.neighborRadius);
        separationRadius = juce::jlimit (0.01f, neighborRadius, p.separationRadius);
        weightSeparation = juce::jlimit (0.0f, 50.0f, p.weightSeparation);
        weightAlignment  = juce::jlimit (0.0f, 50.0f, p.weightAlignment);
        weightCohesion   = juce::jlimit (0.0f, 50.0f, p.weightCohesion);
        minSpeed = juce::jlimit (0.0f, 1000.0f, p.minSpeed);
        maxSpeed = juce::jlimit (juce::jmax (minSpeed + 1.0e-3f, 0.01f), 1000.0f, p.maxSpeed);
        maxAccel = juce::jlimit (0.0f, 10000.0f, p.maxAccel);
        simSpeed = juce::jlimit (0.1f, 2.0f, p.simSpeed);
        centerAttraction = juce::jlimit (0.0f, 1000.0f, p.centerAttraction);
        boundaryMargin = juce::jlimit (0.01f, 1000.0f, p.boundaryMargin);
        boundaryStrength = juce::jlimit (0.0f, 10000.0f, p.boundaryStrength);
        wrapBounds = p.wrapBounds;
        pointSize = juce::jlimit (1.0f, 64.0f, p.pointSize);
        alphaMul = juce::jlimit (0.0f, 1.0f, p.alphaMul);
        particleShape = juce::jlimit (0, 3, p.particleShape);

        colorMode = juce::jlimit (0, 3, p.colorMode);
        hueOffset = juce::jlimit (0.0f, 1.0f, p.hueOffset);
        hueRange = juce::jlimit (0.0f, 1.0f, p.hueRange);
        saturation = juce::jlimit (0.0f, 1.0f, p.saturation);
        value = juce::jlimit (0.0f, 1.0f, p.value);
        densityCurve = juce::jlimit (0.1f, 8.0f, p.densityCurve);

        // Ensure the UI matches the clamped/normalized values.
        p.particleCount = currentParticleCount;
        p.neighborRadius = neighborRadius;
        p.separationRadius = separationRadius;
        p.weightSeparation = weightSeparation;
        p.weightAlignment = weightAlignment;
        p.weightCohesion = weightCohesion;
        p.minSpeed = minSpeed;
        p.maxSpeed = maxSpeed;
        p.maxAccel = maxAccel;
        p.simSpeed = simSpeed;
        p.centerAttraction = centerAttraction;
        p.boundaryMargin = boundaryMargin;
        p.boundaryStrength = boundaryStrength;
        p.wrapBounds = wrapBounds;
        p.pointSize = pointSize;
        p.alphaMul = alphaMul;
        p.particleShape = particleShape;
        p.colorMode = colorMode;
        p.hueOffset = hueOffset;
        p.hueRange = hueRange;
        p.saturation = saturation;
        p.value = value;
        p.densityCurve = densityCurve;

        controlPanel->setParams (p);
    }

    controlPanel->setOnParamsChanged ([this] (BoidsControlPanel::Params p)
    {
        openGLContext.executeOnGLThread ([this, p] (juce::OpenGLContext&)
        {
            // Update CPU-side sim params on the GL thread (render also runs on the GL thread)
            const int newCount = juce::jlimit (1, 100000, p.particleCount);

            const bool neighborRadiusChanged = std::abs (p.neighborRadius - neighborRadius) > 1.0e-4f;

            neighborRadius = juce::jlimit (0.05f, 50.0f, p.neighborRadius);
            separationRadius = juce::jlimit (0.01f, neighborRadius, p.separationRadius);
            weightSeparation = juce::jlimit (0.0f, 50.0f, p.weightSeparation);
            weightAlignment  = juce::jlimit (0.0f, 50.0f, p.weightAlignment);
            weightCohesion   = juce::jlimit (0.0f, 50.0f, p.weightCohesion);
            minSpeed = juce::jlimit (0.0f, 1000.0f, p.minSpeed);
            maxSpeed = juce::jlimit (juce::jmax (minSpeed + 1.0e-3f, 0.01f), 1000.0f, p.maxSpeed);
            maxAccel = juce::jlimit (0.0f, 10000.0f, p.maxAccel);
            simSpeed = juce::jlimit (0.1f, 2.0f, p.simSpeed);
            centerAttraction = juce::jlimit (0.0f, 1000.0f, p.centerAttraction);
            boundaryMargin = juce::jlimit (0.01f, 1000.0f, p.boundaryMargin);
            boundaryStrength = juce::jlimit (0.0f, 10000.0f, p.boundaryStrength);
            wrapBounds = p.wrapBounds;
            pointSize = juce::jlimit (1.0f, 64.0f, p.pointSize);
            alphaMul = juce::jlimit (0.0f, 1.0f, p.alphaMul);
            particleShape = juce::jlimit (0, 3, p.particleShape);

            colorMode = juce::jlimit (0, 3, p.colorMode);
            hueOffset = juce::jlimit (0.0f, 1.0f, p.hueOffset);
            hueRange = juce::jlimit (0.0f, 1.0f, p.hueRange);
            saturation = juce::jlimit (0.0f, 1.0f, p.saturation);
            value = juce::jlimit (0.0f, 1.0f, p.value);
            densityCurve = juce::jlimit (0.1f, 8.0f, p.densityCurve);

            if (newCount != currentParticleCount || neighborRadiusChanged)
                rebuildBuffersOnGLThread (newCount);
        }, true);
    });

    controlPanel->setOnFullscreenChanged ([this] (bool shouldBeFullscreen)
    {
        // NOTE: On Windows JUCE's peer "fullscreen" maps to SW_SHOWMAXIMIZED (i.e. maximise).
        // For a true borderless fullscreen app (hide taskbar, etc), use kiosk mode.
        if (auto* w = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        {
            if (shouldBeFullscreen)
                juce::Desktop::getInstance().setKioskModeComponent (w, /*allowMenusAndBars*/ false);
            else
                juce::Desktop::getInstance().setKioskModeComponent (nullptr);
        }
    });
    addAndMakeVisible (*controlPanel);

    // Ensure the panel is laid out immediately (some hosts won't call resized() until later)
    resized();

    // Record start time
    startTime = static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.001);
    lastFrameTimeSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    fpsUpdateStartSeconds = lastFrameTimeSeconds;

    // Start timer for checking shader file changes (check every 500ms)
    startTimer (500);
}

// Destructor: stops timers and lets JUCE tear down the OpenGL context (calls shutdown()).
MainComponent::~MainComponent()
{
    stopTimer();
    shutdownOpenGL();
}

//==============================================================================
// Finds the runtime `Shaders/` directory (next to the executable, or in a few parent directories, or CWD as fallback).
juce::File MainComponent::getShadersDirectory() const
{
    // Look for Shaders folder relative to the executable
    auto executableFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    
    // Try multiple possible locations
    juce::Array<juce::File> possiblePaths = {
        executableFile.getParentDirectory().getChildFile("Shaders"),
        executableFile.getParentDirectory().getParentDirectory().getChildFile("Shaders"),
        executableFile.getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("Shaders"),
        executableFile.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("Shaders"),
        juce::File::getCurrentWorkingDirectory().getChildFile("Shaders")
    };
    
    for (const auto& path : possiblePaths)
    {
        if (path.isDirectory())
            return path;
    }
    
    // Fallback: return the first option even if it doesn't exist
    return possiblePaths[0];
}

// Configures JUCE's OpenGL context to request an OpenGL 4.3 core context (compute shaders) and attaches it to this component.
void MainComponent::ensureGL43CoreContext()
{
    // OpenGLAppComponent attaches immediately in its base constructor, but the version preference
    // must be set BEFORE attaching. So: detach, set required version, reattach.
    openGLContext.detach();
    openGLContext.setOpenGLVersionRequired (juce::OpenGLContext::openGL4_3);
    openGLContext.setContinuousRepainting (true);
    openGLContext.setComponentPaintingEnabled (true); // needed for our overlay UI + error text
    openGLContext.attachTo (*this);
}

// Validates that the current OpenGL context supports the required features; writes a user-visible error message on failure.
bool MainComponent::checkGLCapabilitiesOnGLThread()
{
    GLint major = 0, minor = 0;
    glGetIntegerv (GL_MAJOR_VERSION, &major);
    glGetIntegerv (GL_MINOR_VERSION, &minor);

    const bool versionOk = (major > 4) || (major == 4 && minor >= 3);
    if (!versionOk)
    {
        lastShaderError = "OpenGL 4.3+ is required for compute shaders. Detected OpenGL "
                          + juce::String (major) + "." + juce::String (minor);
        return false;
    }

    if (! openGLContext.areShadersAvailable())
    {
        lastShaderError = "Shaders are not available in this OpenGL context";
        return false;
    }

    return true;
}

// Loads, compiles, and links a compute shader program from a file path; used by reloadAllShadersOnGLThread().
bool MainComponent::compileComputeProgramFromFile (juce::File file, unsigned int& outProgram, juce::String& outError)
{
    if (! file.existsAsFile())
    {
        outError = "Compute shader file not found: " + file.getFullPathName();
        return false;
    }

    auto src = file.loadFileAsString();
    if (src.isEmpty())
    {
        outError = "Compute shader file is empty: " + file.getFullPathName();
        return false;
    }

    GLuint program = glCreateProgram();
    if (program == 0)
    {
        outError = "glCreateProgram failed";
        return false;
    }

    if (! compileAndAttachShader (program, GL_COMPUTE_SHADER, src, outError))
    {
        glDeleteProgram (program);
        return false;
    }

    if (! linkProgram (program, outError))
    {
        glDeleteProgram (program);
        return false;
    }

    outProgram = program;
    return true;
}

// Loads, compiles, and links a render program from vertex+fragment shader files; used by reloadAllShadersOnGLThread().
bool MainComponent::compileRenderProgramFromFiles (juce::File vertexFile, juce::File fragmentFile, unsigned int& outProgram, juce::String& outError)
{
    if (! vertexFile.existsAsFile())
    {
        outError = "Vertex shader file not found: " + vertexFile.getFullPathName();
        return false;
    }

    if (! fragmentFile.existsAsFile())
    {
        outError = "Fragment shader file not found: " + fragmentFile.getFullPathName();
        return false;
    }

    auto vs = vertexFile.loadFileAsString();
    auto fs = fragmentFile.loadFileAsString();

    if (vs.isEmpty())
    {
        outError = "Vertex shader file is empty: " + vertexFile.getFullPathName();
        return false;
    }

    if (fs.isEmpty())
    {
        outError = "Fragment shader file is empty: " + fragmentFile.getFullPathName();
        return false;
    }

    GLuint program = glCreateProgram();
    if (program == 0)
    {
        outError = "glCreateProgram failed";
        return false;
    }

    if (! compileAndAttachShader (program, GL_VERTEX_SHADER, vs, outError))
    {
        glDeleteProgram (program);
        return false;
    }

    if (! compileAndAttachShader (program, GL_FRAGMENT_SHADER, fs, outError))
    {
        glDeleteProgram (program);
        return false;
    }

    if (! linkProgram (program, outError))
    {
        glDeleteProgram (program);
        return false;
    }

    outProgram = program;
    return true;
}

// Deletes all compiled/linked GL programs owned by MainComponent.
void MainComponent::deletePrograms()
{
    if (computeClearProgram != 0) { glDeleteProgram (computeClearProgram); computeClearProgram = 0; }
    if (computeBuildProgram != 0) { glDeleteProgram (computeBuildProgram); computeBuildProgram = 0; }
    if (computeStepProgram  != 0) { glDeleteProgram (computeStepProgram);  computeStepProgram  = 0; }
    if (renderProgram       != 0) { glDeleteProgram (renderProgram);       renderProgram       = 0; }
}

//==============================================================================
// Periodic file watcher: checks shader file modification times and triggers a full shader reload if any changed.
void MainComponent::timerCallback()
{
    if (! computeClearFile.existsAsFile()
        || ! computeBuildFile.existsAsFile()
        || ! computeStepFile.existsAsFile()
        || ! renderVertexFile.existsAsFile()
        || ! renderFragmentFile.existsAsFile())
        return;

    const auto clearMod = computeClearFile.getLastModificationTime();
    const auto buildMod = computeBuildFile.getLastModificationTime();
    const auto stepMod  = computeStepFile.getLastModificationTime();
    const auto rvMod    = renderVertexFile.getLastModificationTime();
    const auto rfMod    = renderFragmentFile.getLastModificationTime();

    const bool changed =
        (clearMod > lastClearMod)
        || (buildMod > lastBuildMod)
        || (stepMod > lastStepMod)
        || (rvMod > lastRenderVertMod)
        || (rfMod > lastRenderFragMod);

    if (! changed)
        return;

    DBG ("Shader file change detected, reloading...");

    openGLContext.executeOnGLThread ([this] (juce::OpenGLContext&)
    {
        reloadAllShadersOnGLThread();
    }, true);
}

//==============================================================================
// OpenGLAppComponent init: checks capabilities, resolves shader file paths, creates required GL objects (VAO), compiles shaders, and builds SSBOs.
void MainComponent::initialise()
{
    jassert (juce::OpenGLHelpers::isContextActive());

    computeAvailable = checkGLCapabilitiesOnGLThread();

    auto shadersDir = getShadersDirectory();
    computeClearFile   = shadersDir.getChildFile ("boids_clear.comp");
    computeBuildFile   = shadersDir.getChildFile ("boids_build.comp");
    computeStepFile    = shadersDir.getChildFile ("boids_step.comp");
    renderVertexFile   = shadersDir.getChildFile ("particles.vert");
    renderFragmentFile = shadersDir.getChildFile ("particles.frag");

    // Create a VAO (required in core profile even if we don't use vertex attribs)
    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    glBindVertexArray (0);

    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable (GL_PROGRAM_POINT_SIZE);

    reloadAllShadersOnGLThread();

    if (computeAvailable)
        rebuildBuffersOnGLThread (currentParticleCount);
}

// OpenGLAppComponent shutdown: releases GL programs, buffers, and VAO; resets state flags used by render()/paint().
void MainComponent::shutdown()
{
    deletePrograms();
    deleteBuffers();

    if (vao != 0)
    {
        glDeleteVertexArrays (1, &vao);
        vao = 0;
    }

    shadersLoaded = false;
    computeAvailable = false;
    lastShaderError.clear();
}

// Compiles all compute + render shader programs and swaps them in atomically (delete old, install new); updates file modification timestamps.
bool MainComponent::reloadAllShadersOnGLThread()
{
    deletePrograms();

    if (! computeAvailable)
    {
        shadersLoaded = false;
        return false;
    }

    juce::String error;

    unsigned int newClear = 0, newBuild = 0, newStep = 0, newRender = 0;

    if (! compileComputeProgramFromFile (computeClearFile, newClear, error))
    {
        lastShaderError = "boids_clear.comp:\n" + error;
        shadersLoaded = false;
        return false;
    }

    if (! compileComputeProgramFromFile (computeBuildFile, newBuild, error))
    {
        glDeleteProgram (newClear);
        lastShaderError = "boids_build.comp:\n" + error;
        shadersLoaded = false;
        return false;
    }

    if (! compileComputeProgramFromFile (computeStepFile, newStep, error))
    {
        glDeleteProgram (newClear);
        glDeleteProgram (newBuild);
        lastShaderError = "boids_step.comp:\n" + error;
        shadersLoaded = false;
        return false;
    }

    if (! compileRenderProgramFromFiles (renderVertexFile, renderFragmentFile, newRender, error))
    {
        glDeleteProgram (newClear);
        glDeleteProgram (newBuild);
        glDeleteProgram (newStep);
        lastShaderError = "particles.vert/particles.frag:\n" + error;
        shadersLoaded = false;
        return false;
    }

    computeClearProgram = newClear;
    computeBuildProgram = newBuild;
    computeStepProgram  = newStep;
    renderProgram       = newRender;

    lastClearMod      = computeClearFile.getLastModificationTime();
    lastBuildMod      = computeBuildFile.getLastModificationTime();
    lastStepMod       = computeStepFile.getLastModificationTime();
    lastRenderVertMod = renderVertexFile.getLastModificationTime();
    lastRenderFragMod = renderFragmentFile.getLastModificationTime();

    shadersLoaded = true;
    lastShaderError.clear();
    return true;
}

// Deletes all simulation-related GPU buffers (SSBOs) and marks buffers as not ready for compute/render.
void MainComponent::deleteBuffers()
{
    if (particlesSSBO[0] != 0) { glDeleteBuffers (1, &particlesSSBO[0]); particlesSSBO[0] = 0; }
    if (particlesSSBO[1] != 0) { glDeleteBuffers (1, &particlesSSBO[1]); particlesSSBO[1] = 0; }
    if (cellHeadsSSBO != 0)    { glDeleteBuffers (1, &cellHeadsSSBO);    cellHeadsSSBO = 0; }
    if (nextIndexSSBO != 0)    { glDeleteBuffers (1, &nextIndexSSBO);    nextIndexSSBO = 0; }
    buffersReady.store (false);
}

// Allocates/reallocates SSBOs for the requested particle count and current grid parameters; initializes particle data and grid buffers.
void MainComponent::rebuildBuffersOnGLThread (int newParticleCount)
{
    jassert (juce::OpenGLHelpers::isContextActive());

    deleteBuffers();

    currentParticleCount = juce::jlimit (1, 100000, newParticleCount);

    // Grid derives from world size and cell size.
    //
    // Performance note:
    // If cellSize becomes very small (e.g. tiny neighborRadius), cellCount can explode and the per-frame clear/build
    // passes become the dominant cost. Clamp total cells to a reasonable upper bound by increasing cellSize as needed.
    cellSize = juce::jmax (0.5f, neighborRadius);
    const auto worldSize = worldMax - worldMin;

    auto computeGridFromCellSize = [this, worldSize]
    {
        gridDims =
        {
            juce::jmax (1, (int) std::ceil (worldSize.x / cellSize)),
            juce::jmax (1, (int) std::ceil (worldSize.y / cellSize)),
            juce::jmax (1, (int) std::ceil (worldSize.z / cellSize))
        };

        cellCount = gridDims.x * gridDims.y * gridDims.z;
    };

    computeGridFromCellSize();

    if (maxCellCount > 0 && cellCount > maxCellCount)
    {
        // Increase cellSize until we're under budget. Usually 1 iteration, but ceil() can overshoot slightly.
        for (int iter = 0; iter < 4 && cellCount > maxCellCount; ++iter)
        {
            const float scale = std::cbrt ((float) cellCount / (float) maxCellCount);
            cellSize *= (scale * 1.001f);
            computeGridFromCellSize();
        }
    }

    // Create SSBOs
    glGenBuffers (2, particlesSSBO);
    glGenBuffers (1, &cellHeadsSSBO);
    glGenBuffers (1, &nextIndexSSBO);

    // Init particle data
    std::vector<ParticleCPU> particles;
    particles.resize ((size_t) currentParticleCount);

    std::mt19937 rng ((uint32_t) juce::Time::getMillisecondCounter());
    std::uniform_real_distribution<float> rx (worldMin.x, worldMax.x);
    std::uniform_real_distribution<float> ry (worldMin.y, worldMax.y);
    std::uniform_real_distribution<float> rz (worldMin.z, worldMax.z);
    std::uniform_real_distribution<float> ru (-1.0f, 1.0f);
    std::uniform_real_distribution<float> rs (minSpeed, juce::jmax(minSpeed, maxSpeed/2.0f));

    for (auto& p : particles)
    {
        p.pos[0] = rx (rng);
        p.pos[1] = ry (rng);
        p.pos[2] = rz (rng);
        p.pos[3] = 1.0f;

        juce::Vector3D<float> dir { ru (rng), ru (rng), ru (rng) };
        if (dir.length() < 1.0e-3f)
            dir = juce::Vector3D<float> (1.0f, 0.0f, 0.0f);
        dir = dir.normalised();

        const float speed = rs (rng);
        const auto vel = dir * speed;

        p.vel[0] = vel.x;
        p.vel[1] = vel.y;
        p.vel[2] = vel.z;
        p.vel[3] = 0.0f;

        const auto heading = vel.normalised();
        const float t = juce::jlimit (0.0f, 1.0f, (speed - minSpeed) / (maxSpeed - minSpeed));
        p.color[0] = 0.2f + 0.8f * std::abs (heading.x);
        p.color[1] = 0.2f + 0.8f * std::abs (heading.y);
        p.color[2] = 0.2f + 0.8f * std::abs (heading.z);
        p.color[3] = 0.35f + 0.65f * t;
    }

    glBindBuffer (GL_SHADER_STORAGE_BUFFER, particlesSSBO[0]);
    glBufferData (GL_SHADER_STORAGE_BUFFER, (GLsizeiptr) (particles.size() * sizeof (ParticleCPU)), particles.data(), GL_DYNAMIC_DRAW);

    glBindBuffer (GL_SHADER_STORAGE_BUFFER, particlesSSBO[1]);
    glBufferData (GL_SHADER_STORAGE_BUFFER, (GLsizeiptr) (particles.size() * sizeof (ParticleCPU)), nullptr, GL_DYNAMIC_DRAW);

    std::vector<GLint> heads;
    heads.resize ((size_t) cellCount, -1);
    glBindBuffer (GL_SHADER_STORAGE_BUFFER, cellHeadsSSBO);
    glBufferData (GL_SHADER_STORAGE_BUFFER, (GLsizeiptr) (heads.size() * sizeof (GLint)), heads.data(), GL_DYNAMIC_DRAW);

    glBindBuffer (GL_SHADER_STORAGE_BUFFER, nextIndexSSBO);
    glBufferData (GL_SHADER_STORAGE_BUFFER, (GLsizeiptr) (currentParticleCount * (int) sizeof (GLint)), nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer (GL_SHADER_STORAGE_BUFFER, 0);

    buffersReady.store (true);
}

// Builds the combined view-projection matrix from orbit/pan/cameraDistance, used by the render shader.
juce::Matrix3D<float> MainComponent::getViewProjectionMatrix() const
{
    const float w = (float) juce::jmax (1, getWidth());
    const float h = (float) juce::jmax (1, getHeight());
    const float aspect = w / h;

    const float nearZ = 0.1f;
    const float farZ  = 500.0f;
    const float fovY  = juce::MathConstants<float>::pi / 3.0f; // 60 degrees
    const float top   = nearZ * std::tan (fovY * 0.5f);
    const float right = top * aspect;

    const auto proj = juce::Matrix3D<float>::fromFrustum (-right, right, -top, top, nearZ, farZ);

    // Treat orbit as rotating the world (simpler than building the true inverse camera rotation)
    const auto rot = orbit.getRotationMatrix();
    const auto trans = juce::Matrix3D<float>::fromTranslation ({ pan.x, pan.y, -cameraDistance });
    const auto view = trans * rot;

    return proj * view;
}

// Runs the per-frame compute pipeline: clear grid, build grid, step boids; then swaps particle ping-pong buffers.
void MainComponent::dispatchComputePasses (float dtSeconds)
{
    if (! buffersReady.load())
        return;

    // SSBO bindings (must match shaders)
    constexpr GLuint kParticlesInBinding  = 0;
    constexpr GLuint kParticlesOutBinding = 1;
    constexpr GLuint kCellHeadsBinding    = 2;
    constexpr GLuint kNextIndexBinding    = 3;

    // Clear grid
    glUseProgram (computeClearProgram);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kCellHeadsBinding, cellHeadsSSBO);
    setUniform1iIfPresent (computeClearProgram, "u_cellCount", cellCount);

    const GLuint clearGroups = (GLuint) ((cellCount + 255) / 256);
    glDispatchCompute (clearGroups, 1, 1);
    glMemoryBarrier (GL_SHADER_STORAGE_BARRIER_BIT);

    // Build grid
    glUseProgram (computeBuildProgram);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kParticlesInBinding, particlesSSBO[0]);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kCellHeadsBinding, cellHeadsSSBO);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kNextIndexBinding, nextIndexSSBO);

    setUniform1iIfPresent (computeBuildProgram, "u_particleCount", currentParticleCount);
    setUniform3iIfPresent (computeBuildProgram, "u_gridDims", gridDims);
    setUniform3fIfPresent (computeBuildProgram, "u_worldMin", worldMin);
    setUniform1fIfPresent (computeBuildProgram, "u_cellSize", cellSize);

    const GLuint buildGroups = (GLuint) ((currentParticleCount + 255) / 256);
    glDispatchCompute (buildGroups, 1, 1);
    glMemoryBarrier (GL_SHADER_STORAGE_BARRIER_BIT);

    // Boids step
    glUseProgram (computeStepProgram);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kParticlesInBinding,  particlesSSBO[0]);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kParticlesOutBinding, particlesSSBO[1]);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kCellHeadsBinding,    cellHeadsSSBO);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, kNextIndexBinding,    nextIndexSSBO);

    setUniform1iIfPresent (computeStepProgram, "u_particleCount", currentParticleCount);
    setUniform3iIfPresent (computeStepProgram, "u_gridDims", gridDims);
    setUniform3fIfPresent (computeStepProgram, "u_worldMin", worldMin);
    setUniform3fIfPresent (computeStepProgram, "u_worldMax", worldMax);
    setUniform1fIfPresent (computeStepProgram, "u_cellSize", cellSize);
    // Simulation speed: scales time without extra compute work (same number of dispatches).
    // Note: very large values would change behaviour due to integration stability, so the UI range is kept conservative.
    setUniform1fIfPresent (computeStepProgram, "u_dt", dtSeconds * simSpeed);

    setUniform1fIfPresent (computeStepProgram, "u_neighborRadius", neighborRadius);
    setUniform1fIfPresent (computeStepProgram, "u_separationRadius", separationRadius);
    setUniform1fIfPresent (computeStepProgram, "u_weightSeparation", weightSeparation);
    setUniform1fIfPresent (computeStepProgram, "u_weightAlignment", weightAlignment);
    setUniform1fIfPresent (computeStepProgram, "u_weightCohesion", weightCohesion);
    setUniform1fIfPresent (computeStepProgram, "u_minSpeed", minSpeed);
    setUniform1fIfPresent (computeStepProgram, "u_maxSpeed", maxSpeed);
    setUniform1fIfPresent (computeStepProgram, "u_maxAccel", maxAccel);
    setUniform1fIfPresent (computeStepProgram, "u_centerAttraction", centerAttraction);
    setUniform1fIfPresent (computeStepProgram, "u_boundaryMargin", boundaryMargin);
    setUniform1fIfPresent (computeStepProgram, "u_boundaryStrength", boundaryStrength);
    setUniform1iIfPresent (computeStepProgram, "u_wrapBounds", wrapBounds ? 1 : 0);

    // Coloring uniforms
    setUniform1iIfPresent (computeStepProgram, "u_colorMode", colorMode);
    setUniform1fIfPresent (computeStepProgram, "u_hueOffset", hueOffset);
    setUniform1fIfPresent (computeStepProgram, "u_hueRange", hueRange);
    setUniform1fIfPresent (computeStepProgram, "u_saturation", saturation);
    setUniform1fIfPresent (computeStepProgram, "u_value", value);
    setUniform1fIfPresent (computeStepProgram, "u_densityCurve", densityCurve);

    glDispatchCompute (buildGroups, 1, 1);
    glMemoryBarrier (GL_SHADER_STORAGE_BARRIER_BIT);

    // Ping-pong swap
    std::swap (particlesSSBO[0], particlesSSBO[1]);
}

// OpenGLAppComponent per-frame callback: computes dt, updates simulation via compute, then draws particles as points.
void MainComponent::render()
{
    jassert (juce::OpenGLHelpers::isContextActive());

    const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float dt = (float) (nowSeconds - lastFrameTimeSeconds);
    lastFrameTimeSeconds = nowSeconds;
    dt = juce::jlimit (0.0f, 0.05f, dt);

    // FPS readout (update ~2x/sec on the message thread)
    framesSinceFpsUpdate++;
    const double elapsedForFps = nowSeconds - fpsUpdateStartSeconds;
    if (elapsedForFps >= 0.5)
    {
        const double fps = (double) framesSinceFpsUpdate / juce::jmax (1.0e-6, elapsedForFps);
        framesSinceFpsUpdate = 0;
        fpsUpdateStartSeconds = nowSeconds;

        if (controlPanel != nullptr)
        {
            const auto text = "FPS: " + juce::String (fps, 1) + " | Particles: " + juce::String (currentParticleCount);
            juce::MessageManager::callAsync ([panel = controlPanel.get(), text]
            {
                if (panel != nullptr)
                    panel->setFpsText (text);
            });
        }
    }

    // Clear with a fallback color if shaders aren't loaded
    if (! shadersLoaded || ! computeAvailable)
    {
        juce::OpenGLHelpers::clear (juce::Colour (0xff1a1a2e));
        return;
    }

    dispatchComputePasses (dt);

    auto desktopScale = (float) openGLContext.getRenderingScale();
    glViewport (0, 0,
                juce::roundToInt (desktopScale * (float) getWidth()),
                juce::roundToInt (desktopScale * (float) getHeight()));

    juce::OpenGLHelpers::clear (juce::Colours::black);

    glUseProgram (renderProgram);
    glBindVertexArray (vao);

    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, 0, particlesSSBO[0]);

    // JUCE's component painting can change GL state after our render callback.
    // Ensure blending is enabled at draw time so alpha actually has an effect.
    glEnable (GL_DEPTH_TEST);
    glDepthFunc (GL_LEQUAL);
    glDepthMask (GL_TRUE);
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const auto viewProj = getViewProjectionMatrix();
    setUniformMatrix4IfPresent (renderProgram, "u_viewProj", viewProj);
    setUniform1fIfPresent (renderProgram, "u_pointSize", pointSize);
    setUniform1iIfPresent (renderProgram, "u_shape", particleShape); // 0 square, 1 circle, 2 line, 3 cube
    setUniform1fIfPresent (renderProgram, "u_alphaMul", alphaMul);

    glDrawArrays (GL_POINTS, 0, currentParticleCount);

    glBindVertexArray (0);
}

//==============================================================================
// JUCE 2D paint callback: draws shader compile/link errors as an overlay when shaders are not loaded.
void MainComponent::paint(juce::Graphics& g)
{
    // Show error message if shaders failed to load
    if ((! shadersLoaded || ! computeAvailable) && lastShaderError.isNotEmpty())
    {
        g.fillAll(juce::Colour(0xff1a1a2e));
        g.setColour(juce::Colours::red);
        g.setFont(14.0f);
        g.drawMultiLineText("Shader Error:\n" + lastShaderError, 20, 30, getWidth() - 40);
        
        g.setColour(juce::Colours::white);
        g.drawMultiLineText("\nShaders directory: " + getShadersDirectory().getFullPathName(), 20, 80, getWidth() - 40);
    }
}

// JUCE resized callback: updates camera/orbit viewport and positions the overlay control panel.
void MainComponent::resized()
{
    orbit.setViewport (getLocalBounds());

    if (controlPanel != nullptr)
        controlPanel->setBounds (10, 10, juce::jmin (420, getWidth() - 20), juce::jmin (380, getHeight() - 20));
}

//==============================================================================
// Mouse down handler: starts orbit interaction (left button) or pan mode (right button).
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    lastMouse = e.getPosition();
    rightDragging = e.mods.isRightButtonDown();

    if (e.mods.isLeftButtonDown())
        orbit.mouseDown (e.position);
}

// Mouse drag handler: left-drag orbits, right-drag pans the camera.
void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown())
    {
        orbit.mouseDrag (e.position);
        return;
    }

    if (rightDragging && e.mods.isRightButtonDown())
    {
        const auto delta = e.getPosition() - lastMouse;
        lastMouse = e.getPosition();

        const float scale = 0.01f * cameraDistance;
        pan.x += delta.x * scale;
        pan.y -= delta.y * scale;
    }
}

// Mouse wheel handler: zooms camera distance in/out.
void MainComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float zoomFactor = 1.0f - (wheel.deltaY * 0.15f);
    cameraDistance = juce::jlimit (2.0f, 200.0f, cameraDistance * zoomFactor);
}

//==============================================================================
// UI panel constructor: builds sliders/toggles, wires listeners, and starts a debounce timer that emits Params changes.
MainComponent::BoidsControlPanel::BoidsControlPanel()
{
    collapseButton.setToggleState (true, juce::dontSendNotification);
    collapseButton.addListener (this);
    addAndMakeVisible (collapseButton);

    wrapBoundsToggle.setToggleState (false, juce::dontSendNotification);
    wrapBoundsToggle.addListener (this);
    addAndMakeVisible (wrapBoundsToggle);

    fullscreenToggle.setToggleState (false, juce::dontSendNotification);
    fullscreenToggle.addListener (this);
    addAndMakeVisible (fullscreenToggle);

    auto initSlider = [this] (juce::Slider& s, double minV, double maxV, double step, const juce::String& suffix)
    {
        s.setRange (minV, maxV, step);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 20);
        s.setTextValueSuffix (suffix);
        s.addListener (this);
        addAndMakeVisible (s);
    };

    particleCountLabel.setText ("Particles", juce::dontSendNotification);
    addAndMakeVisible (particleCountLabel);
    initSlider (particleCountSlider, 1.0, 100000.0, 1.0, "");

    neighborRadiusLabel.setText ("Neighbor r", juce::dontSendNotification);
    addAndMakeVisible (neighborRadiusLabel);
    initSlider (neighborRadiusSlider, 0.1, 8.0, 0.01, "");

    separationRadiusLabel.setText ("Separation r", juce::dontSendNotification);
    addAndMakeVisible (separationRadiusLabel);
    initSlider (separationRadiusSlider, 0.05, 4.0, 0.01, "");

    wSepLabel.setText ("Weight sep", juce::dontSendNotification);
    addAndMakeVisible (wSepLabel);
    initSlider (wSepSlider, 0.0, 5.0, 0.01, "");

    wAliLabel.setText ("Weight ali", juce::dontSendNotification);
    addAndMakeVisible (wAliLabel);
    initSlider (wAliSlider, 0.0, 5.0, 0.01, "");

    wCohLabel.setText ("Weight coh", juce::dontSendNotification);
    addAndMakeVisible (wCohLabel);
    initSlider (wCohSlider, 0.0, 5.0, 0.01, "");

    minSpeedLabel.setText ("Min speed", juce::dontSendNotification);
    addAndMakeVisible (minSpeedLabel);
    initSlider (minSpeedSlider, 0.0, 10.0, 0.01, "");

    maxSpeedLabel.setText ("Max speed", juce::dontSendNotification);
    addAndMakeVisible (maxSpeedLabel);
    initSlider (maxSpeedSlider, 0.1, 20.0, 0.01, "");

    maxAccelLabel.setText ("Max accel", juce::dontSendNotification);
    addAndMakeVisible (maxAccelLabel);
    initSlider (maxAccelSlider, 0.0, 80.0, 0.1, "");

    simSpeedLabel.setText ("Sim speed", juce::dontSendNotification);
    addAndMakeVisible (simSpeedLabel);
    initSlider (simSpeedSlider, 0.1, 2.0, 0.01, "x");

    centerAttractionLabel.setText ("Center pull", juce::dontSendNotification);
    addAndMakeVisible (centerAttractionLabel);
    initSlider (centerAttractionSlider, 0.0, 3.0, 0.01, "");

    boundaryMarginLabel.setText ("Bound margin", juce::dontSendNotification);
    addAndMakeVisible (boundaryMarginLabel);
    initSlider (boundaryMarginSlider, 0.05, 5.0, 0.01, "");

    boundaryStrengthLabel.setText ("Bound strength", juce::dontSendNotification);
    addAndMakeVisible (boundaryStrengthLabel);
    initSlider (boundaryStrengthSlider, 0.0, 80.0, 0.1, "");

    pointSizeLabel.setText ("Point size", juce::dontSendNotification);
    addAndMakeVisible (pointSizeLabel);
    initSlider (pointSizeSlider, 1.0, 8.0, 0.1, "");

    alphaLabel.setText ("Alpha", juce::dontSendNotification);
    addAndMakeVisible (alphaLabel);
    initSlider (alphaSlider, 0.0, 1.0, 0.01, "");

    particleShapeLabel.setText ("Shape", juce::dontSendNotification);
    addAndMakeVisible (particleShapeLabel);
    particleShapeBox.addItem ("Square", 1);
    particleShapeBox.addItem ("Circle", 2);
    particleShapeBox.addItem ("Line", 3);
    particleShapeBox.addItem ("Cube", 4);
    particleShapeBox.onChange = [this] { pendingAnyChange.store (true); };
    addAndMakeVisible (particleShapeBox);

    colorModeLabel.setText ("Color mode", juce::dontSendNotification);
    addAndMakeVisible (colorModeLabel);
    colorModeBox.addItem ("Solid", 1);
    colorModeBox.addItem ("Heading", 2);
    colorModeBox.addItem ("Speed", 3);
    colorModeBox.addItem ("Density", 4);
    colorModeBox.onChange = [this] { pendingAnyChange.store (true); };
    addAndMakeVisible (colorModeBox);

    hueOffsetLabel.setText ("Hue offset", juce::dontSendNotification);
    addAndMakeVisible (hueOffsetLabel);
    initSlider (hueOffsetSlider, 0.0, 1.0, 0.001, "");

    hueRangeLabel.setText ("Hue range", juce::dontSendNotification);
    addAndMakeVisible (hueRangeLabel);
    initSlider (hueRangeSlider, 0.0, 1.0, 0.001, "");

    saturationLabel.setText ("Saturation", juce::dontSendNotification);
    addAndMakeVisible (saturationLabel);
    initSlider (saturationSlider, 0.0, 1.0, 0.001, "");

    valueLabel.setText ("Brightness", juce::dontSendNotification);
    addAndMakeVisible (valueLabel);
    initSlider (valueSlider, 0.0, 1.0, 0.001, "");

    densityCurveLabel.setText ("Density curve", juce::dontSendNotification);
    addAndMakeVisible (densityCurveLabel);
    initSlider (densityCurveSlider, 0.1, 8.0, 0.01, "");

    fpsLabel.setText ("", juce::dontSendNotification);
    addAndMakeVisible (fpsLabel);

    startTimerHz (10); // debounce slider updates
}

// UI panel destructor: removes listeners to avoid callbacks after destruction.
MainComponent::BoidsControlPanel::~BoidsControlPanel()
{
    particleCountSlider.removeListener (this);
    collapseButton.removeListener (this);
    wrapBoundsToggle.removeListener (this);
    fullscreenToggle.removeListener (this);

    neighborRadiusSlider.removeListener (this);
    separationRadiusSlider.removeListener (this);
    wSepSlider.removeListener (this);
    wAliSlider.removeListener (this);
    wCohSlider.removeListener (this);
    minSpeedSlider.removeListener (this);
    maxSpeedSlider.removeListener (this);
    maxAccelSlider.removeListener (this);
    simSpeedSlider.removeListener (this);
    centerAttractionSlider.removeListener (this);
    boundaryMarginSlider.removeListener (this);
    boundaryStrengthSlider.removeListener (this);
    pointSizeSlider.removeListener (this);
    alphaSlider.removeListener (this);

    hueOffsetSlider.removeListener (this);
    hueRangeSlider.removeListener (this);
    saturationSlider.removeListener (this);
    valueSlider.removeListener (this);
    densityCurveSlider.removeListener (this);
}

// Sets the callback that receives updated Params when the UI changes (called by MainComponent).
void MainComponent::BoidsControlPanel::setOnParamsChanged (std::function<void(Params)> cb)
{
    onParamsChanged = std::move (cb);
}

void MainComponent::BoidsControlPanel::setOnFullscreenChanged (std::function<void(bool)> cb)
{
    onFullscreenChanged = std::move (cb);
}

// Updates the UI controls to match the provided Params without triggering notifications (sync UI from simulation state).
void MainComponent::BoidsControlPanel::setParams (Params p)
{
    particleCountSlider.setValue ((double) p.particleCount, juce::dontSendNotification);
    neighborRadiusSlider.setValue ((double) p.neighborRadius, juce::dontSendNotification);
    separationRadiusSlider.setValue ((double) p.separationRadius, juce::dontSendNotification);
    wSepSlider.setValue ((double) p.weightSeparation, juce::dontSendNotification);
    wAliSlider.setValue ((double) p.weightAlignment, juce::dontSendNotification);
    wCohSlider.setValue ((double) p.weightCohesion, juce::dontSendNotification);
    minSpeedSlider.setValue ((double) p.minSpeed, juce::dontSendNotification);
    maxSpeedSlider.setValue ((double) p.maxSpeed, juce::dontSendNotification);
    maxAccelSlider.setValue ((double) p.maxAccel, juce::dontSendNotification);
    simSpeedSlider.setValue ((double) p.simSpeed, juce::dontSendNotification);
    centerAttractionSlider.setValue ((double) p.centerAttraction, juce::dontSendNotification);
    boundaryMarginSlider.setValue ((double) p.boundaryMargin, juce::dontSendNotification);
    boundaryStrengthSlider.setValue ((double) p.boundaryStrength, juce::dontSendNotification);
    wrapBoundsToggle.setToggleState (p.wrapBounds, juce::dontSendNotification);
    pointSizeSlider.setValue ((double) p.pointSize, juce::dontSendNotification);
    alphaSlider.setValue ((double) p.alphaMul, juce::dontSendNotification);

    // ComboBox item ids start at 1, map shape 0..3 => 1..4
    particleShapeBox.setSelectedId (juce::jlimit (1, 4, p.particleShape + 1), juce::dontSendNotification);

    // ComboBox item ids start at 1, map mode 0..3 => 1..4
    colorModeBox.setSelectedId (juce::jlimit (1, 4, p.colorMode + 1), juce::dontSendNotification);
    hueOffsetSlider.setValue ((double) p.hueOffset, juce::dontSendNotification);
    hueRangeSlider.setValue ((double) p.hueRange, juce::dontSendNotification);
    saturationSlider.setValue ((double) p.saturation, juce::dontSendNotification);
    valueSlider.setValue ((double) p.value, juce::dontSendNotification);
    densityCurveSlider.setValue ((double) p.densityCurve, juce::dontSendNotification);
}

// Updates the FPS label text (called from MainComponent via MessageManager::callAsync()).
void MainComponent::BoidsControlPanel::setFpsText (juce::String text)
{
    fpsLabel.setText (std::move (text), juce::dontSendNotification);
}

// JUCE resized callback: recompute child bounds.
void MainComponent::BoidsControlPanel::resized()
{
    updateLayout();
}

// Paints the translucent background panel behind the controls.
void MainComponent::BoidsControlPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
}

// Marks that some slider changed; actual Params emission is debounced in this panel's timerCallback().
void MainComponent::BoidsControlPanel::sliderValueChanged (juce::Slider* s)
{
    juce::ignoreUnused (s);
    pendingAnyChange.store (true);
}

// Handles toggle/button actions (collapse and wrap bounds) and marks pending changes for debounce.
void MainComponent::BoidsControlPanel::buttonClicked (juce::Button* b)
{
    if (b == &collapseButton)
    {
        collapsed = ! collapseButton.getToggleState();
        updateLayout();
        return;
    }

    if (b == &wrapBoundsToggle)
    {
        pendingAnyChange.store (true);
        return;
    }

    if (b == &fullscreenToggle)
    {
        if (onFullscreenChanged != nullptr)
            onFullscreenChanged (fullscreenToggle.getToggleState());
        return;
    }
}

// Debounce tick (~10Hz): if any control changed, gathers Params and calls onParamsChanged.
void MainComponent::BoidsControlPanel::timerCallback()
{
    if (! pendingAnyChange.exchange (false))
        return;

    if (onParamsChanged == nullptr)
        return;

    Params p;
    p.particleCount = (int) particleCountSlider.getValue();
    p.neighborRadius = (float) neighborRadiusSlider.getValue();
    p.separationRadius = (float) separationRadiusSlider.getValue();
    p.weightSeparation = (float) wSepSlider.getValue();
    p.weightAlignment = (float) wAliSlider.getValue();
    p.weightCohesion = (float) wCohSlider.getValue();
    p.minSpeed = (float) minSpeedSlider.getValue();
    p.maxSpeed = (float) maxSpeedSlider.getValue();
    p.maxAccel = (float) maxAccelSlider.getValue();
    p.simSpeed = (float) simSpeedSlider.getValue();
    p.centerAttraction = (float) centerAttractionSlider.getValue();
    p.boundaryMargin = (float) boundaryMarginSlider.getValue();
    p.boundaryStrength = (float) boundaryStrengthSlider.getValue();
    p.wrapBounds = wrapBoundsToggle.getToggleState();
    p.pointSize = (float) pointSizeSlider.getValue();
    p.alphaMul = (float) alphaSlider.getValue();

    p.particleShape = juce::jlimit (0, 3, particleShapeBox.getSelectedId() - 1);

    p.colorMode = juce::jlimit (0, 3, colorModeBox.getSelectedId() - 1);
    p.hueOffset = (float) hueOffsetSlider.getValue();
    p.hueRange = (float) hueRangeSlider.getValue();
    p.saturation = (float) saturationSlider.getValue();
    p.value = (float) valueSlider.getValue();
    p.densityCurve = (float) densityCurveSlider.getValue();

    onParamsChanged (p);
}

// Lays out the controls and manages collapsed state; also resizes the panel height to fit visible content.
void MainComponent::BoidsControlPanel::updateLayout()
{
    // Resize the panel itself to match content so collapse doesn't leave dead space.
    const int rowH = 22;
    const int rowGap = 4;
    const int pad = 10;
    const int headerH = rowH;
    const int wrapH = rowH;
    const int fullscreenH = rowH;
    const int fpsH = 20;

    const int sliderRows = 22; // includes combo rows (shape + color) and color sliders

    const int expandedContentH =
        headerH
        + rowGap
        + wrapH
        + rowGap
        + fullscreenH
        + rowGap
        + sliderRows * (rowH + rowGap)
        + fpsH;

    const int desiredHeight = (collapsed ? (pad * 2 + headerH) : (pad * 2 + expandedContentH));

    if (getHeight() != desiredHeight)
    {
        auto b = getBounds();
        b.setHeight (desiredHeight);
        setBounds (b);
        return; // resized() will re-enter and lay out children using the new bounds
    }

    auto r = getLocalBounds().reduced (pad);

    collapseButton.setBounds (r.removeFromTop (22));

    if (collapsed)
    {
        for (auto* c : getChildren())
            if (c != &collapseButton)
                c->setVisible (false);
        return;
    }

    for (auto* c : getChildren())
        c->setVisible (true);

    wrapBoundsToggle.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);

    fullscreenToggle.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);

    auto row = [&r] { auto x = r.removeFromTop (22); r.removeFromTop (4); return x; };

    auto place = [] (juce::Label& l, juce::Slider& s, juce::Rectangle<int> area)
    {
        l.setBounds (area.removeFromLeft (110));
        s.setBounds (area);
    };

    place (particleCountLabel, particleCountSlider, row());
    place (neighborRadiusLabel, neighborRadiusSlider, row());
    place (separationRadiusLabel, separationRadiusSlider, row());
    place (wSepLabel, wSepSlider, row());
    place (wAliLabel, wAliSlider, row());
    place (wCohLabel, wCohSlider, row());
    place (minSpeedLabel, minSpeedSlider, row());
    place (maxSpeedLabel, maxSpeedSlider, row());
    place (maxAccelLabel, maxAccelSlider, row());
    place (simSpeedLabel, simSpeedSlider, row());
    place (centerAttractionLabel, centerAttractionSlider, row());
    place (boundaryMarginLabel, boundaryMarginSlider, row());
    place (boundaryStrengthLabel, boundaryStrengthSlider, row());
    place (pointSizeLabel, pointSizeSlider, row());
    place (alphaLabel, alphaSlider, row());

    // Combo row for particle shape
    {
        auto area = row();
        particleShapeLabel.setBounds (area.removeFromLeft (110));
        particleShapeBox.setBounds (area);
    }

    // Combo row for color mode
    {
        auto area = row();
        colorModeLabel.setBounds (area.removeFromLeft (110));
        colorModeBox.setBounds (area);
    }

    place (hueOffsetLabel, hueOffsetSlider, row());
    place (hueRangeLabel, hueRangeSlider, row());
    place (saturationLabel, saturationSlider, row());
    place (valueLabel, valueSlider, row());
    place (densityCurveLabel, densityCurveSlider, row());

    fpsLabel.setBounds (r.removeFromTop (20));
}
