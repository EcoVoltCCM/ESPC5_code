import subprocess

def run_cmd(cmd):
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return result.stdout.strip(), result.returncode

# The commit to cherry pick
commit_hash = "bd424f1"

# All other local branches to apply the commit to
branches = ["GPS", "Increased_sampling", "main", "mqtt_logging", "optimization"]

# Remember the current branch
current_branch, _ = run_cmd("git rev-parse --abbrev-ref HEAD")

for branch in branches:
    print(f"Applying to branch: {branch}")
    # Checkout branch
    _, rc = run_cmd(f"git checkout {branch}")
    if rc != 0:
        print(f"  Failed to checkout {branch}")
        continue
    
    # Cherry pick
    out, rc = run_cmd(f"git cherry-pick {commit_hash}")
    if rc != 0:
        print(f"  Cherry-pick failed (conflict?) on {branch}: {out}")
        run_cmd("git cherry-pick --abort")
        continue
    
    # Push
    out, rc = run_cmd(f"git push origin {branch}")
    if rc == 0:
        print(f"  Successfully applied and pushed to {branch}")
    else:
        print(f"  Failed to push to {branch}: {out}")

# Restore
run_cmd(f"git checkout {current_branch}")
run_cmd(f"git push origin {current_branch}") # push the original branch too
print(f"Restored to {current_branch}")
