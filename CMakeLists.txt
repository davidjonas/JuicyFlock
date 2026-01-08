cmake_minimum_required(VERSION 3.22)

project(JuicyFlock VERSION 1.0.0)

# Add JUCE subdirectory
add_subdirectory(JUCE)

# Create GUI application
juce_add_gui_app(JuicyFlock
    PRODUCT_NAME "JuicyFlock"
    COMPANY_NAME "MyCompany"
    BUNDLE_ID "com.mycompany.juicyflock")

# Add source files
target_sources(JuicyFlock
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/MainComponent.h)

# Generate the JuceHeader.h file
juce_generate_juce_header(JuicyFlock)

# Link JUCE modules
target_link_libraries(JuicyFlock
    PRIVATE
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Set C++ standard
target_compile_features(JuicyFlock PRIVATE cxx_std_17)

# Copy runtime shader files next to the executable (includes .vert/.frag/.comp)
add_custom_command(TARGET JuicyFlock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
        "$<TARGET_FILE_DIR:JuicyFlock>/Shaders"
)

