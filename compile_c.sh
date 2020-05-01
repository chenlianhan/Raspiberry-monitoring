#! /bin/sh
g++ -w -std=c++11 send_cam.cpp -o send_cam `pkg-config --cflags --libs libavformat libavcodec libavutil libavdevice libswscale`
