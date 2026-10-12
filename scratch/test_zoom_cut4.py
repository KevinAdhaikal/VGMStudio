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

    # Click the "Zoom to Cut" button at x=410, y=785
    subprocess.run(["xdotool", "mousemove", "--window", win_id, "410", "785"])
    subprocess.run(["xdotool", "click", "1"])
    time.sleep(0.4)

    subprocess.run(["scrot", "-w", win_id, f"{artifact_dir}/sample_viewer_zoomed_cut_final.png"])
    print("Saved sample_viewer_zoomed_cut_final.png")

finally:
    proc.terminate()
    proc.wait()

print("Done.")
