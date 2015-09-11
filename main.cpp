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
    TouchPoint(int slot) 
        : slot(slot) 
    { 
        // TODO better way to represent nullable without boost?
        pressure = -1;
        originX = -1;
        originY = -1;
    }
    int slot;        // ABS_MT_SLOT
    int pressure;    // ABS_PRESSURE
    int absX;        // ABS_MT_POSITION_X
    int absY;        // ABS_MT_POSITION_Y
    int originX;
    int originY;
};

// init xdo and libevdev structs
bool init(xdo_t *&xdo, libevdev *&dev, int &rc);
// clea xdo and libevdev structs
void cleanup(xdo_t *xdo, libevdev *dev);
// remove touchPoint with given slot
void removeTouchPoint(std::vector<TouchPoint> &touchPoints, int slot);
// retrieve touchPoint with given slot
TouchPoint* getTouchPoint(std::vector<TouchPoint> &touchPoints, int slot);
// handle libevdev events
void handleEvents(libevdev *dev, input_event &ev, std::vector<TouchPoint> &touchPoints, int &currentSlot);

int main(int argc, char **argv) {
    xdo_t *xdo;
    libevdev *dev = NULL;
    int rc = 1;
    if (!init(xdo, dev, rc)) {
        return 1;
    }
    
    std::vector<TouchPoint> touchPoints;
    // MT communication protocol report contact info for each specific slot one slot at a time
    // first it specifies slot value, if there are more than one contact points, then it sends that slot info
    int currentSlot = 0;
    
    using Time = std::chrono::high_resolution_clock;
    using duration = std::chrono::duration<double>;
    duration d;
    double delta;
    
    // X and Y accumulators, due to the integer nature of xdo mouse control, we have to increment relative directions 
    double accX = 0.0;
    double accY = 0.0;
    
    while (1) {
        auto t0 = Time::now();
        do {
            input_event ev;
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                handleEvents(dev, ev, touchPoints, currentSlot);
            }
        } while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS);
        
        if (touchPoints.size() == 1) {
            auto tp = getTouchPoint(touchPoints, currentSlot);
            if (tp) {
                int vX = tp->absX - tp->originX; // raw input difference
                int vY = tp->absY - tp->originY; // raw input difference
                double length = std::sqrt(vX*vX + vY*vY);       // vector length
                double dirX = static_cast<double>(vX) / length; // normalized vector direction X
                double dirY = static_cast<double>(vY) / length; // normalized vector direction Y
                // edge case when length or dirX is 0
                if (isnan(dirX)) {
                    dirX = 0.0;
                } 
                // edge case when length or dirY is 0
                if (isnan(dirY)) {
                    dirY = 0.0;
                }
                
                // TODO add as parameters
                double sensitivityX = 0.08;
                double sensitivityY = 0.08;
                double deadzone = 0.1;
                // TODO length is squared in order to maintain precision when length is low, and to exponentially
                // increase speed when it's high
                // any better function to make small movements precise and big movements fast?
                if (std::abs(dirX) > deadzone) {
                    accX += dirX * std::pow(length, 2) * delta * sensitivityX;
                }
                if (std::abs(dirY) > deadzone) {
                    accY += dirY * std::pow(length, 2) * delta * sensitivityY;
                }
                
                int movX = 0.0;
                int movY = 0.0;
                if (std::abs(accX) >= 1.0) {
                    movX = accX;
                    accX = 0.0;
                }
                if (std::abs(accY) >= 1.0) {
                    movY = accY;
                    accY = 0.0;
                }
                
                xdo_move_mouse_relative(xdo, movX, movY);
            }
        }
        
        auto t1 = Time::now();
        d = t1 - t0;
        delta = d.count();
    }
    
    cleanup(xdo, dev);
    return 0;
}

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
            if (tp->originX == -1) {
                tp->originX = tp->absX;
            }
        }
        break;
      }
      case ABS_MT_POSITION_Y: {
        if (tp) {
            tp->absY = libevdev_get_event_value(dev, ev.type, ev.code);
            if (tp->originY == -1) {
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
            touchPoints.push_back(TouchPoint(currentSlot));
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
