# Orcinus

Orcinus is a file manager based on KDE Dolphin. This fork keeps Dolphin's
upstream history, architecture, authorship, and licensing while giving the
application an independent identity and room for additional features.

> Orcinus is an unofficial fork. It is not affiliated with, endorsed by, or
> supported by KDE e.V. or the Dolphin developers. Please report problems with
> Orcinus to the Orcinus issue tracker, not to KDE. KDE® is a registered
> trademark of KDE e.V.

Orcinus was developed under the working title Whale. Source directories, class
names, CMake variables and action names still use that name; everything that is
installed or shown to users says Orcinus.

Orcinus is currently intended to coexist with the distribution-provided Dolphin.
It uses its own executable, application ID, configuration, session state,
private library, desktop entry, and translation domain. Shared KDE facilities
such as places, service menus, trash, and per-folder view properties remain
shared intentionally.

## Building Orcinus

Use a build and install prefix separate from any Dolphin development build:

```bash
cmake -S . -B ../build-orcinus -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON \
    -DCMAKE_INSTALL_PREFIX="$PWD/../install-orcinus"
cmake --build ../build-orcinus
../build-orcinus/bin/orcinus --new-window
```

The safe defaults do not claim `org.freedesktop.FileManager1` and do not install
Dolphin-named KParts, KCMs, plugins, documentation, or developer files. The
optional FileManager1 integration can be enabled with
`-DWHALE_PROVIDE_FILEMANAGER1=ON`.

Orcinus does not need a Dolphin package to run. It installs a private copy of
Dolphin's small version-control plugin library (`libdolphinvcs.so.6`, under
`lib/orcinus/`), so the version-control plugins of `dolphin-plugins` work when
they are installed. The view plugin API (`liborcinusviewapi`, same directory) is
internal for now: there is no stable interface for third-party plugins yet. Service-menu integration and the handbook are Dolphin's and
are used when present. If a matching optional BalooWidgets package is
unavailable, the information panel is disabled but Orcinus can still be built.

Orcinus issues belong in the [Orcinus issue tracker](https://github.com/Arkarit/orcinus/issues).
Issues reproducible in unmodified Dolphin should be reported upstream.

## Upstream Dolphin

Dolphin is KDE's file manager that lets you navigate and browse the contents of your hard drives, USB sticks, SD cards, and more. Creating, moving, or deleting files and folders is simple and fast. See more information [on Dolphin's homepage](https://apps.kde.org/dolphin/).

![Screenshot](https://cdn.kde.org/screenshots/dolphin/dolphin.png)

## User Documentation

Orcinus behaves like Dolphin except where
[docs/orcinus-vs-dolphin.md](docs/orcinus-vs-dolphin.md) says otherwise; for
everything else see Dolphin's documentation at
https://userbase.kde.org/Special:myLanguage/Dolphin.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). In short: problems and ideas for Orcinus
go to the [Orcinus issue tracker](https://github.com/Arkarit/orcinus/issues).
Problems that also happen in an unmodified Dolphin belong to KDE: see
[KDE Invent](https://invent.kde.org/system/dolphin) and the
[KDE Bugtracking System](https://bugs.kde.org/enter_bug.cgi?format=guided&product=dolphin).
Security issues: see [SECURITY.md](SECURITY.md).

## Development Philosophy

This section is Dolphin's and applies to Orcinus as well.

Dolphin is a file manager focusing on usability. When reading the term Usability people often assume that the focus is on newbies and only basic features are offered. This is not the case; Dolphin is quite full-featured, but the features are carefully chosen so as to not impede any of the users in the target user groups.

### Target User Groups

Focusing on usability means that features are discoverable and efficient to use. The feature set is defined indirectly by the target user group of Dolphin:

- **Lisa**: Lisa has been familiar with computers for 10 years. From her job, she has experience with Word, Excel and Outlook. At home she mainly uses the computer for browsing the web and writing e-mails. She requires a file manager for managing photos from the camera, documents she gets via e-mail, or PDFs she downloads with a browser. Lisa knows concepts like folders and a file hierarchy, but she is not familiar with the file hierarchy of Linux.

- **Simon**: Simon has been a developer at a software company for 8 years. At home he uses a file manager to maintain his large collection of photos and music. Additionally he owns a small homepage and needs to transfer updated files on the FTP server. Moving and copying files are regular tasks in Simon's workflow.

Not part of the target user group of Dolphin are Fred and Jeff:

- **Fred**: Fred is 75 years old and is able to write e-mails and browsing the web. He is not familiar with file hierarchies and stores all his documents on the desktop.

- **Jeff**: Jeff is Linux-freak since the age of 16 a few years ago. He is a developer and in his spare time he acts as administrator for a small company. Jeff has two monitors to keep the overview about his huge number of opened applications.

This does not mean that Fred or Jeff cannot work with Dolphin. But there might be features and concepts of Dolphin that overburden Fred. Also Jeff might miss some features which are a must-have for his daily work. This is acceptable; there are other tools that cater specifically to their needs.

### Non-Intrusive Features

Before a feature is added in Dolphin, check whether the feature is mandatory for the target user group. If this is not the case, then this does not mean that the feature cannot be added; first it must be clarified whether the feature might be non-intrusive, so that it adds value for users outside the primary target user group of Dolphin. The term "non-intrusive" is mainly related to the user interface. A feature that adds a lot of clutter to the main menu, context menus or toolbar might harm the target user group. In this case the feature should not be added.

A good example of a feature that is non-intrusive is the embedded terminal in Dolphin. It only requires one entry inside a sub-menu, but adds great value for Jeff, who is not part of the target user group.

### Options

Options are mandatory as the "average Joe" user does not exist. Still it is not the goal of Dolphin to offer options for all kind of things. Again the focus is on the possible needs of the target user group. Each additional option makes it harder finding other options, so the same rules for features are applied to options too.
