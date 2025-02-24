#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import requests
import yaml
import sys
import os
from time import sleep
from collections import OrderedDict
import subprocess
import re
from packaging import version
import itertools
import threading
import os
import subprocess
import shutil

# See https://blessed.readthedocs.io/en/latest/index.html
import blessed
import signal
import math

# manifest_url = (
#     "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/west.yml"
# )

# fluke_manifest_url = (
#     "https://raw.githubusercontent.com/FlukeCorp-emu/zephyr-module-manifest/refs/heads/main/west.yml"
# )

manifest_self = {
    "name": "zephyr",
    "revision": "main",
    "path": "zephyr/zephyr",
    "west-commands": "scripts/west-commands.yml",
    "clone-depth": 1,
}


class GroupFilterList(list):
    pass


def ordered_dict_representer(dumper, data):
    return dumper.represent_mapping("tag:yaml.org,2002:map", data.items())


def group_filter_representer(dumper, data):
    return dumper.represent_sequence("tag:yaml.org,2002:seq", data, flow_style=True)


yaml.add_representer(OrderedDict, ordered_dict_representer)
yaml.add_representer(GroupFilterList, group_filter_representer)


def center_print(term, message, y_offset=0, formatter=None):
    y = term.height // 3 + y_offset
    x = (term.width - len(message)) // 2
    formatted_message = formatter(message) if formatter else message
    print(term.move_yx(y, x) + formatted_message, end="", flush=True)


def render_menu(term, menu_items):
    menu_text = " ".join(f"[{key}] {value}" for key, value in menu_items.items())
    formatted_menu = term.formatter("bold_on_blue")(menu_text.ljust(term.width))
    return term.move_yx(term.height - 1, 0) + formatted_menu


