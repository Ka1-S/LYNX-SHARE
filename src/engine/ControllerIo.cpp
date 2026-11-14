#include "engine/ControllerIo.h"

#include <algorithm>

namespace lynx
{
ControllerIo::ControllerIo() : profiles (controller::Library::builtIn()) {}

ControllerIo::~ControllerIo() { setSurface (nullptr); }

void ControllerIo::loadProfiles (const juce::File& userFolder)
{
    profiles = controller::Library::builtIn();
    if (userFolder.isDirectory())
    {
        auto files = userFolder.findChildFiles (juce::File::findFiles, false, "*.lynxctl");
        files.sort();
        for (const auto& f : files)
            if (f.getSize() < 1024 * 1024) // (a profile is a few kilobytes)
                profiles.add (f.loadFileAsString().toStdString(), f.getFileNameWithoutExtension().toStdString(), true);
    }
    // the sessions hold their profiles; the next configure() moves the devices to the new ones
    closeSessions();
}

void ControllerIo::closeSessions()
{
    hub.closeAll ([this] (const std::string& input, const std::uint8_t* bytes, std::size_t size) { send (input, bytes, size); });
    {
        const juce::SpinLock::ScopedLockType lock (claimLock);
        claimed.clear();
        ++generation;
    }
    anyClaimed.store (false);
    wanted.clear();
}

void ControllerIo::setSurface (controller::Surface* s)
{
    if (surface == s)
        return;
    if (s == nullptr)
    {
        closeSessions();
        ports.clear();
        surface = nullptr;
        return;
    }
    surface = s;
    apply (true);
}

void ControllerIo::configure (const prefs::MidiPrefs& prefs, const std::vector<std::string>& inputNames, bool openOutputs)
{
    std::vector<controller::Hub::Device> devices;
    for (const auto& name : inputNames)
    {
        const auto settings = prefs.input (name);
        if (! settings.enabled)
            continue;
        if (auto profile = profiles.resolve (settings.controller, name))
            devices.push_back ({ name, std::move (profile) });
    }
    const auto sameOutputs = wantOutputs == openOutputs && (devices.empty() || ! openOutputs
                                                            || std::all_of (devices.begin(), devices.end(), [this] (const controller::Hub::Device& d) {
                                                                   const auto it = ports.find (d.input);
                                                                   return it != ports.end() && it->second.device != nullptr;
                                                               }));
    const auto sameDevices = devices == wanted;
    if (sameDevices && sameOutputs)
        return; // (called every two seconds with the device list: nothing changed)
    wanted = std::move (devices);
    wantOutputs = openOutputs;
    apply (! sameDevices);
}

void ControllerIo::apply (bool devicesChanged)
{
    if (surface == nullptr)
        return; // (the main window is not there yet: setSurface applies)

    // the output ports first, so a new session's opening messages reach its device
    for (auto it = ports.begin(); it != ports.end();)
        it = std::any_of (wanted.begin(), wanted.end(), [&] (const controller::Hub::Device& d) { return d.input == it->first; }) ? std::next (it)
                                                                                                                               : ports.erase (it);
    if (wantOutputs && ! wanted.empty())
    {
        const auto available = juce::MidiOutput::getAvailableDevices();
        std::vector<std::string> names;
        for (const auto& d : available)
            names.push_back (d.name.toStdString());
        for (const auto& d : wanted)
        {
            auto& port = ports[d.input];
            const auto name = controller::pairedOutput (d.input, names);
            const auto listed = std::any_of (available.begin(), available.end(), [&port] (const juce::MidiDeviceInfo& info) {
                return info.identifier.toStdString() == port.id;
            });
            if (port.device != nullptr && port.name == name && listed)
                continue;
            port.device.reset();
            port.id.clear();
            port.name = name;
            if (! name.empty())
                for (const auto& info : available)
                    if (info.name.toStdString() == name)
                    {
                        port.device = juce::MidiOutput::openDevice (info.identifier); // (null when another program holds it)
                        port.id = info.identifier.toStdString();
                        break;
                    }
        }
    }

    hub.configure (wanted, *surface, [this] (const std::string& input, const std::uint8_t* bytes, std::size_t size) { send (input, bytes, size); });

    if (! devicesChanged)
        return; // (only an output port was missing and was looked for again)
    std::vector<Claimed> next;
    for (const auto& d : wanted)
        if (const auto* session = hub.session (d.input))
            next.push_back ({ d.input, controller::Claims (session->profile()) });
    {
        const juce::SpinLock::ScopedLockType lock (claimLock);
        claimed.swap (next);
        ++generation;
    }
    anyClaimed.store (! wanted.empty());
}

std::shared_ptr<const controller::Profile> ControllerIo::profileOf (const std::string& input) const
{
    for (const auto& d : wanted)
        if (d.input == input)
            return d.profile;
    return nullptr;
}

std::string ControllerIo::outputOf (const std::string& input) const
{
    const auto it = ports.find (input);
    return it != ports.end() && it->second.device != nullptr ? it->second.name : std::string();
}

void ControllerIo::send (const std::string& input, const std::uint8_t* bytes, std::size_t size)
{
    if (onSend)
    {
        onSend (input, bytes, size);
        return;
    }
    const auto it = ports.find (input);
    if (it != ports.end() && it->second.device != nullptr && size > 0)
        it->second.device->sendMessageNow (juce::MidiMessage (bytes, (int) size));
}

bool ControllerIo::take (const std::string& device, const std::uint8_t* bytes, int size, const std::array<std::atomic<std::uint64_t>, 2>& linkedCcs) noexcept
{
    if (! anyClaimed.load (std::memory_order_relaxed) || bytes == nullptr || size < 2 || size > 3)
        return false;
    Event e;
    {
        const juce::SpinLock::ScopedLockType lock (claimLock);
        auto index = -1;
        for (std::size_t i = 0; i < claimed.size(); ++i)
            if (claimed[i].input == device)
            {
                // (a controller linked by hand keeps its link and plays as before)
                if (claimed[i].claims.takes (bytes, (std::size_t) size, linkedCcs[0].load (std::memory_order_relaxed),
                                             linkedCcs[1].load (std::memory_order_relaxed)))
                    index = (int) i;
                break;
            }
        if (index < 0)
            return false;
        e.generation = generation;
        e.device = (std::uint8_t) index;
    }
    e.size = (std::uint8_t) size;
    for (int i = 0; i < size; ++i)
        e.bytes[i] = bytes[i];
    queue.push (e);
    return true;
}

void ControllerIo::pump (bool learning)
{
    Event e;
    if (surface == nullptr)
    {
        while (queue.pop (e)) {}
        return;
    }
    auto began = false;
    while (queue.pop (e))
    {
        if (e.generation != generation || e.device >= wanted.size())
            continue; // (queued for a device list that is gone)
        if (learning && (e.bytes[0] & 0xF0) == 0xB0)
            continue; // (this move made a link)
        if (! began)
        {
            surface->beginBatch();
            began = true;
        }
        hub.input (wanted[e.device].input, e.bytes, e.size, *surface);
    }
    if (began)
        surface->endBatch();
    if (! wanted.empty())
        hub.refresh (*surface, [this] (const std::string& input, const std::uint8_t* bytes, std::size_t size) { send (input, bytes, size); });
}
} // namespace lynx
