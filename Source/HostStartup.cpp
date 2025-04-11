#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"

#if ! (JUCE_PLUGINHOST_VST || JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the audio plugin host, you probably want to enable VST and/or AU support"
#endif

class PluginHostApp : public JUCEApplication
{
public:
    PluginHostApp() {}

    void initialise(const String&) override
    {
        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments(&options);

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters(options);

        LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

        mainWindow = std::make_unique<IconMenu>();
        #if JUCE_MAC
            Process::setDockIconVisible(false);
        #endif
    }

    void shutdown() override
    {
        mainWindow.reset();
        appProperties.reset();
        LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override       { return "SoftHost"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       
    {
        StringArray multiInstance = getParameter("-multi-instance");
        return multiInstance.size() == 2;
    }

    ApplicationCommandManager commandManager;
    std::unique_ptr<ApplicationProperties> appProperties;
    LookAndFeel_V3 lookAndFeel;

private:
    std::unique_ptr<IconMenu> mainWindow;

    StringArray PluginHostApp::getParameter(String lookFor) 
    {
        StringArray parameters = getCommandLineParameterArray();
        StringArray found;
    
        for (const auto& param : parameters) // Use range-based for loop
        {
            if (param.contains(lookFor))
            {
                found.add(lookFor);
                int delimiter = param.indexOf("="); // Don't need the 0 parameter
                if (delimiter != -1) // Check if "=" exists
                {
                    String val = param.substring(delimiter + 1);
                    found.add(val);
                    return found;
                }
            }
        }
        return found;
    }

    void checkArguments(PropertiesFile::Options *options) 
    {
        StringArray multiInstance = getParameter("-multi-instance");
        if (multiInstance.size() == 2)
            options->filenameSuffix = multiInstance[1] + "." + options->filenameSuffix;
    }
};


static PluginHostApp& getApp() { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationCommandManager& getCommandManager() { return getApp().commandManager; }
ApplicationProperties& getAppProperties() { return *getApp().appProperties; }

START_JUCE_APPLICATION(PluginHostApp)