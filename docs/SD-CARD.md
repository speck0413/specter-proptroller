# Setting up the SD card for the DFPlayer

The board's sound module (a DFPlayer Mini) plays sounds from a microSD card **by number**, one sound per folder.
Sound 1 is the one file in the folder `01`, sound 2 the one in folder `02`, and so on up to `99`. A script says
`audio_single = 1`, the settings say "Trigger sound 1", and a sketch says `playSound(1)`: the same file every time.

```
01/001-ambient.mp3     sound 1
02/001-scream.mp3      sound 2
03/001-thunder.mp3     sound 3
```

## Recommended: Studio's SD card tool

Studio's SD card tool erases the card, makes the folders and names the files for you.

1. In Specter Studio, open **SD card** (from the board page's Sound tile, or from the library).
2. Add your sound files (mp3 or wav), or drag them in. Put them in the order you want (drag them, or use the up
   and down buttons). The list shows the number each one gets: "1 · ambient", "2 · scream".
3. Pick the card. Leave **Erase and format FAT32** ticked, type the card's name to confirm, and press Copy.
4. Keep the list it shows at the end (**Copy list** or **Save list…**): it tells you which number is which sound.
5. Take the card out when Studio says it is safe, and put it in the sound module before you power the board.

**Check a card** reads a card that is already filled and tells you, in plain words, if a folder is missing a
file, a file is not named `001-…`, or there are files the player would trip over. **Fix** sets it right.

## Doing it by hand

1. Start with an empty card (or format it as FAT32: see below).
2. Make one folder per sound in the card's top level (the root), named with two digits: `01`, `02`, `03` …
   up to `99`.
3. Put one file in each folder, named `001-<description>.mp3`: three digits first, then anything you like
   (`01/001-ambient.mp3`, `02/001-scream.mp3`). The player reads only the digits; the rest of the name is for you.
   The order you copy them in does not matter.
4. Keep nothing else in the root.
5. On a Mac, remove the hidden files the Mac adds (the player can trip over them). In Terminal, with the card
   named CARD:

   ```
   dot_clean -m /Volumes/CARD
   find /Volumes/CARD -name '._*' -delete
   rm -rf /Volumes/CARD/.Spotlight-V100 /Volumes/CARD/.fseventsd /Volumes/CARD/.Trashes
   ```

6. Eject the card before you take it out.

If a sound does not play on a player that is not a genuine DFPlayer Mini, rename its file to plain `001.mp3`
(the datasheet allows any text after the three digits, but a few copies of the module may not).

**Windows:** to format, right-click the card in File Explorer, Format, File system FAT32 (cards of 32 GB or less;
Windows does not offer FAT32 for bigger cards).

**Linux:** format with `mkfs.vfat -F 32 /dev/sdX1` (check the device name first: formatting the wrong disk erases
it), and run `sync` before you eject.

## What the player reads

- A microSD card of 32 GB or less, formatted FAT32 (or FAT16).
- MP3 or WAV files, 8 to 48 kHz (MP3 up to 320 kbps).
- Up to 99 sounds: folders `01` to `99`, one file each.
- How loud: Settings → Sound (the trigger and ambient volumes), or `audio_volume = 60%` in a script.
