//
//  IconMenu.cpp
//  SoftHost
//
//  Created by Rolando Islas on 12/26/15.
//  Updated and optimized on 04/10/25.
//

#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include <ctime>
#include <limits.h>
#if JUCE_WINDOWS
#include "Windows.h"
#endif

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
    PluginListWindow(IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
        : DocumentWindow("Available Plugins", Colours::white,
            DocumentWindow::minimiseButton | DocumentWindow::closeButton),
        owner(owner_)
    {
        const File deadMansPedalFile(getAppProperties().getUserSettings()
            ->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

        setContentOwned(new PluginListComponent(pluginFormatManager,
            owner.knownPluginList,
            deadMansPedalFile,
            getAppProperties().getUserSettings()), true);

        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(300, 400, 800, 1500);
        setTopLeftPosition(60, 60);

        restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
        setVisible(true);
    }

    ~PluginListWindow() override
    {
        getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner.removePluginsLackingInputOutput();
        #if JUCE_MAC
        Process::setDockIconVisible(false);
        #endif
        owner.pluginListWindow.reset(nullptr);
    }

private:
    IconMenu& owner;
};

IconMenu::IconMenu() : 
    INDEX_EDIT(1000000), 
    INDEX_BYPASS(2000000), 
    INDEX_DELETE(3000000), 
    INDEX_MOVE_UP(4000000), 
    INDEX_MOVE_DOWN(5000000)
{
    // Initialization
    formatManager.addDefaultFormats();
    #if JUCE_WINDOWS
    x = y = 0;
    #endif
    
    // Audio device setup
    std::unique_ptr<XmlElement> savedAudioState(getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    deviceManager.initialise(256, 256, savedAudioState.get(), true);
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    
    // Load all plugins
    std::unique_ptr<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    
    // Load active plugins
    std::unique_ptr<XmlElement> savedPluginListActive(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
    if (savedPluginListActive != nullptr)
        activePluginList.recreateFromXml(*savedPluginListActive);
    loadActivePlugins();
    activePluginList.addChangeListener(this);
    
    // Setup system tray icon
    setIcon();
    setIconTooltip(JUCEApplication::getInstance()->getApplicationName());
}

IconMenu::~IconMenu()
{
    savePluginStates();
}

void IconMenu::setIcon()
{
    // Set menu icon
    #if JUCE_MAC
        if (exec("defaults read -g AppleInterfaceStyle").compare("Dark") == 1)
            setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize),
                         ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize));
        else
            setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize),
                         ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
    #else
        String defaultColor;
        #if JUCE_WINDOWS
            defaultColor = "white";
        #elif JUCE_LINUX
            defaultColor = "black";
        #endif
        if (!getAppProperties().getUserSettings()->containsKey("icon"))
            getAppProperties().getUserSettings()->setValue("icon", defaultColor);
        String color = getAppProperties().getUserSettings()->getValue("icon");
        Image icon;
        if (color.equalsIgnoreCase("white"))
            icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
        else if (color.equalsIgnoreCase("black"))
            icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
        
        // Fixed setIconImage call with two images for cross-platform support
        setIconImage(icon, icon);
    #endif
}

