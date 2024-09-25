# Change Log

All notable changes to the "registry-editor" extension will be documented in this file.

Check [Keep a Changelog](http://keepachangelog.com/) for recommendations on how to structure this file.

## [Unreleased]

- Release 1

## [0.5.0]
### Added
- Search
- qwords that (seem to) represent FILETIMEs are displayed as such

## [0.4.1]
## Fixed
- reg bugs

## [0.4.0]
### Added
- Replacement REG executable
    - option to use the replacement or standard reg.exe
- reg language:
    - Syntax highlighting for reg files
    - Editor folding
    - Editor diagnostics
    - context menu option to locate a key or value in the Registry view
### Fixed
- Displays values with any type


## [0.3.0]
### Added
- Icons and a two-letter 'badge' to help identify the different value types (REG_SZ, REG_DWORD, etc).
- Remote Registry support.
- Copy context command to copy keys and values to the clipboard in a human-readable format.\
    Copy (strict) (available by holding ALT when right-clicking an item) copies values in a format compatible with Windows Registry Editor Version 5.00. This means things like REG_MULTI_SZ are long hex strings.

## [0.2.0]
## [0.1.0]
- Initial release
