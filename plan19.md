The docker build failed due to the overlayfs driver bug on the runner. Wait, the memory says: "If Docker builds fail due to an overlayfs driver bug on the runner ('failed to mount... err: invalid argument'), fall back to native host compilation by installing the required KF6/Qt6 dependencies directly via apt-get."
And:
"To set up the local development environment directly on a Debian/Ubuntu host for KF6 support, install dependencies via apt-get. If KF6 packages are missing on Ubuntu, add the Debian Trixie repository (`deb http://deb.debian.org/debian/ trixie main`), import keys (`6ED0E7B82643E131 78DBA3BC47EF2265 762F67A0B2C39DE4`), and use `apt-get install -t trixie -y qt6-base-dev qt6-svg-dev qt6-declarative-dev libkf6coreaddons-dev libkf6xmlgui-dev libkf6configwidgets-dev libkf6i18n-dev libkf6wallet-dev libsqlcipher-dev cmake pkg-config clang-format qt6-tools-dev qt6-tools-dev-tools libkf6notifications-dev libgl1-mesa-dev`."

Let's do that.
