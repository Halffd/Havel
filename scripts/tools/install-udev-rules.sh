#!/bin/bash
sudo cp 99-havel-uinput.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
echo "Add yourself to input group: sudo usermod -a -G input \$USER"