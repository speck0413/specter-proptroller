# Setting up the SD card for the DFPlayer

The board's sound module (a DFPlayer Mini) plays sounds from a microSD card **by number**. Sound 1 is the file
`0001-….mp3` in the card's `mp3` folder, sound 2 is `0002-….mp3`, and so on. A script says `audio_single = 1`,
the settings say "Trigger sound 1", and a sketch says `playSound(1)`: the same file every time.

## Recommended: Studio's SD card tool

Studio's SD card tool erases, names and copies in the right order for you.

1. In Specter Studio, open **SD card** (from the board page's Sound tile, or from the library).
2. Add your sound files (mp3 or wav), or drag them in. Drag them into the order you want (or use the up and
   down buttons). The list shows the number each one gets: "1 · rain", "2 · elevator".
3. Pick the card. Leave **Erase and format FAT32** ticked, type the card's name to confirm, and press Copy.
4. Keep the list it shows at the end (**Copy list** or **Save list…**): it tells you which number is which sound.
5. Take the card out when Studio says it is safe, and put it in the sound module before you power the board.

**Check a card** reads a card that is already filled and tells you, in plain words, if a file is out of order,
a number is missing, or there are files the player would trip over. **Fix** copies them again in order.

## Doing it by hand

Why the order matters: the player numbers the files **in the order they were written to the card**, not by
their names. Studio's tool makes sure the two agree. By hand, you do it like this:

1. Delete everything on the card (or format it as FAT32: see below).
2. Make a folder named `mp3` in the card's top level (the root).
3. Copy the files into it **one at a time, in the order you want**, named `0001-name.mp3`, `0002-name.mp3`,
   `0003-name.mp3` … Always four digits, with the leading zeros. The player reads only the four digits; the rest
   of the name is for you (`0003-scream.mp3` is sound 3).
4. Keep no other files in the root or in the `mp3` folder.
5. On a Mac, remove the hidden files the Mac adds (the player would count them as sounds). In Terminal, with the
   card named CARD:

   ```
   dot_clean -m /Volumes/CARD
   find /Volumes/CARD -name '._*' -delete
   rm -rf /Volumes/CARD/.Spotlight-V100 /Volumes/CARD/.fseventsd /Volumes/CARD/.Trashes
   ```

6. Eject the card before you take it out.

**Windows:** copy the files one by one (one drag per file, waiting for each to finish), not all at once. To
format: right-click the card in File Explorer, Format, File system FAT32 (cards of 32 GB or less; Windows does
not offer FAT32 for bigger cards).

**Linux:** format with `mkfs.vfat -F 32 /dev/sdX1` (check the device name first: formatting the wrong disk erases
it), then copy one file at a time with `cp` and run `sync` before you eject.

## What the player reads

- A microSD card of 32 GB or less, formatted FAT32 (or FAT16).
- MP3 or WAV files, 8 to 48 kHz (MP3 up to 320 kbps).
- Up to 3000 sounds in the `mp3` folder.
- How loud: Settings → Sound (the trigger and ambient volumes), or `audio_volume = 60%` in a script.