void IconMenu::loadActivePlugins()
{
    const AudioProcessorGraph::NodeID INPUT(1000000);
    const AudioProcessorGraph::NodeID OUTPUT(1000001);
    const int CHANNEL_ONE = 0;
    const int CHANNEL_TWO = 1;
    
    PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();
    
    // Use std::unique_ptr for AudioProcessorGraph::AudioGraphIOProcessor
    auto inputProcessor = std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
        AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode);
    auto outputProcessor = std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
        AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode);
        
    inputNode = graph.addNode(std::move(inputProcessor), INPUT);
    outputNode = graph.addNode(std::move(outputProcessor), OUTPUT);
    
    // If no active plugins, connect input directly to output
    if (activePluginList.getNumTypes() == 0)
    {
        // Updated AudioProcessorGraph::addConnection signature
        graph.addConnection({{INPUT, CHANNEL_ONE}, {OUTPUT, CHANNEL_ONE}});
        graph.addConnection({{INPUT, CHANNEL_TWO}, {OUTPUT, CHANNEL_TWO}});
        return;
    }
    
    int pluginTime = 0;
    AudioProcessorGraph::NodeID lastId;
    bool hasInputConnected = false;
    
    // Load plugins in order of time added
    for (int i = 1; i <= activePluginList.getNumTypes(); i++)
    {
        PluginDescription plugin = getNextPluginOlderThanTime(pluginTime);
        String errorMessage;
        
        // Create plugin instance with std::unique_ptr
        std::unique_ptr<AudioPluginInstance> instance = formatManager.createPluginInstance(
            plugin, graph.getSampleRate(), graph.getBlockSize(), errorMessage);
        
        if (instance == nullptr) {
            // Handle the error appropriately
            continue;
        }
        
        // Restore plugin state
        String pluginUid = getKey("state", plugin);
        String savedPluginState = getAppProperties().getUserSettings()->getValue(pluginUid);
        MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding(savedPluginState);
        if (savedPluginBinary.getSize() > 0)
            instance->setStateInformation(savedPluginBinary.getData(), savedPluginBinary.getSize());
            
        AudioProcessorGraph::NodeID nodeId(i);
        auto node = graph.addNode(std::move(instance), nodeId);
        
        // Check if plugin is bypassed
        String key = getKey("bypass", plugin);
        bool bypass = getAppProperties().getUserSettings()->getBoolValue(key, false);
        
        // Handle connections
        if (!hasInputConnected && !bypass)
        {
            // First active plugin gets connected to input
            graph.addConnection({{INPUT, CHANNEL_ONE}, {nodeId, CHANNEL_ONE}});
            graph.addConnection({{INPUT, CHANNEL_TWO}, {nodeId, CHANNEL_TWO}});
            hasInputConnected = true;
        }
        else if (!bypass && lastId != AudioProcessorGraph::NodeID())
        {
            // Connect previous plugin to current
            graph.addConnection({{lastId, CHANNEL_ONE}, {nodeId, CHANNEL_ONE}});
            graph.addConnection({{lastId, CHANNEL_TWO}, {nodeId, CHANNEL_TWO}});
        }
        
        if (!bypass)
            lastId = nodeId;
    }
    
    if (lastId != AudioProcessorGraph::NodeID())
    {
        // Last active plugin to output
        graph.addConnection({{lastId, CHANNEL_ONE}, {OUTPUT, CHANNEL_ONE}});
        graph.addConnection({{lastId, CHANNEL_TWO}, {OUTPUT, CHANNEL_TWO}});
    }
    else
    {
        // No active plugins (all bypassed), connect input to output
        graph.addConnection({{INPUT, CHANNEL_ONE}, {OUTPUT, CHANNEL_ONE}});
        graph.addConnection({{INPUT, CHANNEL_TWO}, {OUTPUT, CHANNEL_TWO}});
    }
}

