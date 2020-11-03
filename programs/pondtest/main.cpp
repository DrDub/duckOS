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

#include <libpond/pond.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgraphics/font.h>

int main() {
	auto* pond = PContext::init();
	if(!pond)
		exit(-1);

	PWindow* window = pond->create_window(nullptr, 50, 50, 100, 100);
	if(!window)
		exit(-1);

	window->set_title("Hello World");
	window->framebuffer.fill({0, 0, 100, 100}, RGBA(0, 0, 0, 200));
	Font* font = pond->get_font("gohu-14");
	window->framebuffer.draw_text("This is text", {3,43}, font, RGB(255, 255, 255));
	window->invalidate();

	while(1) {
		PEvent event = pond->next_event();
		if(event.type == PEVENT_KEY) {
			if(event.key.character == 'q')
				exit(0);
		} else if(event.type == PEVENT_WINDOW_DESTROY)
			break;
	}
}