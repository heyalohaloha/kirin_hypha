#include "../src/HostExecutableIdentity.h"
#include <cstdlib>
#include <iostream>

namespace
{
void require (bool ok, const char* what)
{
    if (ok) return;
    std::cerr << "Host identity: " << what << '\n';
    std::exit (EXIT_FAILURE);
}
}

int main (int argc, char** argv)
{
    // Read-only inspection uses the production reader on an installed host.
    if (argc == 3 && juce::String (argv[1]) == "--inspect")
    {
        const juce::File executable (juce::String::fromUTF8 (argv[2]));
        const auto identity = hypha::host_identity::read (executable);
        std::cout << "name=" << identity.name << " version=" << identity.version
                  << " fileVersion=" << identity.fileVersion
                  << " oldFileVersion=" << executable.getVersion() << '\n';
        return identity.version.isNotEmpty() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    juce::TemporaryFile fixture;
    const auto directory = fixture.getFile();
    require (directory.createDirectory(), "create disposable fixture");
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup { directory };
    const auto absent = hypha::host_identity::read (directory.getChildFile ("absent"));
    require (absent.name.isEmpty() && absent.version.isEmpty(), "missing file fails closed");
   #if JUCE_MAC
    const auto app = directory.getChildFile ("Renamed Application.app");
    const auto executable = app.getChildFile ("Contents/MacOS/Studio Pro");
    require (executable.getParentDirectory().createDirectory(), "create macOS bundle layout");
    require (executable.replaceWithText ("fixture"), "create executable fixture");
    require (app.getChildFile ("Contents/Info.plist").replaceWithText (
        "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
        "<key>CFBundleIdentifier</key><string>test.hypha.identity</string>"
        "<key>CFBundleExecutable</key><string>Studio Pro</string>"
        "<key>CFBundleShortVersionString</key><string>8.1.2 Build 113407</string>"
        "</dict></plist>"), "create native version metadata");
    require (executable.getVersion().isEmpty(), "old executable reader misses app version");
    const auto identity = hypha::host_identity::read (executable);
    require (identity.name == "Studio Pro" && identity.version == "8.1.2 Build 113407"
                 && identity.fileVersion == identity.version,
             "read containing bundle version without using its renamed folder name");
    const auto plain = directory.getChildFile ("Studio Pro");
    require (plain.replaceWithText ("fixture"), "create unbundled fixture");
    require (hypha::host_identity::read (plain).version.isEmpty(), "no unrelated bundle fallback");
    const auto unversioned = directory.getChildFile ("Unversioned.app/Contents/MacOS/Studio Pro");
    require (unversioned.getParentDirectory().createDirectory(), "create unversioned bundle");
    require (unversioned.replaceWithText ("fixture"), "create unversioned executable");
    require (hypha::host_identity::read (unversioned).version.isEmpty(),
             "missing bundle metadata cannot borrow a different app's identity");
   #elif JUCE_WINDOWS
    const auto executable = directory.getChildFile ("Studio Pro.exe");
    require (juce::File::getSpecialLocation (juce::File::hostApplicationPath).copyFileTo (executable),
             "copy resource-bearing fixture into disposable directory");
    require (executable.getVersion() == "8.1.2.0", "fixed WORD version reproduces missing build");
    const auto identity = hypha::host_identity::read (executable);
    require (identity.name == "Studio Pro" && identity.version == "8.1.2.113407"
                 && identity.fileVersion == "8.1.2.0",
             "read exact build from native FileVersion string resource");
    const auto plain = directory.getChildFile ("no-resource.exe");
    require (plain.replaceWithText ("fixture"), "create invalid version resource");
    require (hypha::host_identity::read (plain).version.isEmpty(), "no fixed-version fallback");
   #endif
    std::cout << "Host identity native boundary PASS\n";
}
