# vhs tape for the mini-spire terminal-play demo GIF.
#   brew install vhs   (https://github.com/charmbracelet/vhs)
#   vhs assets/demo.tape   ->   writes assets/demo.gif
#
# Records a short, scripted slice of `minispire-play` on seed 0: Bash (applies
# Vulnerable) -> Strike (hits harder into Vulnerable) -> end turn. Deterministic
# because the seed fixes the fight, so this re-renders identically.

Output assets/demo.gif

# Terminal look — sized to fit the TUI panels; tweak Theme to taste.
Set FontSize 16
Set Width 1100
Set Height 640
Set Padding 20
Set Theme "Catppuccin Mocha"

# Launch the TUI on a fixed seed. (Run vhs from the repo root so `uv` resolves.)
Type "uv run minispire-play 0"
Enter
Sleep 2.5s

# Turn 1: Bash (local index 3) -> applies Vulnerable, deals 8.
Type "3"
Sleep 500ms
Enter
Sleep 2s

# Strike (local index 0) into the Vulnerable enemy -> hits for 9.
Type "0"
Sleep 500ms
Enter
Sleep 2s

# End the turn (index 5) -> Jaw Worm takes its move.
Type "5"
Sleep 500ms
Enter
Sleep 2.5s

# Quit cleanly.
Type "q"
Enter
Sleep 1s
