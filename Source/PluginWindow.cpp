#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginWindow.hpp"

class PluginWindow;
static Array<PluginWindow*> activePluginWindows;
static CriticalSection activeWindowsLock;

PluginWindow::PluginWindow(Component* const pluginEditor,
                         AudioProcessorGraph::Node::Ptr node,
                         WindowFormatType t)
    : DocumentWindow(pluginEditor->getName(), Colours::lightgrey,
                     DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner(std::move(node)),
      type(t)
{
    setSize(400, 300);
    setUsingNativeTitleBar(true);
    setContentOwned(pluginEditor, true);

    const int defaultX = Random::getSystemRandom().nextInt(500);
    const int defaultY = Random::getSystemRandom().nextInt(500);
    
    setTopLeftPosition(owner->properties.getWithDefault(getLastXProp(type), defaultX),
                      owner->properties.getWithDefault(getLastYProp(type), defaultY));

    owner->properties.set(getOpenProp(type), true);

    setVisible(true);

    const ScopedLock sl(activeWindowsLock);
    activePluginWindows.add(this);
}

void PluginWindow::closeCurrentlyOpenWindowsFor(const AudioProcessorGraph::NodeID nodeId)
{
    const ScopedLock sl(activeWindowsLock);
    
    for (int i = activePluginWindows.size(); --i >= 0;)
    {
        if (activePluginWindows.getUnchecked(i)->owner->nodeID == nodeId)
        {
            delete activePluginWindows.getUnchecked(i);
            // Deletion will automatically remove from array via destructor
        }
    }
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    {
        const ScopedLock sl(activeWindowsLock);
        
        if (activePluginWindows.size() > 0)
        {
            for (int i = activePluginWindows.size(); --i >= 0;)
                delete activePluginWindows.getUnchecked(i);
        }
    }

    // Give windows a chance to close properly
    Component dummyModalComp;
    dummyModalComp.enterModalState();
    // Replace deprecated runDispatchLoopUntil with this:
    MessageManager::getInstance()->runDispatchLoop();
}

bool PluginWindow::containsActiveWindows()
{
    const ScopedLock sl(activeWindowsLock);
    return !activePluginWindows.isEmpty();
}

//==============================================================================
class ProcessorProgramPropertyComp : public PropertyComponent,
                                   private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp(const String& name, AudioProcessor& p, int index_)
        : PropertyComponent(name),
          owner(p),
          index(index_)
    {
        owner.addListener(this);
    }

    ~ProcessorProgramPropertyComp() override
    {
        owner.removeListener(this);
    }

    void refresh() override {}
    
    // Updated to match new signature
    void audioProcessorChanged(AudioProcessor*, const AudioProcessorListener::ChangeDetails&) override {}
    void audioProcessorParameterChanged(AudioProcessor*, int, float) override {}

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    explicit ProgramAudioProcessorEditor(AudioProcessor* const p)
        : AudioProcessorEditor(*p)  // Updated to use reference instead of pointer
    {
        jassert(p != nullptr);
        setOpaque(true);

        addAndMakeVisible(panel);

        Array<PropertyComponent*> programs;

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name(p->getProgramName(i).trim());

            if (name.isEmpty())
                name = "Unnamed";

            auto* pc = new ProcessorProgramPropertyComp(name, *p, i);
            programs.add(pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties(programs);

        setSize(400, jlimit(25, 400, totalHeight));
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colours::grey);
    }

    void resized() override
    {
        panel.setBounds(getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgramAudioProcessorEditor)
};

//==============================================================================
std::unique_ptr<PluginWindow> PluginWindow::getWindowFor(AudioProcessorGraph::Node::Ptr node,
                                      WindowFormatType type)
{
    jassert(node != nullptr);

    {
        const ScopedLock sl(activeWindowsLock);
        
        for (auto* window : activePluginWindows)
            if (window->owner == node && window->type == type)
                return nullptr; // Window already exists
    }

    AudioProcessor* processor = node->getProcessor();
    if (processor == nullptr)
        return nullptr;

    std::unique_ptr<AudioProcessorEditor> ui;

    if (type == Normal)
    {
        ui.reset(processor->createEditorIfNeeded());

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui.reset(new GenericAudioProcessorEditor(*processor));  // Use reference
        else if (type == Programs)
            ui.reset(new ProgramAudioProcessorEditor(processor));
    }

    if (ui != nullptr)
    {
        if (auto* plugin = dynamic_cast<AudioPluginInstance*>(processor))
            ui->setName(plugin->getName());

        return std::make_unique<PluginWindow>(ui.release(), node, type);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    const ScopedLock sl(activeWindowsLock);
    activePluginWindows.removeFirstMatchingValue(this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set(getLastXProp(type), getX());
    owner->properties.set(getLastYProp(type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set(getOpenProp(type), false);
    delete this;
}