
Debian
====================
This directory contains files used to package piratecashd/piratecash-qt
for Debian-based Linux systems. If you compile piratecashd/piratecash-qt yourself, there are some useful files here.

## piratecash: URI support ##


piratecash-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install piratecash-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your piratecash-qt binary to `/usr/bin`
and the `../../share/pixmaps/piratecash128.png` to `/usr/share/pixmaps`

piratecash-qt.protocol (KDE)

