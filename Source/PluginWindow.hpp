#ifndef PluginWindow_hpp
#define PluginWindow_hpp

ApplicationProperties& getAppProperties();

class PluginWindow : public DocumentWindow
{
public:
    enum WindowFormatType
    {
        Normal = 0,
        Generic,
        Programs,
        Parameters,
        NumTypes
    };

    PluginWindow(Component* pluginEditor, AudioProcessorGraph::Node::Ptr node, WindowFormatType type);
    ~PluginWindow() override;

    static std::unique_ptr<PluginWindow> getWindowFor(AudioProcessorGraph::Node::Ptr node, WindowFormatType type);

    static void closeCurrentlyOpenWindowsFor(const AudioProcessorGraph::NodeID nodeId);
    static void closeAllCurrentlyOpenWindows();
    static bool containsActiveWindows();

    void moved() override;
    void closeButtonPressed() override;

private:
    AudioProcessorGraph::Node::Ptr owner;
    WindowFormatType type;

    float getDesktopScaleFactor() const override { return 1.0f; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

inline String toString(PluginWindow::WindowFormatType type)
{
    switch (type)
    {
        case PluginWindow::Normal:     return "Normal";
        case PluginWindow::Generic:    return "Generic";
        case PluginWindow::Programs:   return "Programs";
        case PluginWindow::Parameters: return "Parameters";
        default:                       return String();
    }
}

inline String getLastXProp(PluginWindow::WindowFormatType type) { return "uiLastX_" + toString(type); }
inline String getLastYProp(PluginWindow::WindowFormatType type) { return "uiLastY_" + toString(type); }
inline String getOpenProp(PluginWindow::WindowFormatType type)  { return "uiopen_"  + toString(type); }

#endif /* PluginWindow_hpp */