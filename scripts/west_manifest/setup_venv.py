#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import subprocess
import sys
import shutil


def create_and_activate_venv(remove_venv=False):
    venv_path = ".venv"
    requirements_file = "requirements.txt"

    try:
        path = shutil.which("python3")
        if path is None:
            print(
                "Python 3 is not installed. Please install it before executing this script."
            )
            return

        print(
            f"Creating virtual environment in {venv_path} and installing dependencies from {requirements_file}"
        )

        # Remove .venv directory if specified
        if remove_venv:
            if os.path.exists(venv_path):
                shutil.rmtree(venv_path)
                print(f"Removed existing virtual environment in {venv_path}")
            return

        # Check if .venv directory exists
        if not os.path.exists(venv_path):
            # Create virtual environment
            subprocess.run([sys.executable, "-m", "venv", venv_path], check=True)
            print(f"Created virtual environment in {venv_path}")
        else:
            print("Virtual environment already exists. Skipping creation.")

        # Activate the virtual environment and install dependencies
        if os.name == "nt":
            activate_script = os.path.join(venv_path, "Scripts", "activate.bat")
            command = f"{activate_script} && pip install -r {requirements_file}"
            subprocess.run(command, shell=True, check=True)
        else:
            activate_script = os.path.join(venv_path, "bin", "activate")
            command = f"source {activate_script}"
            subprocess.run(command, shell=True, executable="/bin/bash", check=True)
            command = f"pip install -r {requirements_file}"
            subprocess.run(command, shell=True, executable="/bin/bash", check=True)
        # Print activation command in color
        if os.name == "nt":
            activate_command = f"{activate_script}"
        else:
            activate_command = f"source {activate_script}"

        print(
            f"\033[92mTo activate the virtual environment, run: {activate_command}\033[0m"
        )

    except FileNotFoundError as e:
        print(f"Error: {e}")
    except subprocess.CalledProcessError as e:
        print(f"Command '{e.cmd}' returned non-zero exit status {e.returncode}.")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")


def main():
    # Check if the script is executed from the directory it is located in
    script_dir = os.path.dirname(os.path.abspath(__file__))
    current_dir = os.getcwd()
    if script_dir != current_dir:
        print(f"Please execute this script from its directory: {script_dir}")
        sys.exit(1)

    remove_venv = "--remove-venv" in sys.argv
    create_and_activate_venv(remove_venv)


if __name__ == "__main__":
    main()
