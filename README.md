# Visual Studio Code Registry Editor

This extension adds a registry editor to the explorer, and general support for .reg files.

![Main window screenshot](assets/readme.png)


Keys and Values have context menus allowing deletion, renaming, etc.

The 'Edit' option on keys generates a .reg file which, when saved, is automatically imported into the registry, updating the views accordingly.

## What's New

It seems administrators can lock people out of using reg.exe, even to query the registry. As a workaround I've added a setting for using an alternative executable, which has to be largely compatible with reg's command line options.

If you're wondering where on earth you could find such an executable, I supply one within this very extension!

I've added 'reg' as a proper language id to vscode, with syntax-highlighting, folding, and squiggles.
When editing reg files the context menu includes an option for locating a key or value in the registry view.

## More information

This extension is dependent only on the vscode API.

### Contribution Points

Exposes the following commands for inclusion in launch scripts, etc:

| Command | Parameters |
| :----: | :---: |
| regedit.delete    |(key or value) |
| regedit.rename    |(key or value, newname) |
| regedit.createKey |(parent, child) |
| regedit.setValue  |(key, name, type, value) |
| regedit.export    |(key, filename) |
| regedit.import    |(filename) |


## Author
Adrian Stephens