PluginDescription IconMenu::getNextPluginOlderThanTime(int &time)
{
    int timeStatic = time;
    PluginDescription closest;
    int diff = INT_MAX;
    
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        const auto& plugin = activePluginList.getTypes().getReference(i);
        String key = getKey("order", plugin);
        String pluginTimeString = getAppProperties().getUserSettings()->getValue(key);
        int pluginTime = static_cast<int>(pluginTimeString.getIntValue());
        
        if (pluginTime > timeStatic && abs(timeStatic - pluginTime) < diff)
        {
            diff = abs(timeStatic - pluginTime);
            closest = plugin;
            time = pluginTime;
        }
    }
    return closest;
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        std::unique_ptr<XmlElement> savedPluginList(knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue("pluginList", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        std::unique_ptr<XmlElement> savedPluginList(activePluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue("pluginListActive", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
}

#if JUCE_MAC
std::string IconMenu::exec(const char* cmd)
{
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return "ERROR";
    
    char buffer[128];
    std::string result = "";
    
    while (!feof(pipe.get()))
    {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;
    }
    
    return result;
}
#endif

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    
    if (menuIconLeftClicked) {
        menu.addItem(1, "Preferences");
        menu.addItem(2, "Edit Plugins");
        menu.addSeparator();
        menu.addSectionHeader("Active Plugins");
        
        // Add active plugins to menu
        std::vector<PluginDescription> timeSorted = getTimeSortedList();
        
        for (int i = 0; i < timeSorted.size(); i++)
        {
            PopupMenu options;
            options.addItem(INDEX_EDIT + i, "Edit");
            
            String key = getKey("bypass", timeSorted[i]);
            bool bypass = getAppProperties().getUserSettings()->getBoolValue(key);
            options.addItem(INDEX_BYPASS + i, "Bypass", true, bypass);
            
            options.addSeparator();
            options.addItem(INDEX_MOVE_UP + i, "Move Up", i > 0);
            options.addItem(INDEX_MOVE_DOWN + i, "Move Down", i < timeSorted.size() - 1);
            
            options.addSeparator();
            options.addItem(INDEX_DELETE + i, "Delete");
            
            menu.addSubMenu(timeSorted[i].name, options);
        }
        
        menu.addSeparator();
        menu.addSectionHeader("Available Plugins");
        
        // Update KnownPluginList usage - use modern alternative
        const auto& pluginTypes = knownPluginList.getTypes();
        for (int i = 0; i < pluginTypes.size(); ++i) // Using int is acceptable if num types is within int range, JUCE often uses int for sizes.
        {
            const auto& p = pluginTypes.getUnchecked(i); // Use modern access
            menu.addItem(3000 + i, p.name + " - " + p.pluginFormatName);
        }
    }
    else {
        menu.addItem(1, "Quit");
        menu.addSeparator();
        menu.addItem(2, "Delete Plugin States");
        #if !JUCE_MAC
            menu.addItem(3, "Invert Icon Color");
        #endif
    }
    
    #if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), 
                       ModalCallbackFunction::forComponent(menuInvocationCallback, this));
    #else
    if (x == 0 || y == 0)
    {
        POINT iconLocation;
        iconLocation.x = 0;
        iconLocation.y = 0;
        GetCursorPos(&iconLocation);
        x = iconLocation.x;
        y = iconLocation.y;
    }
    juce::Rectangle<int> rect(x, y, 1, 1);
    menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect), 
                       ModalCallbackFunction::forComponent(menuInvocationCallback, this));
    #endif
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    #if JUCE_MAC
        Process::setDockIconVisible(true);
    #endif
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    startTimer(50);
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    // Right click menu
    if (!im->menuIconLeftClicked)
    {
        if (id == 1)
        {
            im->savePluginStates();
            return JUCEApplication::getInstance()->quit();
        }
        if (id == 2)
        {
            im->deletePluginStates();
            return im->loadActivePlugins();
        }
        if (id == 3)
        {
            String color = getAppProperties().getUserSettings()->getValue("icon");
            getAppProperties().getUserSettings()->setValue("icon", color.equalsIgnoreCase("black") ? "white" : "black");
            return im->setIcon();
        }
    }
    
    #if JUCE_MAC
    // Hide icon if clicked elsewhere without active windows
    if (id == 0 && !PluginWindow::containsActiveWindows())
        Process::setDockIconVisible(false);
    #endif
    
    // Audio settings
    if (id == 1)
        im->showAudioSettings();
    
    // Plugin editor
    if (id == 2)
        im->reloadPlugins();
    
    // Other menu options
    if (id > 2)
    {
        // Delete plugin
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
            im->deletePluginStates();

            int index = id - im->INDEX_DELETE;
            std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            String key = getKey("order", timeSorted[index]);
            int unsortedIndex = 0;
            
            for (int i = 0; i < im->activePluginList.getNumTypes(); i++)
            {
                const auto& current = im->activePluginList.getTypes().getReference(i);
                if (key.equalsIgnoreCase(getKey("order", current)))
                {
                    unsortedIndex = i;
                    break;
                }
            }

            // Remove plugin data
            getAppProperties().getUserSettings()->removeValue(key);
            getAppProperties().getUserSettings()->removeValue(getKey("bypass", timeSorted[index]));
            getAppProperties().getUserSettings()->removeValue(getKey("state", timeSorted[index]));
            getAppProperties().saveIfNeeded();
            
            // Remove plugin from list
            const auto& pd = im->activePluginList.getTypes().getReference(unsortedIndex);
            im->activePluginList.removeType(pd);

            im->savePluginStates();
            im->loadActivePlugins();
        }
        // Add plugin (using a revised implementation)
        else if (id >= 3000 && id < 3000 + im->knownPluginList.getNumTypes())
        {
            int index = id - 3000;
            if (index >= 0 && index < im->knownPluginList.getNumTypes())
            {
                const auto& plugin = im->knownPluginList.getTypes().getReference(index);
                String key = getKey("order", plugin);
                int t = static_cast<int>(time(nullptr));
                getAppProperties().getUserSettings()->setValue(key, t);
                getAppProperties().saveIfNeeded();
                
                im->activePluginList.addType(plugin);
                im->savePluginStates();
                im->loadActivePlugins();
            }
        }
        // Bypass plugin
        else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
        {
            int index = id - im->INDEX_BYPASS;
            std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            String key = getKey("bypass", timeSorted[index]);

            // Toggle bypass flag
            bool bypassed = getAppProperties().getUserSettings()->getBoolValue(key);
            getAppProperties().getUserSettings()->setValue(key, !bypassed);
            getAppProperties().saveIfNeeded();

            im->savePluginStates();
            im->loadActivePlugins();
        }
        // Show active plugin GUI
        else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
            // Updated NodeID usage
            AudioProcessorGraph::NodeID nodeId(id - im->INDEX_EDIT + 1);
            if (const AudioProcessorGraph::Node::Ptr f = im->graph.getNodeForId(nodeId))
                if (auto w = PluginWindow::getWindowFor(f, PluginWindow::Normal))
                    w->toFront(true);
        }
        // Move plugin up the list
        else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_UP + 1000000)
        {
            im->savePluginStates();
            std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            PluginDescription toMove = timeSorted[id - im->INDEX_MOVE_UP];
            
            for (int i = 0; i < timeSorted.size(); i++)
            {
                bool move = getKey("move", toMove).equalsIgnoreCase(getKey("move", timeSorted[i]));
                getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i]), move ? i : i+1);
                if (move && i > 0)
                    getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i-1]), i+1);
            }
            getAppProperties().saveIfNeeded();
            im->loadActivePlugins();
        }
        // Move plugin down the list
        else if (id >= im->INDEX_MOVE_DOWN && id < im->INDEX_MOVE_DOWN + 1000000)
        {
            im->savePluginStates();
            std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
            PluginDescription toMove = timeSorted[id - im->INDEX_MOVE_DOWN];
            
            for (int i = 0; i < timeSorted.size(); i++)
            {
                bool move = getKey("move", toMove).equalsIgnoreCase(getKey("move", timeSorted[i]));
                getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i]), move ? i+2 : i+1);
                if (move && i+1 < timeSorted.size())
                {
                    getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i + 1]), i + 1);
                    i++;
                }
            }
            getAppProperties().saveIfNeeded();
            im->loadActivePlugins();
        }
        
        // Update menu
        im->startTimer(50);
    }
}