def render_projects(term, projects, idx, selected_tag):
    project_count = len(projects)

    # Prevent cursor from going beyond the list
    idx = min(idx, project_count - 1)

    list_height = term.height - 1
    page = (idx // list_height) + 1
    pages = math.ceil(project_count / list_height)
    bottom = (page - 1) * list_height
    top = min(bottom + list_height, project_count)

    result = [term.home + term.normal + term.clear_eos]

    for i in range(bottom, top):
        project = projects[i]
        line = f"{'[X]' if 'selected' in project else '[ ]'} {project['name']}"
        if i == idx:
            line = term.formatter("white_on_blue")(line)
        result.append(line + "\n")  # Add newline after each project

    # Remove the last newline to prevent an empty line at the bottom
    if result[-1].endswith("\n"):
        result[-1] = result[-1].rstrip("\n")

    # Modify menu items
    menu_items = {
        "s": "Save",
        "t": f"Select Tag ({selected_tag})",  # Updated to show current tag
        "Space": "Select/Deselect",
        "↓↑": "Navigate",
        "q": "Back to Main Menu",
    }

    # Render menu
    result.append(render_menu(term, menu_items))

    return idx, "".join(result)


def handle_resize(signum, frame):
    os.environ["RESIZE_FLAG"] = "1"


def quit(term):
    with term.hidden_cursor():
        quit_message = "[ Quitting... ]"
        center_print(term, quit_message, formatter=term.formatter("white_on_blue"))
        sleep(1)
    sys.exit(0)


def deep_merge(dict1, dict2):
    """
    Perform a deep merge of dict2 into dict1 without altering the original dictionaries.
    """
    result = {}

    for key, value in dict1.items():
        if key in dict2:
            if isinstance(value, dict) and isinstance(dict2[key], dict):
                result[key] = deep_merge(value, dict2[key])
            elif isinstance(value, list) and isinstance(dict2[key], list):
                result[key] = value + dict2[key]
            else:
                result[key] = dict2[key]
        else:
            result[key] = value

    for key, value in dict2.items():
        if key not in result:
            result[key] = value

    return result


def save_manifest(term, manifest, selected_tag):
    file_name = "../../manifest/west.yml"

    manifest_remotes = manifest["manifest"]["remotes"]
    manifest_defaults = manifest["manifest"]["defaults"]
    manifest_self["revision"] = selected_tag

    with term.hidden_cursor():
        selected_projects = [
            project
            for project in manifest["manifest"]["projects"]
            if "selected" in project
        ]

        # Remove the temporary selected key from each project
        selected_projects = [
            {
                k: (f"zephyr/{v}" if k == "path" else v)
                for k, v in project.items()
                if k != "selected"
            }
            for project in selected_projects
            if project.get("selected")
        ]

        # Add manifest_self to selected_projects
        selected_projects.append(manifest_self)

        new_manifest = OrderedDict(
            [
                (
                    "manifest",
                    OrderedDict(
                        [
                            ("defaults", manifest_defaults),
                            ("remotes", manifest_remotes),
                            ("projects", selected_projects),
                        ]
                    ),
                )
            ]
        )

        with open(file_name, "w") as file:
            yaml.dump(new_manifest, file, default_flow_style=False)

        center_print(term, " Saving.... ", formatter=term.formatter("white_on_blue"))
        sleep(0.5)

        file_path = os.path.abspath(file_name)
        center_print(
            term,
            f" Saved to: {file_path} ",
            y_offset=2,
            formatter=term.formatter("white_on_blue"),
        )
        sleep(1.5)

    return True


def fetch_compliant_tags(term, repo_url):
    # Start spinner animation
    stop_spinner = threading.Event()
    spinner_thread = threading.Thread(
        target=spinner_animation, args=(term, stop_spinner, "Fetching tags")
    )
    spinner_thread.start()

    try:
        output = subprocess.check_output(
            ["git", "ls-remote", "--tags", repo_url],
            universal_newlines=True,
            stderr=subprocess.STDOUT,
        )
        lines = output.splitlines()
        pattern = re.compile(r"^[a-f0-9]+\s+refs/tags/(v[23]\.\d+\.\d+)$")
        tags = [match.group(1) for line in lines if (match := pattern.match(line))]
        tags.sort(key=lambda x: version.parse(x[1:]), reverse=True)

        # Add "main" to the beginning of the list
        tags.insert(0, "main")

        # Stop spinner animation
        stop_spinner.set()
        spinner_thread.join()

        return tags
    except subprocess.CalledProcessError as e:
        # Stop spinner animation
        stop_spinner.set()
        spinner_thread.join()

        error_message = f"Error executing git command: {e.output}"
        center_print(term, error_message, formatter=term.formatter("bold_red_on_white"))
        sleep(2)  # Give user time to read the error
        return ["main"]  # Return at least "main" if there's an error
    except Exception as e:
        # Stop spinner animation
        stop_spinner.set()
        spinner_thread.join()

        error_message = f"An unexpected error occurred: {e}"
        center_print(term, error_message, formatter=term.formatter("bold_red_on_white"))
        sleep(2)  # Give user time to read the error
        return ["main"]  # Return at least "main" if there's an error


def spinner_animation(term, stop_event, message):
    spinner = itertools.cycle(["-", "\\", "|", "/"])
    while not stop_event.is_set():
        _message = f"{message} {next(spinner)}"
        center_print(term, _message, formatter=term.formatter("bold_white_on_blue"))
        sleep(0.1)


def render_tag_selection(term, tags, selected_idx):
    max_height = term.height - 4  # Leave some space for borders and instructions
    max_width = max(len(tag) for tag in tags) + 4
    start_y = (term.height - max_height) // 2
    start_x = (term.width - max_width) // 2

    # Calculate the visible range of tags
    start_index = max(0, selected_idx - max_height // 2)
    end_index = min(len(tags), start_index + max_height)
    visible_tags = tags[start_index:end_index]

    result = []
    result.append(term.clear)

    # Draw top border
    result.append(
        term.move_yx(start_y - 1, start_x) + "┌" + "─" * (max_width - 2) + "┐"
    )

    # Draw tags
    for i, tag in enumerate(visible_tags):
        y = start_y + i
        if start_index + i == selected_idx:
            result.append(
                term.move_yx(y, start_x)
                + "│"
                + term.bold_white_on_blue(tag.center(max_width - 2))
                + "│"
            )
        else:
            result.append(
                term.move_yx(y, start_x) + "│" + tag.center(max_width - 2) + "│"
            )

    # Fill empty space if there are fewer visible tags than max_height
    for i in range(len(visible_tags), max_height):
        y = start_y + i
        result.append(term.move_yx(y, start_x) + "│" + " " * (max_width - 2) + "│")

    # Draw bottom border
    result.append(
        term.move_yx(start_y + max_height, start_x) + "└" + "─" * (max_width - 2) + "┘"
    )

    # Add scrollbar if necessary
    if len(tags) > max_height:
        scrollbar_height = max(1, int(max_height * (max_height / len(tags))))
        scrollbar_pos = int((start_index / len(tags)) * (max_height - scrollbar_height))
        for i in range(max_height):
            if scrollbar_pos <= i < scrollbar_pos + scrollbar_height:
                result.append(term.move_yx(start_y + i, start_x + max_width - 1) + "█")

    # Add instructions
    instructions = " ↑↓: Navigate | Enter: Select | q: Cancel "
    formatted_instructions = term.white_on_blue(instructions)
    result.append(
        term.move_yx(start_y + max_height + 1, (term.width - len(instructions)) // 2)
        + formatted_instructions
    )

    return "".join(result)


def fetch_manifest(term, manifest_url, is_private=False):
    # Start spinner animation
    stop_spinner = threading.Event()
    spinner_thread = threading.Thread(
        target=spinner_animation, args=(term, stop_spinner, "Fetching Manifest")
    )
    spinner_thread.start()
    headers = {}
    try:
        if is_private:
            access_token = os.environ.get("GITHUB_ACCESS_TOKEN")
            if not access_token:
                raise ValueError("GITHUB_ACCESS_TOKEN environment variable is not set")

            # Set up the headers with the access token
            headers = {
                "Authorization": f"Bearer {access_token}",
                "Accept": "application/vnd.github.raw+json",
            }

        response = requests.get(manifest_url, allow_redirects=True, headers=headers)
        manifest = response.content.decode("utf-8")
        manifest = yaml.safe_load(manifest)

        # Stop spinner animation
        stop_spinner.set()
        spinner_thread.join()

        return manifest
    except Exception as e:
        # Stop spinner animation
        stop_spinner.set()
        spinner_thread.join()

        # Display error message
        error_message = f"Error fetching manifest: {str(e)}"
        center_print(term, error_message, formatter=term.formatter("bold_red_on_white"))
        sleep(2)  # Give user time to read the error
        return None


def handle_tag_selection(term, tags, current_tag, base_manifest_url):
    tag_idx = tags.index(current_tag) if current_tag in tags else 0
    resize_flag = False
    while True:
        if resize_flag:
            term = blessed.Terminal()
            resize_flag = False

        print(render_tag_selection(term, tags, tag_idx), end="", flush=True)
        tag_inp = term.inkey(timeout=0.1)
        if not tag_inp:
            if "RESIZE_FLAG" in os.environ:
                resize_flag = True
                del os.environ["RESIZE_FLAG"]
            continue
        if tag_inp.code == term.KEY_UP:
            tag_idx = max(0, tag_idx - 1)
        elif tag_inp.code == term.KEY_DOWN:
            tag_idx = min(len(tags) - 1, tag_idx + 1)
        elif tag_inp.code == term.KEY_ENTER:
            selected_tag = tags[tag_idx]
            if selected_tag != current_tag:
                manifest_url = base_manifest_url.format(selected_tag)
                manifest = fetch_manifest(term, manifest_url)
                if manifest:
                    return selected_tag, manifest
                else:
                    return None, None
            else:
                return current_tag, None
        elif tag_inp == "q" or tag_inp == "Q" or tag_inp.name == "KEY_ESCAPE":
            return None, None


def check_and_prompt_west_update(term):
    # Check if the "west" command exists
    if shutil.which("west") is None:
        center_print(
            term,
            "West command has not been found. Please run west update manually",
            formatter=term.formatter("white_on_blue"),
        )
        sleep(1)
        return

    current_dir = os.getcwd() + "/../../manifest"
    west_yml_path = os.path.join(current_dir, "west.yml")
    if os.path.exists(west_yml_path):
        with term.cbreak(), term.hidden_cursor():
            center_print(
                term,
                "west.yml file detected. Do you want to run west update? (y/n) ",
                formatter=term.formatter("white_on_blue"),
            )
            response = term.inkey()
            if response.lower() == "y":
                process = subprocess.Popen(
                    ["west", "update"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                output_lines = []
                max_lines = 10  # Set the maximum number of lines for the dialog
                dialog_width = 50
                dialog_height = max_lines + 4  # Adding 4 for padding and borders

                def render_dialog():
                    print(term.clear)
                    top_border = "┌" + "─" * (dialog_width - 2) + "┐"
                    bottom_border = "└" + "─" * (dialog_width - 2) + "┘"
                    empty_line = "│" + " " * (dialog_width - 2) + "│"

                    print(term.move_y(term.height // 2 - dialog_height // 2))

                    print(term.center(top_border))
                    print(term.center(empty_line))
                    print(
                        term.center(
                            "│"
                            + term.bold_white_on_blue(" West Update Output ").center(
                                dialog_width - 2
                            )
                            + "│"
                        )
                    )
                    print(term.center(empty_line))

                    for line in output_lines[-max_lines:]:
                        truncated_line = (
                            (line[: dialog_width - 5] + "...")
                            if len(line) > dialog_width - 5
                            else line
                        )
                        print(
                            term.center(
                                "│" + truncated_line.center(dialog_width - 2) + "│"
                            )
                        )

                    print(term.center(empty_line))
                    print(
                        term.center(
                            "│"
                            + term.bold_white_on_blue(" Press 'q' to quit ").center(
                                dialog_width - 2
                            )
                            + "│"
                        )
                    )
                    print(term.center(empty_line))
                    print(term.center(bottom_border))

                for line in process.stdout:
                    output_lines.append(line.strip())
                    render_dialog()
                for line in process.stderr:
                    output_lines.append(line.strip())
                    render_dialog()

                # Wait for user to press 'q' to quit the dialog
                while True:
                    key = term.inkey()
                    if key.lower() == "q":
                        break


def main():
    west_dirty = False
    repo_url = "https://github.com/zephyrproject-rtos/zephyr.git"
    base_manifest_url = (
        "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/{}/west.yml"
    )
    fluke_manifest_url = "https://raw.githubusercontent.com/FlukeCorp-emu/zephyr-module-manifest/refs/heads/main/west.yml"

    term = blessed.Terminal()
    signal.signal(signal.SIGWINCH, handle_resize)

    # Fetch tags
    tags = fetch_compliant_tags(term, repo_url)

    selected_tag = tags[0] if tags else "main"

    # Use the new fetch_manifest function here
    zephyr_manifest = fetch_manifest(term, base_manifest_url.format(selected_tag))
    fluke_manifest = fetch_manifest(term, fluke_manifest_url, is_private=True)

    if not zephyr_manifest or not fluke_manifest:
        return  # Exit if we couldn't fetch the initial manifest

    # Fluke manifest is the base, zephyr is added on
    manifest = deep_merge(fluke_manifest, zephyr_manifest)
    # Start with fluke projects
    projects = manifest["manifest"]["projects"]

    with term.cbreak(), term.hidden_cursor(), term.fullscreen():
        idx = 0
        dirty = True
        resize_flag = False
        while True:
            if resize_flag:
                term = blessed.Terminal()  # Reinitialize the terminal object
                resize_flag = False
                dirty = True
            if dirty:
                idx, outp = render_projects(term, projects, idx, selected_tag)
                print(outp, end="", flush=True)
            with term.hidden_cursor():
                inp = term.inkey(timeout=0.1)
                dirty = True
            if not inp:
                if "RESIZE_FLAG" in os.environ:
                    resize_flag = True
                    del os.environ["RESIZE_FLAG"]
                continue
            elif inp == "s" or inp == "S":
                west_dirty = save_manifest(term, manifest, selected_tag)
                dirty = True
            # Tag selection sub menu
            elif inp == "t" or inp == "T":
                # TODO: need to change this to handle both zephyr and fluke
                new_tag, new_manifest = handle_tag_selection(
                    term, tags, selected_tag, base_manifest_url
                )
                if new_tag != selected_tag and new_manifest:
                    selected_tag = new_tag
                    manifest = deep_merge(fluke_manifest, new_manifest)
                    projects = manifest["manifest"]["projects"]
                    idx = 0
                    dirty = True
            elif inp == "q" or inp == "Q" or inp.name == "KEY_ESCAPE":
                # Call the function to check and prompt for west update
                if west_dirty:
                    check_and_prompt_west_update(term)
                return
            elif inp.code == term.KEY_UP or inp == "u":
                idx = max(0, idx - 1)
            elif inp.code == term.KEY_DOWN or inp == "d":
                idx = min(len(projects) - 1, idx + 1)
            elif inp == " ":
                if "selected" in projects[idx]:
                    del projects[idx]["selected"]
                else:
                    projects[idx]["selected"] = 1
            elif inp != "\x0c":
                dirty = False


if __name__ == "__main__":
    main()
