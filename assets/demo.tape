# vhs tape for the mini-spire terminal-play demo GIF.
#   brew install vhs   (https://github.com/charmbracelet/vhs)
#   vhs assets/demo.tape   ->   writes assets/demo.gif
#
# Replays a complete winning fight on seed 0 (5 turns, ends at 65/80 HP). The
# keystrokes below are the local action indices the TUI prompts for, transcribed
# from a real playthrough (logs/...-seed0.jsonl) and verified to reach
# Outcome.Won. Deterministic because the seed fixes the fight, so this
# re-renders identically.
#
# Per-turn the local indices shift as the hand changes — they are not a simple
# pattern; see the per-line comments.

Output assets/demo.gif

# Terminal look — sized to fit the TUI panels; tweak Theme to taste.
Set FontSize 16
Set Width 1100
Set Height 700
Set Padding 20
Set Theme "Catppuccin Mocha"

# Launch the TUI on a fixed seed. (Run vhs from the repo root so `uv` resolves.)
Type "uv run minispire-play 0"
Enter
Sleep 2.5s

# --- Turn 1: Bash (Vulnerable) + Defend, then end turn ---
Type "3"
Sleep 400ms
Enter
Sleep 1.4s
Type "2"
Sleep 400ms
Enter
Sleep 1.4s
Type "0"
Sleep 400ms
Enter
Sleep 2s

# --- Turn 2: Strike, Strike, Defend, end turn ---
Type "2"
Sleep 400ms
Enter
Sleep 1.2s
Type "2"
Sleep 400ms
Enter
Sleep 1.2s
Type "0"
Sleep 400ms
Enter
Sleep 1.2s
Type "0"
Sleep 400ms
Enter
Sleep 2s

# --- Turn 3: Defend, Defend, Strike, end turn ---
Type "2"
Sleep 400ms
Enter
Sleep 1.2s
Type "2"
Sleep 400ms
Enter
Sleep 1.2s
Type "0"
Sleep 400ms
Enter
Sleep 1.2s
Type "0"
Sleep 400ms
Enter
Sleep 2s

# --- Turn 4: Bash, Defend, end turn ---
Type "0"
Sleep 400ms
Enter
Sleep 1.4s
Type "0"
Sleep 400ms
Enter
Sleep 1.4s
Type "0"
Sleep 400ms
Enter
Sleep 2s

# --- Turn 5: Strike, Bash -> lethal, fight won ---
Type "1"
Sleep 400ms
Enter
Sleep 1.4s
Type "3"
Sleep 400ms
Enter
Sleep 3s

# End screen, then quit.
Type "q"
Enter
Sleep 1s
