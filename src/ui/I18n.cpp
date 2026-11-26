#include "ui/I18n.h"
#include "engine/SafeWrite.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/Inflate.h"
#include "engine/PluginHost.h"
#include "ui/I18nData.h"
#include "ui/I18nEnglish.h"
#include "ui/I18nLocale.h"

namespace lynx
{
namespace
{
juce::String& currentId()
{
    static juce::String id { "en" };
    return id;
}

juce::File settingsFile()
{
    return PluginHost::settingsFolder().getChildFile ("language.txt");
}

bool isKnown (const juce::String& id)
{
    for (const auto& l : i18n::languages())
        if (id == l.id)
            return true;
    return false;
}

// Round 190ri (paint gate): juce::LocalisedStrings finds a key by a linear search through its table, on every tr() — some
// in paint code. The current table's keys in a hash map (the same keys, the same texts: LocalisedStrings is made with
// case-sensitive keys and keeps the last of a repeated key, as the map is filled from it); juce::translate itself still
// has the table for any direct caller.
struct StringHash
{
    std::size_t operator() (const juce::String& s) const noexcept { return s.hash(); }
};
using TextMap = std::unordered_map<juce::String, juce::String, StringHash>;

std::atomic<unsigned>& textGeneration() // (bumped when the language's texts change: the per-thread caches start again)
{
    static std::atomic<unsigned> generation { 0 };
    return generation;
}

std::shared_ptr<const TextMap>& textMapSlot()
{
    static std::shared_ptr<const TextMap> map;
    return map;
}

std::shared_ptr<const TextMap> textMap()
{
    return std::atomic_load (&textMapSlot());
}

juce::String translated (const juce::String& english)
{
    if (const auto map = textMap())
    {
        const auto it = map->find (english);
        return it != map->end() ? it->second : english;
    }
    return english; // (no table: English as written)
}
} // namespace

juce::String tr (const juce::String& english)
{
    return translated (english);
}

juce::String tr (const char* englishUtf8)
{
    // Nearly every call passes a string literal: its text, per thread, by its address (checked against a copy of the key,
    // so a reused buffer cannot give another text), without the UTF-8 conversion and the lookup of every call
    struct Cache
    {
        unsigned generation = ~0u;
        std::unordered_map<const char*, std::pair<std::string, juce::String>> texts;
    };
    if (englishUtf8 == nullptr) // (some callers pass a null text: an empty one, as juce::String::fromUTF8 gave)
        return {};
    thread_local Cache cache;
    const auto generation = textGeneration().load (std::memory_order_acquire);
    if (cache.generation != generation || cache.texts.size() > 60000)
    {
        cache.texts.clear();
        cache.generation = generation;
    }
    if (const auto it = cache.texts.find (englishUtf8); it != cache.texts.end() && std::strcmp (it->second.first.c_str(), englishUtf8) == 0)
        return it->second.second;
    auto text = translated (juce::String::fromUTF8 (englishUtf8));
    cache.texts.insert_or_assign (englishUtf8, std::pair<std::string, juce::String> { std::string (englishUtf8), text });
    return text;
}

juce::String trc (const char* contextAndEnglishUtf8)
{
    const auto key = juce::String::fromUTF8 (contextAndEnglishUtf8);
    const auto text = translated (key);
    if (text != key || ! key.startsWithChar ('@'))
        return text;
    return key.fromFirstOccurrenceOf (": ", false, false);
}

juce::String tr (const char* englishUtf8, std::initializer_list<juce::String> args)
{
    auto text = tr (englishUtf8);
    int i = 0;
    for (const auto& a : args)
        text = text.replace ("{" + juce::String (i++) + "}", a);
    return text;
}

namespace i18n
{
const std::vector<Language>& languages()
{
    // English first, then alphabetical by English name; each language in its own name (UTF-8)
    static const std::vector<Language> list {
        { "en", "English" },
        { "zh-Hans", "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87" },             // 简体中文
        { "zh-Hant", "\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87" },             // 繁體中文
        { "nl", "Nederlands" },
        { "fr", "Fran\xc3\xa7" "ais" },
        { "de", "Deutsch" },
        { "id", "Bahasa Indonesia" },
        { "it", "Italiano" },
        { "ja", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e" },                                 // 日本語
        { "ko", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4" },                                 // 한국어
        { "pl", "Polski" },
        { "pt-BR", "Portugu\xc3\xaas (Brasil)" },
        { "ru", "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9" },        // Русский
        { "es", "Espa\xc3\xb1ol" },
        { "tr", "T\xc3\xbcrk\xc3\xa7" "e" },
    };
    // only the languages whose table is embedded (a table lands together with its entry)
    static const std::vector<Language> available = [] {
        std::vector<Language> out;
        for (const auto& l : list)
            if (juce::String (l.id) == "en" || hasTable (l.id))
                out.push_back (l);
        return out;
    }();
    return available;
}

bool isNewTranslation (const juce::String& id)
{
    return id != "en" && id != "pt-BR";
}

juce::String fromLocale (const juce::String& localeName)
{
    std::vector<std::string> ids;
    for (const auto& l : languages())
        ids.emplace_back (l.id);
    return juce::String (languageForLocale (localeName.toStdString(), ids));
}

juce::String osDefault()
{
    // Windows: the display language ("de-DE", "zh-TW"); macOS: the first preferred language ("zh-Hans-CN");
    // Linux: LANGUAGE / LC_ALL / LC_MESSAGES / LANG ("ja_JP.UTF-8")
   #if JUCE_LINUX || JUCE_BSD
    for (const char* var : { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" })
        if (const auto value = juce::SystemStats::getEnvironmentVariable (var, {}); value.isNotEmpty() && value != "C" && value != "POSIX")
        {
            // LANGUAGE may be a list ("de:en"): its first entry
            const auto id = fromLocale (value);
            return id.isNotEmpty() ? id : juce::String ("en");
        }
   #endif
    for (const auto& name : { juce::SystemStats::getDisplayLanguage(),
                              juce::SystemStats::getUserLanguage() + "-" + juce::SystemStats::getUserRegion() })
        if (const auto id = fromLocale (name); id.isNotEmpty())
            return id;
    return "en";
}

juce::String current()
{
    return currentId();
}

juce::String saved()
{
    const auto id = settingsFile().loadFileAsString().trim();
    return isKnown (id) ? id : juce::String ("en");
}

void save (const juce::String& id)
{
    if (isKnown (id))
        safewrite::replaceWithText (settingsFile(), id);
}

bool hasTable (const juce::String& id)
{
    for (const auto& t : data::tables)
        if (id == t.id)
            return true;
    return false;
}

juce::String tableText (const juce::String& id)
{
    for (const auto& t : data::tables)
        if (id == t.id)
            if (const auto text = inflateZlib (t.zlib, t.zlibSize, false, t.textSize + 16); text && text->size() == t.textSize)
                return juce::String::fromUTF8 ((const char*) text->data(), (int) text->size());
    return {};
}

void initialise (const juce::StringArray& args)
{
    auto id = saved();
    const auto flag = args.indexOf ("--lang");

    if (flag >= 0 && isKnown (args[flag + 1].unquoted()))
        id = args[flag + 1].unquoted();
    else if (! settingsFile().existsAsFile() && ! PluginHost::settingsFolder().getChildFile ("setup-done.txt").existsAsFile()
             && juce::SystemStats::getEnvironmentVariable ("LYNX_TEST_MODE", {}) != "1"
             && ! std::any_of (args.begin(), args.end(), [] (const juce::String& a) { return a.startsWith ("--"); }))
    {
        // the very first start of the app (no language chosen, setup wizard never finished): the OS language when
        // LYNX has it; the wizard's Language box starts there and the choice is kept
        id = osDefault();
        save (id);
    }

    currentId() = id;
    // English has no translation table; its display table (resources/i18n/en/display.txt) changes what a few keys show
    const auto table = id == "en" ? juce::String::fromUTF8 (data::englishDisplay) : tableText (id);
    auto* strings = table.isEmpty() ? nullptr : new juce::LocalisedStrings (table, false);
    std::shared_ptr<const TextMap> map;
    if (strings != nullptr)
    {
        auto fresh = std::make_shared<TextMap>();
        const auto& pairs = strings->getMappings();
        fresh->reserve ((std::size_t) pairs.size());
        for (int i = 0; i < pairs.size(); ++i)
            (*fresh)[pairs.getAllKeys()[i]] = pairs.getAllValues()[i];
        map = std::move (fresh);
    }
    std::atomic_store (&textMapSlot(), map);
    textGeneration().fetch_add (1, std::memory_order_release);
    juce::LocalisedStrings::setCurrentMappings (strings);
}
} // namespace i18n
} // namespace lynx
