# vhs tape for the mini-spire terminal-play demo GIF.
#   brew install vhs   (https://github.com/charmbracelet/vhs)
#   vhs assets/demo.tape   ->   writes assets/gameplay.gif
#
# Replays a complete winning fight on seed 0 (5 turns, ends at 65/80 HP). The
# keystrokes below are the local action indices the TUI prompts for, transcribed
# from a real playthrough (logs/...-seed0.jsonl) and verified to reach
# Outcome.Won. Deterministic because the seed fixes the fight, so this
# re-renders identically.
#
# Pacing notes:
#   - Local indices shift as the hand changes; once energy is spent the hand
#     collapses and "end turn" becomes local index 0. We pause BEFORE sending
#     that 0 so the screen has re-rendered to the "(0) end turn" prompt, and
#     pause AFTER so the enemy's attack + the new turn are visible.
#   - The end screen says "(press enter to exit)" — so we send Enter, not q.

Output assets/gameplay.gif

Set FontSize 16
Set Width 1100
Set Height 700
Set Padding 20
Set Theme "Catppuccin Mocha"

Type "uv run minispire-play 0"
Enter
Sleep 1.5s

# --- Turn 1: Bash (Vulnerable), Defend, end turn ---
Type "3"
Sleep 250ms
Enter
Sleep 900ms
Type "2"
Sleep 250ms
Enter
Sleep 1s            # let the hand collapse to "(0) end turn"
Type "0"
Sleep 250ms
Enter
Sleep 1.6s          # show enemy attack + new turn

# --- Turn 2: Strike, Strike, Defend, end turn ---
Type "2"
Sleep 250ms
Enter
Sleep 800ms
Type "2"
Sleep 250ms
Enter
Sleep 800ms
Type "0"
Sleep 250ms
Enter
Sleep 1s
Type "0"
Sleep 250ms
Enter
Sleep 1.6s

# --- Turn 3: Defend, Defend, Strike, end turn ---
Type "2"
Sleep 250ms
Enter
Sleep 800ms
Type "2"
Sleep 250ms
Enter
Sleep 800ms
Type "0"
Sleep 250ms
Enter
Sleep 1s
Type "0"
Sleep 250ms
Enter
Sleep 1.6s

# --- Turn 4: Bash, Defend, end turn ---
Type "0"
Sleep 250ms
Enter
Sleep 900ms
Type "0"
Sleep 250ms
Enter
Sleep 1s
Type "0"
Sleep 250ms
Enter
Sleep 1.6s

# --- Turn 5: Strike, Bash -> lethal, fight won ---
Type "1"
Sleep 250ms
Enter
Sleep 900ms
Type "3"
Sleep 250ms
Enter
Sleep 2.5s          # linger on the YOU WIN screen

# End screen: "(press enter to exit)".
Enter
Sleep 800ms
