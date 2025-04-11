//
//  IconMenu.hpp
//  SoftHost
//
//  Created by Rolando Islas on 12/26/15.
//  Updated and optimized on 04/10/25.
//

#ifndef IconMenu_hpp
#define IconMenu_hpp

ApplicationProperties& getAppProperties();

class IconMenu : public SystemTrayIconComponent, private Timer, public ChangeListener
{
public:
    IconMenu();
    ~IconMenu();
    void mouseDown(const MouseEvent&);
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed) override;
    static String getKey(String type, PluginDescription plugin);
    void removePluginsLackingInputOutput();

    const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE, INDEX_MOVE_UP, INDEX_MOVE_DOWN;
    
private:
    #if JUCE_MAC
    std::string exec(const char* cmd);
    #endif
    void timerCallback() override;
    void reloadPlugins();
    void showAudioSettings();
    void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
    PluginDescription getNextPluginOlderThanTime(int &time);
    std::vector<PluginDescription> getTimeSortedList();
    void setIcon();
    
    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList activePluginList;
    KnownPluginList::SortMethod pluginSortMethod;
    PopupMenu menu;
    bool menuIconLeftClicked = false;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioProcessorGraph::Node* inputNode = nullptr;
    AudioProcessorGraph::Node* outputNode = nullptr;
    #if JUCE_WINDOWS
    int x = 0, y = 0;
    #endif

    class PluginListWindow;
    ScopedPointer<PluginListWindow> pluginListWindow;
};

#endif /* IconMenu_hpp */