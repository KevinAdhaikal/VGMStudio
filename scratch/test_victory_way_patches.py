import subprocess
import time
import os

artifact_dir = "/home/kevinadhaikal/.gemini/antigravity-cli/brain/2744c8b9-a544-476e-874e-01ba68994ea8"
os.makedirs(artifact_dir, exist_ok=True)

# Remove old screenshots
subprocess.run(["rm", "-f", f"{artifact_dir}/test_victory_way_*.png"])

env = os.environ.copy()
env["VGMSTUDIO_OPEN_PATCH_VIEWER"] = "1"
proc = subprocess.Popen(["./build/vgmstudio", "samples/15 Victory Way (Ending).vgz"], env=env)
time.sleep(2.5)

try:
    win_ids = subprocess.check_output(["xdotool", "search", "--name", "VGM Studio"]).decode().strip().split('\n')
    win_id = win_ids[-1]
    subprocess.run(["xdotool", "windowactivate", "--sync", win_id])
    subprocess.run(["xdotool", "windowraise", win_id])
    time.sleep(0.5)

    # 1. Capture initial patch viewer showing presets sorted by Most Used with Preset #19 auto-selected
    subprocess.run(["scrot", "-w", win_id, f"{artifact_dir}/test_victory_way_most_used.png"])
    print("Saved test_victory_way_most_used.png")

    # 2. Open Channel combo (row 2, right dropdown: around x=260, y=200 inside window)
    # Click to open Channel dropdown
    subprocess.run(["xdotool", "mousemove", "--window", win_id, "250", "200"])
    subprocess.run(["xdotool", "click", "--window", win_id, "1"])
    time.sleep(0.4)

    # Click on "Ch 2" item in the combo dropdown list (approx 50px below)
    subprocess.run(["xdotool", "mousemove", "--window", win_id, "250", "255"])
    subprocess.run(["xdotool", "click", "--window", win_id, "1"])
    time.sleep(0.6)

    # Capture after selecting Ch 2
    subprocess.run(["scrot", "-w", win_id, f"{artifact_dir}/test_victory_way_filtered_ch2.png"])
    print("Saved test_victory_way_filtered_ch2.png")

finally:
    proc.terminate()
    proc.wait()

print("Victory Way tests finished!")
