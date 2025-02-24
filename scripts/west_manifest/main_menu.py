#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import blessed
import signal
import os
import sys
from time import sleep
from project_menu import main as manage_projects, center_print, render_menu


def render_main_menu(term, selected_index):
    menu_items = {"1": "Manage Manifest Components", "q": "Quit"}

    result = [term.home + term.normal + term.clear]
    result.append(term.move_y(term.height // 3))
    result.append(term.bold_white_on_blue(term.center("MAIN MENU")))
    result.append("\n\n")

    for i, item in enumerate(menu_items):
        if i == selected_index:
            result.append(
                term.center(term.white_on_blue(f" > {item} < {menu_items.get(item)} "))
                + "\n"
            )
        else:
            result.append(term.center(f"  {item}  {menu_items.get(item)}") + "\n")

    footer_items = {"↑↓": "Navigate", "Enter": "Select", "q": "Quit"}
    result.append(render_menu(term, footer_items))

    return "".join(result)


def handle_main_menu(term):
    selected_index = 0
    menu_items = ["Manage Projects", "Quit"]
    resize_flag = False

    while True:
        if resize_flag:
            term = blessed.Terminal()  # Reinitialize the terminal object
            resize_flag = False

        print(render_main_menu(term, selected_index), end="", flush=True)
        with term.cbreak(), term.hidden_cursor():
            key = term.inkey(timeout=0.1)
            # Check if the terminal has resized.
            # Unfortunatley, Blessed doesn't provide a way to check this.
            # This unfortuantely comes with a CPU hit of about 1.5%
            if not key:
                if "RESIZE_FLAG" in os.environ:
                    resize_flag = True
                    del os.environ["RESIZE_FLAG"]
                continue
            if key.name == "KEY_UP":
                selected_index = (selected_index - 1) % len(menu_items)
            elif key.name == "KEY_DOWN":
                selected_index = (selected_index + 1) % len(menu_items)
            elif key.name == "KEY_ENTER":
                if selected_index == 0:
                    manage_projects()
                elif selected_index == 1:
                    quit_program(term)
            elif key.lower() == "q" or key.name == "KEY_ESCAPE":
                quit_program(term)


def handle_resize(signum, frame):
    os.environ["RESIZE_FLAG"] = "1"


def quit_program(term):
    with term.hidden_cursor():
        quit_message = "[ Quitting... ]"
        center_print(term, quit_message, formatter=term.formatter("white_on_blue"))
        sleep(0.5)
    sys.exit(0)


def main():
    # Check if the script is executed from the directory it is located in
    script_dir = os.path.dirname(os.path.abspath(__file__))
    current_dir = os.getcwd()
    if script_dir != current_dir:
        print(f"Please execute this script from its directory: {script_dir}")
        sys.exit(1)

    term = blessed.Terminal()
    signal.signal(signal.SIGWINCH, handle_resize)

    with term.fullscreen():
        handle_main_menu(term)


if __name__ == "__main__":
    main()
