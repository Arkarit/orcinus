/** Set whether to build Dolphin with support for these technologies or not. */
#cmakedefine01 HAVE_BALOO
#cmakedefine01 HAVE_KUSERFEEDBACK
#cmakedefine01 HAVE_PACKAGEKIT
#cmakedefine01 HAVE_TERMINAL
#cmakedefine01 HAVE_X11

#define WHALE_EXECUTABLE "@WHALE_EXECUTABLE@"
#define WHALE_COMPONENT "@WHALE_COMPONENT@"
#define WHALE_ORG_DOMAIN "@WHALE_ORG_DOMAIN@"
#define WHALE_APP_ID "@WHALE_APP_ID@"
#define WHALE_DISPLAY_NAME "@WHALE_DISPLAY_NAME@"
#define WHALE_BUG_URL "@WHALE_BUG_URL@"
#define WHALE_HOMEPAGE "@WHALE_HOMEPAGE@"
#cmakedefine01 WHALE_PROVIDE_FILEMANAGER1
#cmakedefine01 WHALE_DEV_BUILD

#define KDE_INSTALL_FULL_DATADIR "${KDE_INSTALL_FULL_DATADIR}"

/** The name of the package that needs to be installed so URLs starting with "admin:" can be opened in Dolphin. */
#cmakedefine ADMIN_WORKER_PACKAGE_NAME "@ADMIN_WORKER_PACKAGE_NAME@"

/** The name of the KDE Filelight package. */
#cmakedefine FILELIGHT_PACKAGE_NAME "@FILELIGHT_PACKAGE_NAME@"

/** The name of the KFind package. */
#cmakedefine KFIND_PACKAGE_NAME "@KFIND_PACKAGE_NAME@"
