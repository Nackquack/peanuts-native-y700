# Hellblade: Senua's Sacrifice (010044500CF8E000): the rewriter's patches for this game.
# dm/tools/patch_nso.py loads every profile's patches.py and applies those of a module's NSO build ID.
CUTS = {
  # Hellblade: Senua's Sacrifice 1.0.0 (v0) main
  '052fcb358955ed489217313a058d7a29': [
  ],
}

BINDING = [
  {'what': 'Unreal Engine 4 frame rate and dynamic resolution scaling uncap', 'now': 'profile', 'goal': 'game', 'purpose': 'frame rate',
   'why': 'Uncaps 30 FPS limit to rock-solid 60 FPS on Snapdragon 8 Elite and locks DRS to native 1080p target'}
]