std::vector<PluginDescription> IconMenu::getTimeSortedList()
{
    int time = 0;
    std::vector<PluginDescription> list;
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
        list.push_back(getNextPluginOlderThanTime(time));
    return list;
}

String IconMenu::getKey(String type, PluginDescription plugin)
{
    return "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
}

void IconMenu::deletePluginStates()
{
    std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < list.size(); i++)
    {
        String pluginUid = getKey("state", list[i]);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
    }
    getAppProperties().saveIfNeeded();
}

void IconMenu::savePluginStates()
{
    std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < list.size(); i++)
    {
        AudioProcessorGraph::NodeID nodeId(i + 1);
        auto node = graph.getNodeForId(nodeId);
        if (node == nullptr)
            continue;
            
        AudioProcessor& processor = *node->getProcessor();
        String pluginUid = getKey("state", list[i]);
        MemoryBlock savedStateBinary;
        processor.getStateInformation(savedStateBinary);
        
        if (savedStateBinary.getSize() > 0)
            getAppProperties().getUserSettings()->setValue(pluginUid, savedStateBinary.toBase64Encoding());
    }
    getAppProperties().saveIfNeeded();
}

void IconMenu::showAudioSettings()
{
    AudioDeviceSelectorComponent audioSettingsComp(deviceManager, 0, 256, 0, 256, false, false, true, true);
    audioSettingsComp.setSize(500, 450);
    
    DialogWindow::LaunchOptions o;
    o.content.setNonOwned(&audioSettingsComp);
    o.dialogTitle                   = "Audio Settings";
    o.componentToCentreAround       = this;
    o.dialogBackgroundColour        = Colour::fromRGB(236, 236, 236);
    o.escapeKeyTriggersCloseButton  = true;
    o.useNativeTitleBar             = true;
    o.resizable                     = false;

    o.launchAsync();
        
    std::unique_ptr<XmlElement> audioState(deviceManager.createStateXml());
        
    getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState.get());
    getAppProperties().saveIfNeeded();
}

void IconMenu::reloadPlugins()
{
    if (pluginListWindow == nullptr)
        pluginListWindow = std::make_unique<PluginListWindow>(*this, formatManager);
    else
        pluginListWindow->toFront(true);
}

void IconMenu::removePluginsLackingInputOutput()
{
    std::vector<PluginDescription> pluginsToRemove;
    const auto& pluginTypes = knownPluginList.getTypes();
    for (int i = 0; i < pluginTypes.size(); ++i)
    {
        const auto& pluginRef = pluginTypes.getReference(i);
        if (pluginRef.numInputChannels < 2 || pluginRef.numOutputChannels < 2)
        pluginsToRemove.push_back(pluginRef);
    }
    
    // Remove plugins that don't have enough channels
    for (const auto& plugin : pluginsToRemove)
        knownPluginList.removeType(plugin);
}