"""
bridge.py - Fresh Picks: Python-to-C Communication Bridge
==========================================================
This module provides ONE reusable function  to call C binaries.

HOW IT WORKS:
    1. Flask route calls run_c_binary("auth", ["login_user", "User", "User@123"])
    2. This function builds the shell command: ./auth login_user User User@123
    3. subprocess.run() executes the C binary
    4. The C binary prints to stdout: SUCCESS|U001
    5. We parse this and return a clean Python dict: {"status": "SUCCESS", "data": "U001"}

USAGE EXAMPLE (in app.py):
    from bridge import run_c_binary

    result = run_c_binary("auth", ["login_user", username, password])
    if result["status"] == "SUCCESS":
        user_id = result["data"]

Team: CodeCrafters | Project: Fresh Picks | SDP-1
"""

import subprocess  # Used to run external programs (our C binaries)
import os          # Used to build the correct file path


# ─────────────────────────────────────────────────────────────
# CONSTANT: Path to the backend folder where C binaries live.
# os.path.dirname(__file__) gets the folder of THIS file (app/).
# We then go up one level (..) and into backend/.
# os.path.dirname(__file__) → /home/user/project/app
# os.path.join("/home/user/project/app", "..", "backend")
#             → /home/user/project/backend 
# ─────────────────────────────────────────────────────────────
BACKEND_DIR = os.path.join(os.path.dirname(__file__), "..", "backend")


def run_c_binary(executable_name, args_list):
    """
    PURPOSE:
        The single, universal function to call any C binary.

    HOW IT WORKS:
        1. Builds the full path to the binary (e.g., ../backend/auth)
        2. Assembles the full command: [binary_path, arg1, arg2, ...]
        3. Runs it with subprocess and captures stdout
        4. Splits the output on '|' and returns a clean dict

    PARAMS:
        executable_name (str): Name of the compiled binary.
                                Example: "auth"
        args_list (list):       List of string arguments to pass.
                                Example: ["login_user", "User", "User@123"]

    RETURNS:
        dict with keys:
            "status" (str): "SUCCESS" or "ERROR"
            "data"   (str): The rest of the output after the first '|'
                            Could be a user_id, message, profile data, etc.
            "raw"    (str): The full unmodified stdout string (for debugging)

    EXAMPLE RETURN VALUES:
        {"status": "SUCCESS", "data": "U001",           "raw": "SUCCESS|U001"}
        {"status": "ERROR",   "data": "Wrong password", "raw": "ERROR|Wrong password"}
    """

    # Step 1: Build the full path to the binary
    # On Linux/Mac: ../backend/auth
    # On Windows:   ../backend/auth.exe (if you add .exe)
    binary_path = os.path.join(BACKEND_DIR, executable_name)

    # Step 2: Build the full command list
    # Example: ["../backend/auth", "login_user", "User", "User@123"]
    command = [binary_path] + args_list

    try:
        # Step 3: Run the command
        # capture_output=True  -> captures stdout and stderr
        # text=True            -> returns strings instead of bytes
        # timeout=10           -> kill if takes more than 10 seconds
        # cwd=BACKEND_DIR      -> to run C binary INSIDE the backend folder
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=10,
            cwd=BACKEND_DIR  # <--- ADD THIS LINE
        )

        # Step 4: Get the output from stdout and clean whitespace
        raw_output = result.stdout.strip()

        # Step 5: Parse the output
        # Expected format: "STATUS|data" e.g. "SUCCESS|U001"
        if "|" in raw_output:
            # Split only on the FIRST '|' to allow '|' in data fields
            parts  = raw_output.split("|", 1)
            status = parts[0]  # "SUCCESS" or "ERROR"
            data   = parts[1]  # Everything after the first |
        else:
            # Unexpected format — treat as error
            status = "ERROR"
            data   = raw_output if raw_output else "No output from binary"

        # Step 6: Return the structured result
        return {
            "status":     status,
            "data":       data,
            "raw_output": raw_output 
        }

    except FileNotFoundError:
        # The binary doesn't exist — forgot to compile?
        return {
            "status":     "ERROR",
            "data":       f"Binary '{executable_name}' not found. Did you run build.sh?",
            "raw_output": ""
        }

    except subprocess.TimeoutExpired:
        # The binary took too long — something is wrong
        return {
            "status":     "ERROR",
            "data":       "Binary execution timed out",
            "raw_output": ""
        }

    except Exception as e:
        # Catch-all for any other unexpected errors
        return {
            "status":     "ERROR",
            "data":       f"Unexpected error: {str(e)}",
            "raw_output": ""
        }
