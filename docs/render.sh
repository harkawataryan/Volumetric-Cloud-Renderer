#!/bin/bash
# Renders the three README images headless (Mesa EGL works without a GPU).
# Run from the repo root after building: ./docs/render.sh
set -e
R="./build/clouds --headless --size 1280x720 --frames 48"
$R --cam 0,200,0 --yaw 0.9 --pitch 0.22 --coverage 0.5 --sun-el 0.55 --sun-az 2.4 --out docs/below.png
$R --cam 0,6500,0 --yaw 0.9 --pitch -0.2 --coverage 0.55 --sun-el 0.35 --sun-az 0.9 --out docs/above.png
$R --cam 0,200,0 --yaw 0.9 --pitch 0.2 --coverage 0.5 --sun-el 0.05 --sun-az -0.3 --out docs/sunset.png
