#pragma once

#include <JuceHeader.h>

//==============================================================================
class MainComponent : public juce::OpenGLAppComponent,
                      private juce::Timer
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void initialise() override;
    void shutdown() override;
    void render() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    //==============================================================================
    void timerCallback() override;

    juce::File getShadersDirectory() const;

    // Shader management (compute + render)
    bool reloadAllShadersOnGLThread();
    bool compileComputeProgramFromFile (juce::File file, unsigned int& outProgram, juce::String& outError);
    bool compileRenderProgramFromFiles (juce::File vertexFile, juce::File fragmentFile, unsigned int& outProgram, juce::String& outError);
    void deletePrograms();

    // GL + simulation
    void ensureGL43CoreContext();
    bool checkGLCapabilitiesOnGLThread();
    void rebuildBuffersOnGLThread (int newParticleCount);
    void deleteBuffers();
    void dispatchComputePasses (float dtSeconds);
    juce::Matrix3D<float> getViewProjectionMatrix() const;

    // UI
    class BoidsControlPanel final : public juce::Component,
                                    private juce::Slider::Listener,
                                    private juce::Button::Listener,
                                    private juce::Timer
    {
    public:
        struct Params
        {
            int particleCount = 60000;
            float neighborRadius = 1.34f;
            float separationRadius = 2.07f;
            float weightSeparation = 1.85f;
            float weightAlignment = 1.37f;
            float weightCohesion = 0.5f;
            float minSpeed = 1.0f;
            float maxSpeed = 10.0f;
            float maxAccel = 8.0f;
            float simSpeed = 1.0f; // time scale multiplier (1.0 = real-time)
            float centerAttraction = 0.3f;
            float boundaryMargin = 5.0f;
            float boundaryStrength = 10.0f;
            bool wrapBounds = false;
            float pointSize = 1.0f;
            float alphaMul = 0.65f;

            // Rendering
            // 0 square, 1 circle, 2 line (screen-facing, aligned to velocity), 3 cube (fake shaded sprite)
            int particleShape = 1;

            // Coloring
            int colorMode = 1;          // 0 solid, 1 heading, 2 speed, 3 density
            float hueOffset = 0.0f;     // 0..1
            float hueRange = 0.7f;      // 0..1
            float saturation = 0.4f;    // 0..1
            float value = 1.0f;         // 0..1
            float densityCurve = 1.0f;  // >0, applied as pow(t, densityCurve)
        };

        BoidsControlPanel();
        ~BoidsControlPanel() override;

        void resized() override;
        void paint (juce::Graphics& g) override;

        void setParams (Params p);
        void setFpsText (juce::String text);
        void setOnParamsChanged (std::function<void(Params)> cb);
        void setOnFullscreenChanged (std::function<void(bool)> cb);

    private:
        void sliderValueChanged (juce::Slider* s) override;
        void buttonClicked (juce::Button* b) override;
        void timerCallback() override;

        void updateLayout();
        void addRow (juce::Label& label, juce::Component& control);

        juce::ToggleButton collapseButton { "Controls" };
        juce::ToggleButton wrapBoundsToggle { "Wrap bounds" };
        juce::ToggleButton fullscreenToggle { "Fullscreen" };

        juce::Label particleCountLabel;
        juce::Slider particleCountSlider;

        juce::Label neighborRadiusLabel;
        juce::Slider neighborRadiusSlider;

        juce::Label separationRadiusLabel;
        juce::Slider separationRadiusSlider;

        juce::Label wSepLabel;
        juce::Slider wSepSlider;
        juce::Label wAliLabel;
        juce::Slider wAliSlider;
        juce::Label wCohLabel;
        juce::Slider wCohSlider;

        juce::Label minSpeedLabel;
        juce::Slider minSpeedSlider;
        juce::Label maxSpeedLabel;
        juce::Slider maxSpeedSlider;

        juce::Label maxAccelLabel;
        juce::Slider maxAccelSlider;

        juce::Label simSpeedLabel;
        juce::Slider simSpeedSlider;

        juce::Label centerAttractionLabel;
        juce::Slider centerAttractionSlider;

        juce::Label boundaryMarginLabel;
        juce::Slider boundaryMarginSlider;
        juce::Label boundaryStrengthLabel;
        juce::Slider boundaryStrengthSlider;

        juce::Label pointSizeLabel;
        juce::Slider pointSizeSlider;

        juce::Label alphaLabel;
        juce::Slider alphaSlider;

        juce::Label particleShapeLabel;
        juce::ComboBox particleShapeBox;

        juce::Label colorModeLabel;
        juce::ComboBox colorModeBox;

        juce::Label hueOffsetLabel;
        juce::Slider hueOffsetSlider;
        juce::Label hueRangeLabel;
        juce::Slider hueRangeSlider;
        juce::Label saturationLabel;
        juce::Slider saturationSlider;
        juce::Label valueLabel;
        juce::Slider valueSlider;
        juce::Label densityCurveLabel;
        juce::Slider densityCurveSlider;

        juce::Label fpsLabel;

        bool collapsed = false;
        std::function<void(Params)> onParamsChanged;
        std::function<void(bool)> onFullscreenChanged;
        std::atomic<bool> pendingAnyChange { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoidsControlPanel)
    };

    std::unique_ptr<BoidsControlPanel> controlPanel;

    // Camera
    juce::Draggable3DOrientation orbit;
    juce::Point<int> lastMouse;
    bool rightDragging = false;
    juce::Vector3D<float> pan { 0.0f, 0.0f, 0.0f };
    float cameraDistance = 18.0f;

    // Shader files (compute + render)
    juce::File computeClearFile, computeBuildFile, computeStepFile;
    juce::File renderVertexFile, renderFragmentFile;
    juce::Time lastClearMod, lastBuildMod, lastStepMod, lastRenderVertMod, lastRenderFragMod;

    // GL objects
    unsigned int vao = 0;
    unsigned int particlesSSBO[2] { 0, 0 };
    unsigned int cellHeadsSSBO = 0;
    unsigned int nextIndexSSBO = 0;
    unsigned int computeClearProgram = 0;
    unsigned int computeBuildProgram = 0;
    unsigned int computeStepProgram = 0;
    unsigned int renderProgram = 0;

    // Simulation parameters are initialised from BoidsControlPanel::Params defaults in MainComponent::MainComponent().
    int currentParticleCount = 0;
    std::atomic<int> requestedParticleCount { 0 };
    std::atomic<bool> buffersReady { false };

    // Simulation / grid parameters (kept simple for now, configurable later)
    juce::Vector3D<float> worldMin { -10.0f, -10.0f, -10.0f };
    juce::Vector3D<float> worldMax {  10.0f,  10.0f,  10.0f };
    float cellSize = 2.0f; // typically ~= neighbor radius
    juce::Vector3D<int> gridDims { 10, 10, 10 };
    int cellCount = 1000;
    int maxCellCount = 1 << 20; // safety clamp to avoid clearing/building huge grids per-frame (e.g. very small neighborRadius)

    float neighborRadius = 0.0f;
    float separationRadius = 0.0f;
    float maxSpeed = 0.0f;
    float minSpeed = 0.0f;
    float weightSeparation = 0.0f;
    float weightAlignment  = 0.0f;
    float weightCohesion   = 0.0f;
    float maxAccel = 0.0f;
    float simSpeed = 1.0f;
    float centerAttraction = 0.0f;
    float boundaryMargin = 0.0f;
    float boundaryStrength = 0.0f;
    bool wrapBounds = false;
    float pointSize = 0.0f;
    float alphaMul = 0.0f;
    int particleShape = 1; // matches shader u_shape mapping

    // Coloring
    int colorMode = 0;
    float hueOffset = 0.0f;
    float hueRange = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
    float densityCurve = 0.0f;

    double lastFrameTimeSeconds = 0.0;
    int framesSinceFpsUpdate = 0;
    double fpsUpdateStartSeconds = 0.0;

    float startTime = 0.0f;
    bool shadersLoaded = false;
    bool computeAvailable = false;
    juce::String lastShaderError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
