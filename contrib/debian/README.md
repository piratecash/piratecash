
Debian
====================
This directory contains files used to package cosantad/cosanta-qt
for Debian-based Linux systems. If you compile cosantad/cosanta-qt yourself, there are some useful files here.

## cosanta: URI support ##


cosanta-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install cosanta-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your cosanta-qt binary to `/usr/bin`
and the `../../share/pixmaps/cosanta128.png` to `/usr/share/pixmaps`

cosanta-qt.protocol (KDE)

