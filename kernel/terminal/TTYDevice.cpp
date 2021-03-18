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

#include <kernel/tasking/TaskManager.h>
#include <kernel/device/VGADevice.h>
#include "TTYDevice.h"

TTYDevice::TTYDevice(unsigned int major, unsigned int minor): CharacterDevice(major, minor), _input_buffer(1024) {
	_termios.c_iflag = 0;
	_termios.c_oflag = 0;
	_termios.c_cflag = 0;
	_termios.c_lflag = ISIG | ECHO | ICANON;
	static const char c_cc[] = "\017\031\004\0\0\010\003\025\026\1\034\022\021\024\023\032\0\0\027";
	memcpy(_termios.c_cc, c_cc, sizeof(c_cc));
	_termios.c_ispeed = 0;
	_termios.c_ospeed = 0;
}

ssize_t TTYDevice::write(FileDescriptor &fd, size_t offset, const uint8_t *buffer, size_t count) {
	return tty_write(buffer, count);
}

ssize_t TTYDevice::read(FileDescriptor &fd, size_t offset, uint8_t *buffer, size_t count) {
	LOCK(_input_lock);

	//Block until there's something to read
	while(!_buffer_blocker.is_ready())
		TaskManager::current_process()->block(_buffer_blocker);

	size_t nread = 0;

	count = min(count, _input_buffer.size());
	if(_termios.c_lflag & ICANON) {
		//Canonical mode
		for(nread = 0; nread < count; nread++) {
			char c = _input_buffer.pop_front();
			if(c == '\n' || c == _termios.c_cc[VEOL]) {
				//We got a newline, decrease the number of lines available and stop reading
				_lines--;
				buffer[nread++] = c;
				break;
			} else if(c == '\0') {
				//EOF, decrease lines available
				_lines--;
				break;
			}
			buffer[nread] = c;
		}

		//If we read all the lines or the buffer is empty, set the buffer blocker to not ready
		_buffer_blocker.set_ready(_lines && !_input_buffer.empty());
	} else {
		//Non-canonical mode
		while(count--) {
			*buffer++ = _input_buffer.pop_front();
			nread++;
		}

		//If we read all the data, set the buffer blocker to not ready
		_buffer_blocker.set_ready(!_input_buffer.empty());
	}

	return nread;
}

bool TTYDevice::is_tty() {
	return true;
}

void TTYDevice::emit(uint8_t c) {
	//Check if we should generate a signal
	if(_termios.c_lflag & ISIG) {
		if(c == _termios.c_cc[VINTR]) {
			generate_signal(SIGINT);
			return;
		}
		if(c == _termios.c_cc[VQUIT]) {
			generate_signal(SIGQUIT);
			return;
		}
		if(c == _termios.c_cc[VSUSP]) {
			generate_signal(SIGTSTP);
			return;
		}
	}

	//Canonical mode stuff
	if(_termios.c_lflag & ICANON) {
		if(c == _termios.c_cc[VEOF]) {
			_input_buffer.push('\0');
			_lines++;
			_buffer_blocker.set_ready(true);
			return;
		}
		if(c == '\n' || c == _termios.c_cc[VEOL]) {
			_lines++;
			_buffer_blocker.set_ready(true);
		}
		if(c == _termios.c_cc[VERASE]) {
			backspace();
			return;
		}
		if(c == _termios.c_cc[VKILL]) {
			while(erase());
			_buffer_blocker.set_ready(_lines && !_input_buffer.empty());
			return;
		}
		if(c == _termios.c_cc[VWERASE]) {
			while(_input_buffer.back() == ' ' && erase());
			while(_input_buffer.back() != ' ' && erase());
			_buffer_blocker.set_ready(_lines && !_input_buffer.empty());
			return;
		}
	}

	_input_buffer.push(c);
	echo(c);
}

bool TTYDevice::can_read(const FileDescriptor& fd) {
	return _termios.c_lflag & ICANON ? _lines : _input_buffer.empty();
}

bool TTYDevice::can_write(const FileDescriptor& fd) {
	return true;
}

int TTYDevice::ioctl(unsigned int request, void* argp) {
	auto* cur_proc = TaskManager::current_process();
	auto* termios_arg = (termios*)argp;
	switch(request) {
		case TIOCSCTTY:
			cur_proc->set_tty(shared_ptr());
			return SUCCESS;
		case TIOCNOTTY:
			cur_proc->set_tty(kstd::shared_ptr<TTYDevice>(nullptr));
			return SUCCESS;
		case TIOCGPGRP:
			return _pgid;
		case TIOCSPGRP: {
			auto pgid = (pid_t) argp;
			if(pgid <= 0)
				return -EINVAL;
			auto* proc = TaskManager::process_for_pgid(pgid);
			if(!proc)
				return -EINVAL;
			if(cur_proc->sid() != proc->sid())
				return -EPERM;
			_pgid = pgid;
			return SUCCESS;
		}
		case TCGETS:
			*termios_arg = _termios;
			return SUCCESS;
		case TCSETS:
		case TCSETSF:
		case TCSETSW:
			_termios = *termios_arg;
			return SUCCESS;
		default:
			return -EINVAL;
	}
}

void TTYDevice::generate_signal(int sig) {
	if(_pgid == 0)
		return;
	TaskManager::kill_pgid(_pgid, sig);
}

bool TTYDevice::backspace() {
	//Check if we can backspace and do so
	char last_char = _input_buffer.back();
	if(!_input_buffer.empty() && last_char != '\0' && last_char != _termios.c_cc[VEOL]) {
		_input_buffer.pop_back();
		echo('\b');
		echo(' ');
		echo('\b');
		return true;
	} else {
		return false;
	}
}

bool TTYDevice::erase() {
	//Check if we can erase and do so
	char last_char = _input_buffer.back();
	if(!_input_buffer.empty() && last_char != '\0' && last_char != _termios.c_cc[VEOL]) {
		_input_buffer.pop_back();
		echo(_termios.c_cc[VERASE]);
		echo(' ');
		echo(_termios.c_cc[VERASE]);
		return true;
	} else {
		return false;
	}
}
