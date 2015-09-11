/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2015 Mateusz Dudek
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <iostream>
#include <map>
#include <vector>
#include <chrono>
#include <thread> 

#include <cstdlib>
#include <cstring>
#include <fcntl.h> // for "open()" definition
#include <cmath>

// macro that prints out variable name and it's value
#define LOG(var) std::cout << #var": " << var << std::endl

extern "C" {
#include "libevdev/libevdev.h"
#include "libevdev/libevdev-uinput.h"
#include "xdo.h"
}

struct TouchPoint 
{
    int id;          // ABS_MT_TRACKING_ID
    int slot;        // ABS_MT_SLOT
    int pressure;    // ABS_PRESSURE
    int absX;        // ABS_MT_POSITION_X
    int absY;        // ABS_MT_POSITION_Y
    int originX;
    int originY;
};

bool init(xdo_t *&xdo, libevdev *&dev, int &rc) 
{
    // init xdo
    xdo = xdo_new(NULL);
    
    // init libevdev
    int fd;
    fd = open("/dev/input/event6", O_RDONLY | O_NONBLOCK);
    rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        // if encountered error says "Bad file descriptor" you probably must run as root
        std::cerr << "Failed to init libevdev. " << std::strerror(-rc) << std::endl;;
        return false;
    }
    std::cout << "Input device name: " << libevdev_get_name(dev) << std::endl;
    return true;
}

void cleanup(xdo_t *xdo, libevdev *dev) 
{
    xdo_free(xdo);
    libevdev_free(dev);
}

int getEventValue(const std::map<int, int> &eventDescriptor, int code)
{
    auto it = eventDescriptor.find(code);
    if (it != eventDescriptor.end()) {
        return it->second;
    }
    return 0;
}

void removeTouchPoint(std::vector<TouchPoint> &touchPoints, int slot)
{
    for (auto it = touchPoints.begin(); it != touchPoints.end(); ) {
        if (it->slot == slot) {
            it = touchPoints.erase(it);
        } else {
            it++;
        }
    }
}

TouchPoint* getTouchPoint(std::vector<TouchPoint> &touchPoints, int slot)
{
    for (TouchPoint &tp : touchPoints) {
        if (tp.slot == slot) {
            return &tp;
        }
    }
    return nullptr;
}

void handleEvents(libevdev *dev, input_event &ev, std::vector<TouchPoint> &touchPoints, int &currentSlot)
{
    auto tp = getTouchPoint(touchPoints, currentSlot);
    switch (ev.code) {
      case ABS_MT_POSITION_X: {
        if (tp) {
            tp->absX = libevdev_get_event_value(dev, ev.type, ev.code);
            if (tp->originX == 0) {
                tp->originX = tp->absX;
            }
        }
        break;
      }
      case ABS_MT_POSITION_Y: {
        if (tp) {
            tp->absY = libevdev_get_event_value(dev, ev.type, ev.code);
            if (tp->originY == 0) {
                tp->originY = tp->absY;
            }
        }
        break;
      }
      case ABS_PRESSURE: {
        if (tp) {
            tp->pressure = libevdev_get_event_value(dev, ev.type, ev.code); 
        }
        break;
      }
      case ABS_MT_TRACKING_ID: {
        int id = libevdev_get_event_value(dev, ev.type, ev.code);
        if (id == -1) {
            removeTouchPoint(touchPoints, currentSlot);
        } else {
            touchPoints.push_back(TouchPoint());
        }
        break;
      }
      case ABS_MT_SLOT: {
        currentSlot = libevdev_get_event_value(dev, ev.type, ev.code);
        break;
      }
      default: {
        break;
      }
    }
}

int main(int argc, char **argv) {
    xdo_t *xdo;
    libevdev *dev = NULL;
    int rc = 1;
    if (!init(xdo, dev, rc)) {
        return 1;
    }
    
    double moveCounterX = 0;
    double moveCounterY = 0;
    
    std::vector<TouchPoint> touchPoints;
    // MT communication protocol report contact info for each specific slot one slot at a time
    // first it specifies slot value, if there are more than one contact points, then it sends that slot info
    int currentSlot = 0;
    
    while (1) {
        do {
            input_event ev;
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                handleEvents(dev, ev, touchPoints, currentSlot);
            }
        } while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS);
        
        //sprawdzić czemu touch pointy nie są usuwane na bieżąco
        if (touchPoints.size() == 1) {
            auto tp = getTouchPoint(touchPoints, currentSlot);
            if (tp) {
                int dirX = tp->absX - tp->originX;
                int dirY = tp->absY - tp->originY;
                double length = std::sqrt(dirX*dirX + dirY*dirY) / 100;
                
                moveCounterX += dirX * 0.1 * std::min(1.0, length); // add delta, remove sleep
                moveCounterY += dirY * 0.1 * std::min(1.0, length); // add delta, remove sleep
                
                if (std::abs(moveCounterX) > 1) {
                    xdo_move_mouse_relative(xdo, moveCounterX, 0);
                    moveCounterX = 0;
                }
                if (std::abs(moveCounterY) > 1) {
                    xdo_move_mouse_relative(xdo, 0, moveCounterY);
                    moveCounterY = 0;
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    cleanup(xdo, dev);
    return 0;
}

