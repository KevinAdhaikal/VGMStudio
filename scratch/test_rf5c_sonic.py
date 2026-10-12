import subprocess
import time
import os

artifact_dir = "/home/kevinadhaikal/.gemini/antigravity-cli/brain/2744c8b9-a544-476e-874e-01ba68994ea8"

def capture_vgz(vgz_path, out_png_name):
    print(f"Testing {vgz_path}...")
    env = os.environ.copy()
    env["VGMSTUDIO_OPEN_SAMPLE_VIEWER"] = "1"

    proc = subprocess.Popen(["./build/vgmstudio", vgz_path], env=env)
    time.sleep(2.5)

    try:
        win_ids = subprocess.check_output(["xdotool", "search", "--pid", str(proc.pid)]).decode().strip().split('\n')
        win_id = [w for w in win_ids if w][-1]
        subprocess.run(["xdotool", "windowactivate", "--sync", win_id])
        subprocess.run(["xdotool", "windowraise", win_id])
        time.sleep(0.5)

        out_path = f"{artifact_dir}/{out_png_name}"
        subprocess.run(["scrot", "-w", win_id, out_path])
        print(f"Saved screenshot: {out_path}")

    finally:
        proc.terminate()
        proc.wait()

capture_vgz("samples/01 - Palmtree Panic Zone Past.vgz", "sample_viewer_rf5c_palmtree_panic.png")
time.sleep(1.0)
capture_vgz("samples/07 - Metallic Madness Zone Past.vgz", "sample_viewer_rf5c_metallic_madness.png")

print("All captures done.")
