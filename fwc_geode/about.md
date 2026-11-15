# FWC Window Counter

Counts the **frame window** of every input in a level by replaying your macro with the real game physics, and shows it in game.

## In game
- Top left: how many inputs fall in each window bin (17+, 13+, 9+, 7-8, 5-6, 4, 3, 2, 1).
- Bottom left: progress and raw precision (24h, sigma/s).
- A colored ring and the window number are left where each input happened.

## How to use
1. Put your macro in the mod config folder:
   - an Eclipse replay renamed to `macro.gdr2` (240 TPS), or
   - record one with this mod (**Record macro**).
2. Turn on **Play macro**.
3. Open the level, pause and press **FWC Count** (or turn on **Count windows in game**).
4. Optional: **Count at 8000 fps (CBF)** measures inside ticks (1/8000 s steps).
5. Optional: **Record video** saves `showcase.mp4` (needs `ffmpeg_path.txt` in the config folder).

## Notes
- Made for Geometry Dash 2.2081 with Geode.
- Windows wider than 20 frames are not shown.
- Do not play an Eclipse replay and this mod's macro at the same time.
- Between-tick inputs follow the idea of the Click Between Frames mod (syzzi, MIT license); the code here is written separately.
