# vhs tape for the mini-spire terminal-play demo GIF.
#   brew install vhs   (https://github.com/charmbracelet/vhs)
#   vhs assets/demo.tape   ->   writes assets/gameplay.gif
#
# Replays a complete winning fight on seed 0 (3 turns) in the Textual UI.
#
# The keystrokes are DERIVED, not transcribed: `uv run python -m
# analysis.derive_demo_line` plays the fight headlessly and prints the local
# index for each press. The previous tape was hand-transcribed from a play log
# and carried a comment apologising for how easily the indices drift as the
# hand changes — so the drift is computed now instead of watched for.
#
# Textual acts on the keypress itself. The old print-and-prompt loop needed a
# digit AND an Enter to submit, which is why every keystroke below is a single
# Type with no Enter, and why the app closes itself on victory rather than
# waiting on "press enter to exit".
#
# The arrow keys and 'p' below change what is on screen but not the game state,
# so they can be spliced anywhere without shifting the indices that follow.

Output assets/gameplay.gif

Set FontSize 16
Set Width 1200
# 660 fits the fight panel + hand grid + footer with no dead band. At 800 the
# bottom third was empty terminal, which reads as a badly cropped screenshot.
Set Height 660
Set Padding 20
Set Theme "Catppuccin Mocha"

Type "uv run minispire-play 0"
Enter
Sleep 2.5s

# Arrow across the hand so the focus highlight and each card's rules text are
# visible before anything is played — that legibility is the point of the
# revamp, and a still frame of the opening hand does not show it.
Right
Sleep 800ms
Right
Sleep 800ms
Down
Sleep 900ms
Left
Sleep 1s

# --- Turn 1 ---
Type "2"
Sleep 1.1s
Type "0"
Sleep 1.1s
Type "0"
Sleep 1.4s
Type "0"
Sleep 1.8s

# --- Turn 2 ---
Type "0"
Sleep 1.1s
Type "1"
Sleep 1.1s
Type "0"
Sleep 1.4s

# Open the piles mid-fight: the discard now holds real cards, so this shows
# the box view and that browsing does not cost a turn.
Type "p"
Sleep 1.6s
Right
Sleep 700ms
Down
Sleep 900ms
Type "p"
Sleep 1s

Type "1"
Sleep 1.1s

# --- Turn 3: lethal ---
Type "1"
Sleep 1.1s
Type "0"
Sleep 1.1s
Type "4"
Sleep 1.1s
Type "2"
Sleep 3s          # linger on the victory screen
