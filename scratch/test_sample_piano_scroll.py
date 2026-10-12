import subprocess
import time
import os

artifact_dir = "/home/kevinadhaikal/.gemini/antigravity-cli/brain/2744c8b9-a544-476e-874e-01ba68994ea8"

env = os.environ.copy()
env["VGMSTUDIO_OPEN_SAMPLE_VIEWER"] = "1"

proc = subprocess.Popen(["./build/vgmstudio", "samples/02 - Data Select.vgz"], env=env)
time.sleep(2.5)

try:
    win_ids = subprocess.check_output(["xdotool", "search", "--pid", str(proc.pid)]).decode().strip().split('\n')
    win_id = [w for w in win_ids if w][-1]
    subprocess.run(["xdotool", "windowactivate", "--sync", win_id])
    subprocess.run(["xdotool", "windowraise", win_id])
    time.sleep(0.5)

    # Scroll the right panel of Sample Viewer down to show the Audition Piano
    # Sample Viewer right pane is roughly around x=600, y=650
    subprocess.run(["xdotool", "mousemove", "--window", win_id, "600", "650"])
    time.sleep(0.1)
    for _ in range(8):
        subprocess.run(["xdotool", "click", "5"]) # scroll down
        time.sleep(0.05)
    time.sleep(0.3)
    subprocess.run(["scrot", "-u", f"{artifact_dir}/test_sample_viewer_scrolled_piano.png"])
    print("Saved test_sample_viewer_scrolled_piano.png")

    # Click Zoom to Cut button (approx x=405, y=550)
    # Or click inside the cut
    # Let's test clicking on a piano key
    subprocess.run(["xdotool", "mousemove", "--window", win_id, "500", "700"])
    subprocess.run(["xdotool", "mousedown", "1"])
    time.sleep(0.2)
    subprocess.run(["scrot", "-u", f"{artifact_dir}/test_sample_viewer_piano_clicked.png"])
    subprocess.run(["xdotool", "mouseup", "1"])
    print("Saved test_sample_viewer_piano_clicked.png")

finally:
    proc.terminate()
    proc.wait()

print("Test complete.")
