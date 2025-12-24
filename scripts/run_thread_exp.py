import subprocess
import os

def run_thread_test(num_threads):
    exec_path = os.path.join(os.getcwd(), "build/runtime")
    command = ["perf", "stat", exec_path, str(num_threads)]

    result = subprocess.run(
        command,
        capture_output=True,
        text=True
    )

    os.mkdir("temp") if not os.path.exists("temp") else None
    exp_res_path = os.path.join("temp", f"thread_exp_{num_threads}.txt")
    with open(exp_res_path, "w") as f:
        f.write(result.stderr)

if __name__ == "__main__":
    print(os.getcwd())
    for num_threads in (8, 16, 32, 64):
        run_thread_test(num_threads)
