/*
    This file is part of duckOS.

    duckOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    duckOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with duckOS.  If not, see <https://www.gnu.org/licenses/>.

    Copyright (c) Byteduck 2016-2020. All rights reserved.
*/

#ifndef DUCKOS_MOUSEDEVICE_H
#define DUCKOS_MOUSEDEVICE_H

#include <kernel/interrupt/IRQHandler.h>
#include <kernel/device/CharacterDevice.h>
#include <common/circular_queue.hpp>

#define I8042_BUFFER 0x60u
#define I8042_STATUS 0x64u

#define I8042_ACK 0xFAu
#define I8042_BUFFER_FULL 0x01u
#define I8042_WHICH_BUFFER 0x20u
#define I8042_MOUSE_BUFFER 0x20u
#define I8042_KEYBOARD_BUFFER 0x00u

#define MOUSE_SET_RESOLUTION 0xE8u
#define MOUSE_STATUS_REQUEST 0xE9u
#define MOUSE_REQUEST_SINGLE_PACKET 0xEBu
#define MOUSE_GET_DEVICE_ID 0xF2u
#define MOUSE_SET_SAMPLE_RATE 0xF3u
#define MOUSE_ENABLE_PACKET_STREAMING 0xF4u
#define MOUSE_DISABLE_PACKET_STREAMING 0xF5u
#define MOUSE_SET_DEFAULTS 0xF6u
#define MOUSE_RESEND 0xFEu
#define MOUSE_RESET 0xFFu
#define MOUSE_INTELLIMOUSE_ID 0x03u
#define MOUSE_INTELLIMOUSE_EXPLORER_ID 0x04u

struct MouseEvent {
	int x;
	int y;
	int z;
	uint8_t buttons;
};

class MouseDevice: public CharacterDevice, public IRQHandler {
public:
	static MouseDevice* inst();

	MouseDevice();

	//Device
	ssize_t read(FileDescriptor& fd, size_t offset, uint8_t* buffer, size_t count) override;
	ssize_t write(FileDescriptor& fd, size_t offset, const uint8_t* buffer, size_t count) override;

	//IRQHandler
	void handle_irq(Registers* regs) override;

private:
	static void wait_read();
	static void wait_write();
	static uint8_t read();
	static void write(uint8_t value);
	void handle_packet();

	static MouseDevice* instance;
	bool present = false;
	bool has_scroll_wheel = false;
	uint8_t packet_data[4];
	uint8_t packet_state = 0;
	DC::circular_queue<MouseEvent> event_buffer;
	SpinLock lock;
};


#endif //DUCKOS_MOUSEDEVICE_H
